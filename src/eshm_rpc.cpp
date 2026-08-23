// Named-trigger dispatcher. See include/eshm_rpc.h for the contract.
//
// Wire format: one DataHandler record of three scalar fields.
//
//     k  INTEGER   kind: 0 = call, 1 = event
//     n  STRING    handler name
//     s  INTEGER   sender's monotonic sequence number
//
// Deliberately built from the five types both codecs already speak, rather
// than DataHandler's FUNCTION_CALL/EVENT tags: those are implemented only in
// C++ and are rejected by the C API and the Python codec, and with no
// arguments they would carry nothing a STRING does not.

#include "eshm_rpc.h"
#include "data_handler.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace shm_protocol;

namespace {

constexpr int64_t KIND_CALL  = 0;
constexpr int64_t KIND_EVENT = 1;

thread_local char g_last_error[256] = {0};

void set_error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void set_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, args);
    va_end(args);
}

struct Registration {
    eshm_rpc_handler fn;
    void* user;
};

} // namespace

struct EshmRpc {
    ESHMHandle* handle = nullptr;
    std::string name;

    std::thread dispatcher;
    std::atomic<bool> running{false};

    // Guards the two tables. Handlers are looked up under the lock, then run
    // outside it, so a handler may register more handlers without deadlocking.
    std::mutex mutex;
    std::unordered_map<std::string, Registration> calls;
    std::unordered_map<std::string, std::vector<Registration>> events;

    // Serialises writers: eshm_write is safe against a concurrent reader, but
    // two threads writing at once would race on the sequence lock.
    std::mutex write_mutex;
    uint64_t out_seq = 0;

    uint64_t in_seq = 0;
    bool have_in_seq = false;
    std::atomic<uint64_t> missed{0};
    std::atomic<uint64_t> dispatched{0};

    DataHandler codec;
};

namespace {

int send(EshmRpc* rpc, int64_t kind, const char* name) {
    if (!rpc || !name) {
        set_error("invalid parameters");
        return ESHM_ERROR_INVALID_PARAM;
    }

    std::lock_guard<std::mutex> lock(rpc->write_mutex);

    std::vector<DataItem> items;
    items.push_back(DataHandler::createInteger("k", kind));
    items.push_back(DataHandler::createString("n", name));
    items.push_back(DataHandler::createInteger("s", static_cast<int64_t>(++rpc->out_seq)));

    try {
        const std::vector<uint8_t> buffer = rpc->codec.encodeDataBuffer(items);
        const int rc = eshm_write(rpc->handle, buffer.data(), buffer.size());
        if (rc != ESHM_SUCCESS) {
            set_error("write failed: %s", eshm_error_string(rc));
        }
        return rc;
    } catch (const std::exception& e) {
        set_error("encode failed: %s", e.what());
        return ESHM_ERROR_INVALID_PARAM;
    }
}

// Run whatever is registered for one decoded trigger. Called on the dispatcher
// thread with no locks held.
void dispatch(EshmRpc* rpc, int64_t kind, const std::string& name) {
    std::vector<Registration> to_run;
    bool known = true;

    {
        std::lock_guard<std::mutex> lock(rpc->mutex);
        if (kind == KIND_CALL) {
            auto it = rpc->calls.find(name);
            if (it != rpc->calls.end()) to_run.push_back(it->second);
            else known = false;
        } else {
            auto it = rpc->events.find(name);
            if (it != rpc->events.end()) to_run = it->second;
            // An event with no listeners is not an error - that is the whole
            // difference between an event and a call.
        }
    }

    if (!known) {
        fprintf(stderr, "[ESHM-RPC] no handler registered for call '%s'\n", name.c_str());
        return;
    }

    for (const Registration& r : to_run) {
        if (r.fn) {
            r.fn(r.user);
            rpc->dispatched.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void dispatcher_loop(EshmRpc* rpc) {
    std::vector<uint8_t> buffer(ESHM_MAX_DATA_SIZE);

    while (rpc->running.load(std::memory_order_acquire)) {
        size_t received = 0;
        // A bounded wait rather than ESHM_TIMEOUT_INFINITE, so stop() is
        // observed promptly. Push wakeup means an idle park costs nothing.
        const int rc = eshm_read_ex(rpc->handle, buffer.data(), buffer.size(),
                                    &received, 200);
        if (rc != ESHM_SUCCESS || received == 0) {
            continue;   // timeout, no data, or a peer that went away
        }

        int64_t kind = -1;
        int64_t seq = 0;
        std::string name;
        try {
            auto values = DataHandler::extractSimpleValues(
                rpc->codec.decodeDataBuffer(buffer.data(), received));
            kind = std::get<int64_t>(values.at("k"));
            name = std::get<std::string>(values.at("n"));
            seq  = std::get<int64_t>(values.at("s"));
        } catch (const std::exception& e) {
            fprintf(stderr, "[ESHM-RPC] undecodable trigger ignored: %s\n", e.what());
            continue;
        }

        // Gaps mean the peer fired faster than we drained; the channel holds
        // one value per direction, so intermediate triggers were overwritten.
        // Harmless for level-triggered handlers, worth counting regardless.
        const uint64_t useq = static_cast<uint64_t>(seq);
        if (rpc->have_in_seq && useq > rpc->in_seq + 1) {
            rpc->missed.fetch_add(useq - rpc->in_seq - 1, std::memory_order_relaxed);
        }
        rpc->in_seq = useq;
        rpc->have_in_seq = true;

        dispatch(rpc, kind, name);
    }
}

} // namespace

extern "C" {

const char* eshm_rpc_get_last_error(void) {
    return g_last_error;
}

EshmRpc* eshm_rpc_create(const char* channel, enum ESHMRole role) {
    if (!channel) {
        set_error("channel name is NULL");
        return nullptr;
    }

    EshmRpc* rpc = new (std::nothrow) EshmRpc();
    if (!rpc) {
        set_error("out of memory");
        return nullptr;
    }

    // Always a channel of our own: the dispatcher must be the sole reader, and
    // a trigger sharing the data channel would overwrite the data it announces.
    rpc->name = std::string(channel) + "_ctl";

    ESHMConfig config = eshm_default_config(rpc->name.c_str());
    config.role = role;
    if (role == ESHM_ROLE_SLAVE) {
        config.auto_cleanup = false;         // the master owns the segment
        config.max_reconnect_attempts = 0;   // survive a master restart
        config.reconnect_wait_ms = 0;
    }

    // A slave cannot attach before the master has created the segment.
    const int attempts = (role == ESHM_ROLE_MASTER) ? 1 : 50;
    for (int i = 0; i < attempts && !rpc->handle; ++i) {
        rpc->handle = eshm_init(&config);
        if (!rpc->handle && i + 1 < attempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (!rpc->handle) {
        set_error("could not open control channel '%s'", rpc->name.c_str());
        delete rpc;
        return nullptr;
    }

    return rpc;
}

void eshm_rpc_destroy(EshmRpc* rpc) {
    if (!rpc) return;
    eshm_rpc_stop(rpc);
    if (rpc->handle) eshm_destroy(rpc->handle);
    delete rpc;
}

int eshm_rpc_on_call(EshmRpc* rpc, const char* name,
                     eshm_rpc_handler fn, void* user) {
    if (!rpc || !name || !fn) {
        set_error("invalid parameters");
        return ESHM_ERROR_INVALID_PARAM;
    }
    std::lock_guard<std::mutex> lock(rpc->mutex);
    rpc->calls[name] = Registration{fn, user};
    return ESHM_SUCCESS;
}

int eshm_rpc_on_event(EshmRpc* rpc, const char* name,
                      eshm_rpc_handler fn, void* user) {
    if (!rpc || !name || !fn) {
        set_error("invalid parameters");
        return ESHM_ERROR_INVALID_PARAM;
    }
    std::lock_guard<std::mutex> lock(rpc->mutex);
    rpc->events[name].push_back(Registration{fn, user});
    return ESHM_SUCCESS;
}

int eshm_rpc_start(EshmRpc* rpc) {
    if (!rpc) {
        set_error("invalid parameters");
        return ESHM_ERROR_INVALID_PARAM;
    }
    if (rpc->running.load()) return ESHM_SUCCESS;   // idempotent

    rpc->running.store(true, std::memory_order_release);
    try {
        rpc->dispatcher = std::thread(dispatcher_loop, rpc);
    } catch (const std::exception& e) {
        rpc->running.store(false);
        set_error("could not start dispatcher: %s", e.what());
        return ESHM_ERROR_NOT_INITIALIZED;
    }
    return ESHM_SUCCESS;
}

int eshm_rpc_stop(EshmRpc* rpc) {
    if (!rpc) {
        set_error("invalid parameters");
        return ESHM_ERROR_INVALID_PARAM;
    }
    if (!rpc->running.load()) return ESHM_SUCCESS;  // idempotent

    rpc->running.store(false, std::memory_order_release);
    if (rpc->dispatcher.joinable()) rpc->dispatcher.join();
    return ESHM_SUCCESS;
}

int eshm_rpc_call(EshmRpc* rpc, const char* name) {
    return send(rpc, KIND_CALL, name);
}

int eshm_rpc_emit(EshmRpc* rpc, const char* name) {
    return send(rpc, KIND_EVENT, name);
}

uint64_t eshm_rpc_missed(const EshmRpc* rpc) {
    return rpc ? rpc->missed.load(std::memory_order_relaxed) : 0;
}

uint64_t eshm_rpc_dispatched(const EshmRpc* rpc) {
    return rpc ? rpc->dispatched.load(std::memory_order_relaxed) : 0;
}

} // extern "C"
