// Exercises the ABI-stable C API that language bindings use:
//   dh_*            encode/decode without touching the C++ DataHandler
//   eshm_write_data encode + write in one call
//   eshm_read_data  read + decode in one call
//
// Master and slave run in one process (fork), so `ctest` can run it unattended.

#include "data_handler_c_api.h"
#include "eshm.h"
#include "eshm_data_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace {

int g_checks = 0;
int g_failed = 0;

bool check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failed;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    std::fflush(stdout);
    return ok;
}

struct BinaryValue {   // the representation dh_decode uses for DataType BINARY
    uint8_t* data;
    size_t len;
};

// One item of each simple type: 0=INTEGER 1=BOOLEAN 2=REAL 3=STRING
const uint8_t kTypes[] = {0, 1, 2, 3};
const char* kKeys[] = {"frame_id", "recording", "temperature_c", "camera"};

int64_t g_int = 42;
bool g_bool = true;
double g_real = 21.5;
char g_str[] = "front";
const void* kValues[] = {&g_int, &g_bool, &g_real, g_str};

// Storage for decoded keys: dh_decode/eshm_read_data write into caller buffers.
struct KeyBuffers {
    static constexpr int kMax = 8;
    static constexpr int kLen = 64;
    char storage[kMax][kLen];
    char* pointers[kMax];

    KeyBuffers() {
        for (int i = 0; i < kMax; ++i) pointers[i] = storage[i];
    }
};

bool values_match(const uint8_t* types, char** keys, void** values, int count,
                  const std::string& label) {
    if (!check(count == 4, label + ": four items decoded")) return false;

    bool ok = std::memcmp(types, kTypes, sizeof(kTypes)) == 0;
    ok = ok && std::strcmp(keys[0], "frame_id") == 0 && *static_cast<int64_t*>(values[0]) == 42;
    ok = ok && std::strcmp(keys[1], "recording") == 0 && *static_cast<bool*>(values[1]);
    ok = ok && std::strcmp(keys[2], "temperature_c") == 0 &&
         *static_cast<double*>(values[2]) > 21.49 && *static_cast<double*>(values[2]) < 21.51;
    ok = ok && std::strcmp(keys[3], "camera") == 0 &&
         std::strcmp(static_cast<char*>(values[3]), "front") == 0;
    return check(ok, label + ": every key and value survived the round trip");
}

// --- dh_* on its own ------------------------------------------------------

void test_data_handler_c_api() {
    std::printf("\n[1/2] DataHandler C API\n");

    DataHandlerHandle handler = dh_create();
    if (!check(handler != nullptr, "dh_create")) return;

    uint8_t buffer[4096];
    const int written = dh_encode(handler, kTypes, kKeys, kValues, 4, buffer, sizeof(buffer));
    if (!check(written > 0, "dh_encode returns " + std::to_string(written) + " bytes")) {
        dh_destroy(handler);
        return;
    }

    KeyBuffers keys;
    uint8_t types[KeyBuffers::kMax];
    void* values[KeyBuffers::kMax];
    const int count = dh_decode(handler, buffer, written, types, keys.pointers,
                                KeyBuffers::kLen, values, KeyBuffers::kMax);
    if (values_match(types, keys.pointers, values, count, "dh_decode")) {
        for (int i = 0; i < count; ++i) dh_free_value(types[i], values[i]);
    }

    check(dh_encode(handler, kTypes, kKeys, kValues, 4, buffer, 4) < 0,
          "dh_encode rejects a buffer that is too small");
    check(dh_get_last_error() != nullptr && dh_get_last_error()[0] != '\0',
          "dh_get_last_error describes the failure");

    dh_destroy(handler);
    check(true, "dh_destroy");
}

// --- eshm_write_data / eshm_read_data over real shared memory -------------

int slave_main(const char* shm_name) {
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;

    ESHMHandle* handle = nullptr;
    for (int i = 0; i < 50 && !handle; ++i) {
        handle = eshm_init(&config);
        if (!handle) usleep(100 * 1000);
    }
    if (!handle) return 1;

    // Answer once the master's structured message arrives.
    KeyBuffers keys;
    uint8_t types[KeyBuffers::kMax];
    void* values[KeyBuffers::kMax];
    int count = 0;

    int rc = ESHM_ERROR_TIMEOUT;
    for (int i = 0; i < 20 && rc != ESHM_SUCCESS; ++i) {
        rc = eshm_read_data(handle, types, keys.pointers, KeyBuffers::kLen,
                            values, KeyBuffers::kMax, &count, 500);
    }
    if (rc != ESHM_SUCCESS) {
        eshm_destroy(handle);
        return 2;
    }

    // Echo the same items back, so the master can verify a full cycle.
    const int written = eshm_write_data(handle, types, const_cast<const char**>(keys.pointers),
                                        const_cast<const void**>(values), count);
    for (int i = 0; i < count; ++i) eshm_data_free_value(types[i], values[i]);

    eshm_destroy(handle);
    return written > 0 ? 0 : 3;
}

void test_eshm_data_api() {
    std::printf("\n[2/2] ESHM + DataHandler C API over shared memory\n");

    const std::string shm_name = "eshm_capi_" + std::to_string(getpid());

    ESHMConfig config = eshm_default_config(shm_name.c_str());
    config.role = ESHM_ROLE_MASTER;
    ESHMHandle* handle = eshm_init(&config);
    if (!check(handle != nullptr, "master eshm_init")) return;

    const pid_t pid = fork();
    if (pid == 0) {
        _exit(slave_main(shm_name.c_str()));
    }
    if (!check(pid > 0, "fork a slave process")) {
        eshm_destroy(handle);
        return;
    }

    // The slave polls, so repeat the write until it answers.
    KeyBuffers keys;
    uint8_t types[KeyBuffers::kMax];
    void* values[KeyBuffers::kMax];
    int count = 0;
    int rc = ESHM_ERROR_TIMEOUT;

    for (int attempt = 0; attempt < 20 && rc != ESHM_SUCCESS; ++attempt) {
        const int written = eshm_write_data(handle, kTypes, kKeys, kValues, 4);
        if (attempt == 0) {
            check(written > 0, "eshm_write_data returns " + std::to_string(written) + " bytes");
        }
        rc = eshm_read_data(handle, types, keys.pointers, KeyBuffers::kLen,
                            values, KeyBuffers::kMax, &count, 300);
    }

    if (check(rc == ESHM_SUCCESS, "eshm_read_data received the slave's reply")) {
        if (values_match(types, keys.pointers, values, count, "eshm_read_data")) {
            for (int i = 0; i < count; ++i) eshm_data_free_value(types[i], values[i]);
        }
    }

    check(eshm_write_data(nullptr, kTypes, kKeys, kValues, 4) < 0,
          "eshm_write_data rejects a NULL handle");
    check(eshm_data_get_last_error() != nullptr, "eshm_data_get_last_error is available");

    int status = 0;
    waitpid(pid, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "slave process exited cleanly");

    eshm_destroy(handle);
}

}  // namespace

int main() {
    std::printf("C API test\n");
    test_data_handler_c_api();
    test_eshm_data_api();

    std::printf("\n%d checks, %d failed -> %s\n", g_checks, g_failed,
                g_failed == 0 ? "PASS" : "FAIL");
    return g_failed == 0 ? 0 : 1;
}
