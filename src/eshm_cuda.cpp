// GPU VRAM sharing. See include/eshm_cuda.h for the contract.
//
// Rendezvous wire format sent alongside the SCM_RIGHTS fd: a fixed
// WireHeader (magic/version/device/size/generation), nothing else - the fd
// itself carries the actual memory handle via the OS.
//
// This file never links against libcuda.so. cuda.h is a pure declaration
// header (no library needed to compile against it), and every driver call is
// resolved with dlopen()/dlsym() at first use instead - see the `driver`
// namespace below. That is what lets libeshm_cuda.so ship in the exact same
// package as the rest of ESHM and be linked unconditionally by an
// application: on a machine with no NVIDIA driver at all, the library still
// loads and links fine, and eshm_cuda_create()/attach() simply fail with a
// clear error instead of the whole process refusing to start over a missing
// libcuda.so.1. See scripts/export_deb.sh for the packaging side of this.

#include "eshm_cuda.h"

#include <cuda.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

thread_local char g_last_error[256] = {0};

void set_error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void set_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, args);
    va_end(args);
}

}  // namespace

// ---------------------------------------------------------------------------
// Driver API, loaded lazily. Every symbol here is unversioned in the ABI
// sense (verified against the CUDA 13.2 headers this was built with: none of
// these have a "#define cuFoo cuFoo_v2/v3/v4" redirect except
// cuDevicePrimaryCtxRelease, whose real symbol name - cuDevicePrimaryCtxRelease_v2
// - has been stable since that function's introduction). That stability is
// exactly what the driver API is for: a binary built against any reasonably
// recent CUDA toolkit's headers runs unchanged against any driver new enough
// to support VMM POSIX-fd export (R470+), with no per-CUDA-version rebuild.
namespace driver {

using PFN_cuInit = CUresult(CUDAAPI*)(unsigned int);
using PFN_cuGetErrorName = CUresult(CUDAAPI*)(CUresult, const char**);
using PFN_cuGetErrorString = CUresult(CUDAAPI*)(CUresult, const char**);
using PFN_cuDeviceGet = CUresult(CUDAAPI*)(CUdevice*, int);
using PFN_cuDevicePrimaryCtxRetain = CUresult(CUDAAPI*)(CUcontext*, CUdevice);
using PFN_cuDevicePrimaryCtxRelease = CUresult(CUDAAPI*)(CUdevice);
using PFN_cuCtxSetCurrent = CUresult(CUDAAPI*)(CUcontext);
using PFN_cuMemGetAllocationGranularity =
    CUresult(CUDAAPI*)(size_t*, const CUmemAllocationProp*, CUmemAllocationGranularity_flags);
using PFN_cuMemCreate =
    CUresult(CUDAAPI*)(CUmemGenericAllocationHandle*, size_t, const CUmemAllocationProp*, unsigned long long);
using PFN_cuMemAddressReserve = CUresult(CUDAAPI*)(CUdeviceptr*, size_t, size_t, CUdeviceptr, unsigned long long);
using PFN_cuMemMap = CUresult(CUDAAPI*)(CUdeviceptr, size_t, size_t, CUmemGenericAllocationHandle, unsigned long long);
using PFN_cuMemSetAccess = CUresult(CUDAAPI*)(CUdeviceptr, size_t, const CUmemAccessDesc*, size_t);
using PFN_cuMemExportToShareableHandle =
    CUresult(CUDAAPI*)(void*, CUmemGenericAllocationHandle, CUmemAllocationHandleType, unsigned long long);
using PFN_cuMemImportFromShareableHandle =
    CUresult(CUDAAPI*)(CUmemGenericAllocationHandle*, void*, CUmemAllocationHandleType);
using PFN_cuMemUnmap = CUresult(CUDAAPI*)(CUdeviceptr, size_t);
using PFN_cuMemAddressFree = CUresult(CUDAAPI*)(CUdeviceptr, size_t);
using PFN_cuMemRelease = CUresult(CUDAAPI*)(CUmemGenericAllocationHandle);

struct Api {
    PFN_cuInit Init = nullptr;
    PFN_cuGetErrorName GetErrorName = nullptr;
    PFN_cuGetErrorString GetErrorString = nullptr;
    PFN_cuDeviceGet DeviceGet = nullptr;
    PFN_cuDevicePrimaryCtxRetain DevicePrimaryCtxRetain = nullptr;
    PFN_cuDevicePrimaryCtxRelease DevicePrimaryCtxRelease = nullptr;
    PFN_cuCtxSetCurrent CtxSetCurrent = nullptr;
    PFN_cuMemGetAllocationGranularity MemGetAllocationGranularity = nullptr;
    PFN_cuMemCreate MemCreate = nullptr;
    PFN_cuMemAddressReserve MemAddressReserve = nullptr;
    PFN_cuMemMap MemMap = nullptr;
    PFN_cuMemSetAccess MemSetAccess = nullptr;
    PFN_cuMemExportToShareableHandle MemExportToShareableHandle = nullptr;
    PFN_cuMemImportFromShareableHandle MemImportFromShareableHandle = nullptr;
    PFN_cuMemUnmap MemUnmap = nullptr;
    PFN_cuMemAddressFree MemAddressFree = nullptr;
    PFN_cuMemRelease MemRelease = nullptr;
};

Api g_api;
std::atomic<int> g_state{0};  // 0 = not tried, 1 = loaded, -1 = failed
std::once_flag g_load_once;
std::once_flag g_cuinit_once;
char g_load_error[256] = {0};

template <typename Fn>
bool bind(void* handle, const char* symbol, Fn* out) {
    void* p = dlsym(handle, symbol);
    if (!p) return false;
    *out = reinterpret_cast<Fn>(p);
    return true;
}

void load() {
    // The driver's own userspace library, not the CUDA Toolkit's libcudart -
    // ships with the NVIDIA driver package, present on any machine with a
    // working GPU setup and nothing else.
    void* handle = dlopen("libcuda.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        snprintf(g_load_error, sizeof(g_load_error),
                 "libcuda.so.1 not found (%s) - no NVIDIA driver installed?", dlerror());
        g_state.store(-1);
        return;
    }

    bool ok = true;
    ok &= bind(handle, "cuInit", &g_api.Init);
    ok &= bind(handle, "cuGetErrorName", &g_api.GetErrorName);
    ok &= bind(handle, "cuGetErrorString", &g_api.GetErrorString);
    ok &= bind(handle, "cuDeviceGet", &g_api.DeviceGet);
    ok &= bind(handle, "cuDevicePrimaryCtxRetain", &g_api.DevicePrimaryCtxRetain);
    ok &= bind(handle, "cuDevicePrimaryCtxRelease_v2", &g_api.DevicePrimaryCtxRelease);
    ok &= bind(handle, "cuCtxSetCurrent", &g_api.CtxSetCurrent);
    ok &= bind(handle, "cuMemGetAllocationGranularity", &g_api.MemGetAllocationGranularity);
    ok &= bind(handle, "cuMemCreate", &g_api.MemCreate);
    ok &= bind(handle, "cuMemAddressReserve", &g_api.MemAddressReserve);
    ok &= bind(handle, "cuMemMap", &g_api.MemMap);
    ok &= bind(handle, "cuMemSetAccess", &g_api.MemSetAccess);
    ok &= bind(handle, "cuMemExportToShareableHandle", &g_api.MemExportToShareableHandle);
    ok &= bind(handle, "cuMemImportFromShareableHandle", &g_api.MemImportFromShareableHandle);
    ok &= bind(handle, "cuMemUnmap", &g_api.MemUnmap);
    ok &= bind(handle, "cuMemAddressFree", &g_api.MemAddressFree);
    ok &= bind(handle, "cuMemRelease", &g_api.MemRelease);

    if (!ok) {
        snprintf(g_load_error, sizeof(g_load_error),
                 "libcuda.so.1 found but missing an expected symbol - "
                 "driver too old for VMM memory sharing (need R470+)");
        g_state.store(-1);
        return;
    }

    g_state.store(1);
}

// Loads libcuda.so.1 and resolves every symbol on first call; every later
// call is a single atomic load. Returns the API table, or nullptr with
// set_error() already called.
const Api* ensure_loaded() {
    std::call_once(g_load_once, load);
    if (g_state.load() != 1) {
        set_error("%s", g_load_error);
        return nullptr;
    }
    std::call_once(g_cuinit_once, []() { g_api.Init(0); });
    return &g_api;
}

}  // namespace driver

namespace {

void set_cuda_error(const char* what, CUresult err) {
    const char* name = nullptr;
    const char* desc = nullptr;
    driver::g_api.GetErrorName(err, &name);
    driver::g_api.GetErrorString(err, &desc);
    set_error("%s failed: %s (%s)", what, name ? name : "?", desc ? desc : "?");
}

// Sanity stamp for the handshake, not a compatibility promise - bump
// WIRE_VERSION if the header layout ever changes.
constexpr uint64_t make_magic() {
    const char s[8] = {'E', 'S', 'H', 'C', 'U', 'D', 'A', '1'};
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | (unsigned char)s[i];
    return v;
}
constexpr uint64_t WIRE_MAGIC = make_magic();
constexpr uint32_t WIRE_VERSION = 1;

struct WireHeader {
    uint64_t magic;
    uint32_t version;
    uint32_t device_ordinal;
    uint64_t size;
    uint64_t generation;
};

std::string socket_name(const std::string& name) {
    return "eshm_cuda_" + name;
}

// Fills in an abstract-namespace AF_UNIX address for `name`. Returns false if
// the name is too long to fit.
bool make_address(const std::string& name, struct sockaddr_un* addr, socklen_t* addrlen) {
    std::string sname = socket_name(name);
    if (sname.size() >= sizeof(addr->sun_path) - 1) return false;
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    addr->sun_path[0] = '\0';
    memcpy(addr->sun_path + 1, sname.data(), sname.size());
    *addrlen = (socklen_t)(sizeof(sa_family_t) + 1 + sname.size());
    return true;
}

bool send_fd(int sock, int fd_to_send, const WireHeader& hdr) {
    struct msghdr msg = {};
    struct iovec io = {const_cast<WireHeader*>(&hdr), sizeof(hdr)};
    char cbuf[CMSG_SPACE(sizeof(int))] = {};

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int));

    return sendmsg(sock, &msg, MSG_NOSIGNAL) >= 0;
}

bool recv_fd(int sock, WireHeader* hdr, int* fd_out) {
    struct msghdr msg = {};
    struct iovec io = {hdr, sizeof(*hdr)};
    char cbuf[CMSG_SPACE(sizeof(int))] = {};

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    ssize_t n = recvmsg(sock, &msg, 0);
    if (n != (ssize_t)sizeof(*hdr)) return false;

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) return false;

    memcpy(fd_out, CMSG_DATA(cmsg), sizeof(int));
    return true;
}

}  // namespace

struct EshmCudaBuffer {
    std::string name;
    int device_ordinal = 0;
    CUdevice device = 0;
    CUcontext primary_ctx = nullptr;
    CUdeviceptr dptr = 0;
    size_t size = 0;
    uint64_t generation = 1;

    CUmemGenericAllocationHandle mem_handle = 0;
    int owned_fd = -1;  // producer's exported fd, or consumer's imported fd

    // Producer-only: thread accepting attachers on listen_sock.
    int listen_sock = -1;
    std::thread listener_thread;
    std::atomic<bool> running{false};
};

namespace {

// Shared teardown for both eshm_cuda_destroy() and a failed eshm_cuda_create()/
// eshm_cuda_attach() - every field defaults to a sentinel (0/-1/nullptr) so
// this is safe to call on a partially-constructed buffer. Only ever reached
// after a successful driver::ensure_loaded(), since every field it touches
// is set by driver calls that could not have succeeded otherwise.
void cleanup_buffer(EshmCudaBuffer* buf) {
    if (buf->running.exchange(false) && buf->listen_sock >= 0) {
        // Poke our own listening socket so a blocked accept() wakes up.
        struct sockaddr_un addr;
        socklen_t addrlen;
        if (make_address(buf->name, &addr, &addrlen)) {
            int poke = socket(AF_UNIX, SOCK_STREAM, 0);
            if (poke >= 0) {
                connect(poke, (struct sockaddr*)&addr, addrlen);
                close(poke);
            }
        }
    }
    if (buf->listener_thread.joinable()) {
        buf->listener_thread.join();
    }
    if (buf->listen_sock >= 0) {
        close(buf->listen_sock);
        buf->listen_sock = -1;
    }

    const driver::Api& api = driver::g_api;
    if (buf->dptr != 0) {
        api.MemUnmap(buf->dptr, buf->size);
        api.MemAddressFree(buf->dptr, buf->size);
        buf->dptr = 0;
    }
    if (buf->mem_handle != 0) {
        api.MemRelease(buf->mem_handle);
        buf->mem_handle = 0;
    }
    if (buf->owned_fd >= 0) {
        close(buf->owned_fd);
        buf->owned_fd = -1;
    }
    if (buf->primary_ctx) {
        api.DevicePrimaryCtxRelease(buf->device);
        buf->primary_ctx = nullptr;
    }
}

}  // namespace

EshmCudaBuffer* eshm_cuda_create(const EshmCudaConfig* config) {
    if (!config || !config->name || config->size == 0) {
        set_error("invalid parameters");
        return nullptr;
    }

    const driver::Api* api = driver::ensure_loaded();
    if (!api) return nullptr;

    auto* buf = new EshmCudaBuffer();
    buf->name = config->name;
    buf->device_ordinal = config->device_ordinal;

    CUresult err = api->DeviceGet(&buf->device, buf->device_ordinal);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuDeviceGet", err);
        delete buf;
        return nullptr;
    }

    err = api->DevicePrimaryCtxRetain(&buf->primary_ctx, buf->device);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuDevicePrimaryCtxRetain", err);
        delete buf;
        return nullptr;
    }
    api->CtxSetCurrent(buf->primary_ctx);

    CUmemAllocationProp prop = {};
    prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    prop.location.id = buf->device;
    prop.requestedHandleTypes = CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR;

    size_t granularity = 0;
    err = api->MemGetAllocationGranularity(&granularity, &prop, CU_MEM_ALLOC_GRANULARITY_MINIMUM);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemGetAllocationGranularity", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }
    buf->size = ((config->size + granularity - 1) / granularity) * granularity;

    err = api->MemCreate(&buf->mem_handle, buf->size, &prop, 0);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemCreate", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    err = api->MemAddressReserve(&buf->dptr, buf->size, 0, 0, 0);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemAddressReserve", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    err = api->MemMap(buf->dptr, buf->size, 0, buf->mem_handle, 0);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemMap", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    CUmemAccessDesc access = {};
    access.location = prop.location;
    access.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
    err = api->MemSetAccess(buf->dptr, buf->size, &access, 1);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemSetAccess", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    err = api->MemExportToShareableHandle(&buf->owned_fd, buf->mem_handle,
                                           CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR, 0);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemExportToShareableHandle", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    struct sockaddr_un addr;
    socklen_t addrlen;
    if (!make_address(buf->name, &addr, &addrlen)) {
        set_error("name too long for rendezvous socket: '%s'", buf->name.c_str());
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    buf->listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (buf->listen_sock < 0) {
        set_error("socket() failed: %s", strerror(errno));
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }
    if (bind(buf->listen_sock, (struct sockaddr*)&addr, addrlen) < 0) {
        set_error("bind() failed for '%s': %s (already have a producer for this name?)",
                   buf->name.c_str(), strerror(errno));
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }
    if (listen(buf->listen_sock, 16) < 0) {
        set_error("listen() failed: %s", strerror(errno));
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    buf->running.store(true);
    buf->listener_thread = std::thread([buf]() {
        while (true) {
            int conn = accept(buf->listen_sock, nullptr, nullptr);
            if (!buf->running.load()) {
                if (conn >= 0) close(conn);
                break;
            }
            if (conn < 0) continue;

            WireHeader hdr;
            hdr.magic = WIRE_MAGIC;
            hdr.version = WIRE_VERSION;
            hdr.device_ordinal = (uint32_t)buf->device_ordinal;
            hdr.size = buf->size;
            hdr.generation = buf->generation;
            send_fd(conn, buf->owned_fd, hdr);
            close(conn);
        }
    });

    return buf;
}

EshmCudaBuffer* eshm_cuda_attach(const char* name, uint32_t timeout_ms) {
    if (!name) {
        set_error("invalid parameters");
        return nullptr;
    }

    const driver::Api* api = driver::ensure_loaded();
    if (!api) return nullptr;

    struct sockaddr_un addr;
    socklen_t addrlen;
    if (!make_address(name, &addr, &addrlen)) {
        set_error("name too long for rendezvous socket: '%s'", name);
        return nullptr;
    }

    int sock = -1;
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            set_error("socket() failed: %s", strerror(errno));
            return nullptr;
        }
        if (connect(sock, (struct sockaddr*)&addr, addrlen) == 0) break;
        close(sock);
        sock = -1;

        if (timeout_ms == 0) {
            set_error("no producer for '%s' yet", name);
            return nullptr;
        }
        if (timeout_ms != ESHM_CUDA_TIMEOUT_INFINITE) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
            if (elapsed >= timeout_ms) {
                set_error("timed out waiting for producer '%s'", name);
                return nullptr;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    WireHeader hdr = {};
    int fd = -1;
    bool ok = recv_fd(sock, &hdr, &fd);
    close(sock);
    if (!ok) {
        set_error("failed to receive VRAM handle from producer '%s'", name);
        return nullptr;
    }
    if (hdr.magic != WIRE_MAGIC || hdr.version != WIRE_VERSION) {
        close(fd);
        set_error("protocol mismatch attaching to '%s' (rebuild both sides against the same eshm_cuda)",
                   name);
        return nullptr;
    }

    auto* buf = new EshmCudaBuffer();
    buf->name = name;
    buf->device_ordinal = (int)hdr.device_ordinal;
    buf->size = hdr.size;
    buf->generation = hdr.generation;
    buf->owned_fd = fd;

    CUresult err = api->DeviceGet(&buf->device, buf->device_ordinal);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuDeviceGet", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    err = api->DevicePrimaryCtxRetain(&buf->primary_ctx, buf->device);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuDevicePrimaryCtxRetain", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }
    api->CtxSetCurrent(buf->primary_ctx);

    err = api->MemImportFromShareableHandle(&buf->mem_handle, (void*)(intptr_t)buf->owned_fd,
                                             CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemImportFromShareableHandle", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    err = api->MemAddressReserve(&buf->dptr, buf->size, 0, 0, 0);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemAddressReserve", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    err = api->MemMap(buf->dptr, buf->size, 0, buf->mem_handle, 0);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemMap", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    CUmemAccessDesc access = {};
    access.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    access.location.id = buf->device;
    access.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
    err = api->MemSetAccess(buf->dptr, buf->size, &access, 1);
    if (err != CUDA_SUCCESS) {
        set_cuda_error("cuMemSetAccess", err);
        cleanup_buffer(buf);
        delete buf;
        return nullptr;
    }

    return buf;
}

int eshm_cuda_get_ptr(EshmCudaBuffer* buf, void** devptr, size_t* size) {
    if (!buf || !devptr) {
        set_error("invalid parameters");
        return ESHM_ERROR_INVALID_PARAM;
    }
    *devptr = (void*)(uintptr_t)buf->dptr;
    if (size) *size = buf->size;
    return ESHM_SUCCESS;
}

int eshm_cuda_device(EshmCudaBuffer* buf) {
    return buf ? buf->device_ordinal : -1;
}

uint64_t eshm_cuda_generation(EshmCudaBuffer* buf) {
    return buf ? buf->generation : 0;
}

void eshm_cuda_destroy(EshmCudaBuffer* buf) {
    if (!buf) return;
    cleanup_buffer(buf);
    delete buf;
}

const char* eshm_cuda_get_last_error(void) {
    return g_last_error[0] ? g_last_error : "no error";
}
