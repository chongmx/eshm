// Self-test for the ESHM shared memory IPC library.
//
//   test_selftest                 self-test: forks a slave child and drives it as master
//   test_selftest master [name]   run only the master side (pair it with a slave)
//   test_selftest slave  [name]   run only the slave side (echo responder)
//
// Protocol used by the test: the master sends a payload, the slave answers with
// "OK <bytes> <fnv1a-hash>" describing what it received.  The master compares
// that against the summary of what it sent, so both the length and every byte
// of the round trip are verified.  "QUIT" ends the slave.

#include "eshm.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int kPingCount        = 20;     // small messages exchanged
constexpr int kReplyTimeoutMs   = 2000;   // per-message reply budget
constexpr int kConnectTimeoutMs = 5000;   // slave waiting for the master's segment
constexpr int kSlaveLifetimeMs  = 60000;  // slave gives up eventually, never hangs

int g_checks = 0;
int g_failed = 0;

bool check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failed;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    std::fflush(stdout);
    return ok;
}

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint32_t fnv1a(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

// The reply the slave is expected to produce for a given payload.
std::string summary(const void* data, size_t len) {
    char out[64];
    std::snprintf(out, sizeof(out), "OK %zu %08x", len, fnv1a(data, len));
    return out;
}

std::vector<uint8_t> make_payload(size_t len, uint32_t seed) {
    std::vector<uint8_t> v(len);
    uint32_t x = seed | 1u;
    for (size_t i = 0; i < len; ++i) {
        x = x * 1664525u + 1013904223u;
        v[i] = static_cast<uint8_t>(x >> 24);
    }
    return v;
}

// ---------------------------------------------------------------------------
// Slave side
// ---------------------------------------------------------------------------

// eshm_init() as SLAVE fails immediately when the segment does not exist yet,
// so a slave started alongside its master has to retry the attach itself.
ESHMHandle* connect_slave(const char* shm_name, int timeout_ms) {
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;        // the master owns the segment
    config.max_reconnect_attempts = 0;  // unlimited reconnects once attached
    config.reconnect_wait_ms = 0;

    const int64_t deadline = now_ms() + timeout_ms;
    for (;;) {
        if (ESHMHandle* handle = eshm_init(&config)) return handle;
        if (now_ms() >= deadline) return nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int run_slave(const char* shm_name) {
    ESHMHandle* handle = connect_slave(shm_name, kConnectTimeoutMs);
    if (!handle) {
        std::fprintf(stderr, "slave: could not attach to '%s'\n", shm_name);
        return 1;
    }

    std::vector<uint8_t> buffer(ESHM_MAX_DATA_SIZE);
    const int64_t deadline = now_ms() + kSlaveLifetimeMs;
    bool linked = false;
    int replies = 0;

    while (now_ms() < deadline) {
        // A reader only sees writes that happen after its first eshm_read(), so
        // keep announcing readiness until the master's first message arrives.
        if (!linked) eshm_write(handle, "READY", 6);

        size_t n = 0;
        if (eshm_read_ex(handle, buffer.data(), buffer.size(), &n, 50) != ESHM_SUCCESS) continue;

        linked = true;
        if (n == 5 && std::memcmp(buffer.data(), "QUIT", 5) == 0) break;

        const std::string reply = summary(buffer.data(), n);
        eshm_write(handle, reply.c_str(), reply.size() + 1);
        ++replies;
    }

    std::printf("slave: answered %d message(s)\n", replies);
    std::fflush(stdout);
    eshm_destroy(handle);
    return 0;
}

// ---------------------------------------------------------------------------
// Master side
// ---------------------------------------------------------------------------

// Waits for two "READY" beacons: the second one proves the slave has completed
// at least one eshm_read(), so it cannot miss the first real message.
bool wait_for_slave(ESHMHandle* handle, int timeout_ms) {
    std::vector<char> buffer(ESHM_MAX_DATA_SIZE);
    const int64_t deadline = now_ms() + timeout_ms;
    int beacons = 0;

    while (now_ms() < deadline) {
        size_t n = 0;
        if (eshm_read_ex(handle, buffer.data(), buffer.size(), &n, 100) != ESHM_SUCCESS) continue;
        if (n >= 5 && std::memcmp(buffer.data(), "READY", 5) == 0 && ++beacons == 2) return true;
    }
    return false;
}

// Sends one payload and verifies the slave's summary of what it received.
bool exchange(ESHMHandle* handle, const void* data, size_t len, const std::string& label) {
    const int rc = eshm_write(handle, data, len);
    if (rc != ESHM_SUCCESS) {
        return check(false, label + ": write failed (" + eshm_error_string(rc) + ")");
    }

    const std::string expected = summary(data, len);
    std::vector<char> buffer(ESHM_MAX_DATA_SIZE);
    const int64_t deadline = now_ms() + kReplyTimeoutMs;

    while (now_ms() < deadline) {
        size_t n = 0;
        if (eshm_read_ex(handle, buffer.data(), buffer.size(), &n, 100) != ESHM_SUCCESS) continue;

        const std::string reply(buffer.data(), strnlen(buffer.data(), n));
        if (reply.rfind("OK ", 0) != 0) continue;  // a late READY beacon
        if (reply == expected) return check(true, label + ": " + reply);
        return check(false, label + ": got '" + reply + "', expected '" + expected + "'");
    }
    return check(false, label + ": no reply within " + std::to_string(kReplyTimeoutMs) + "ms");
}

int run_master(const char* shm_name) {
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!check(handle != nullptr, "eshm_init as master")) return 1;

    ESHMRole role = ESHM_ROLE_AUTO;
    check(eshm_get_role(handle, &role) == ESHM_SUCCESS && role == ESHM_ROLE_MASTER,
          "eshm_get_role reports MASTER");

    if (!check(wait_for_slave(handle, kConnectTimeoutMs), "slave connected and ready")) {
        eshm_destroy(handle);
        return 1;
    }

    // 1. A burst of small text messages.
    for (int i = 1; i <= kPingCount; ++i) {
        char msg[64];
        const int len = std::snprintf(msg, sizeof(msg), "PING #%d", i) + 1;  // include NUL
        const bool ok = exchange(handle, msg, static_cast<size_t>(len),
                                 "round trip " + std::to_string(i) + "/" +
                                     std::to_string(kPingCount));
        if (!ok) break;
    }

    // 2. Boundary sizes: one byte and a full channel.
    const uint8_t one = 0x42;
    exchange(handle, &one, 1, "1-byte payload");

    const std::vector<uint8_t> big = make_payload(ESHM_MAX_DATA_SIZE, 0xC0FFEEu);
    exchange(handle, big.data(), big.size(),
             std::to_string(ESHM_MAX_DATA_SIZE) + "-byte payload (channel limit)");

    // 3. Statistics reflect the traffic above.
    ESHMStats stats;
    std::memset(&stats, 0, sizeof(stats));
    if (check(eshm_get_stats(handle, &stats) == ESHM_SUCCESS, "eshm_get_stats succeeds")) {
        check(stats.master_pid == getpid(), "stats.master_pid is this process");
        check(stats.slave_pid > 0, "stats.slave_pid is set");
        check(stats.m2s_write_count >= static_cast<uint64_t>(kPingCount) + 2,
              "master->slave write count covers every message");
        check(stats.s2m_write_count >= static_cast<uint64_t>(kPingCount) + 2,
              "slave->master write count covers every reply");
        check(stats.master_heartbeat > 0, "master heartbeat thread is running");
        check(stats.slave_heartbeat > 0, "slave heartbeat is visible");
    }

    bool alive = false;
    check(eshm_check_remote_alive(handle, &alive) == ESHM_SUCCESS && alive,
          "eshm_check_remote_alive reports the slave alive");

    eshm_write(handle, "QUIT", 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    check(eshm_destroy(handle) == ESHM_SUCCESS, "eshm_destroy succeeds");
    return 0;
}

// ---------------------------------------------------------------------------
// Single process checks - no peer involved, so results are deterministic
// ---------------------------------------------------------------------------

void local_api_checks(const char* shm_name) {
    ESHMConfig config = eshm_default_config(shm_name);
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!check(handle != nullptr, "eshm_init on a private segment")) return;

    char scratch[64];
    check(eshm_write(nullptr, "x", 1) == ESHM_ERROR_INVALID_PARAM,
          "write with NULL handle returns INVALID_PARAM");
    check(eshm_read_ex(handle, scratch, sizeof(scratch), nullptr, 0) == ESHM_ERROR_NO_DATA,
          "non-blocking read with no peer returns NO_DATA");
    check(eshm_read_ex(handle, scratch, sizeof(scratch), nullptr, 50) == ESHM_ERROR_TIMEOUT,
          "blocking read with no peer returns TIMEOUT");

    const std::vector<uint8_t> oversized(ESHM_MAX_DATA_SIZE + 1, 0xAB);
    check(eshm_write(handle, oversized.data(), oversized.size()) == ESHM_ERROR_BUFFER_TOO_SMALL,
          "write larger than ESHM_MAX_DATA_SIZE is rejected");

    check(std::strlen(eshm_error_string(ESHM_ERROR_TIMEOUT)) > 0,
          "eshm_error_string describes an error code");
    check(eshm_update_heartbeat(handle) == ESHM_SUCCESS, "eshm_update_heartbeat succeeds");

    check(eshm_destroy(handle) == ESHM_SUCCESS, "eshm_destroy succeeds");
}

// ---------------------------------------------------------------------------

int run_self_test() {
    const std::string base = "testeshm_" + std::to_string(getpid());

    std::printf("ESHM self-test (channel limit %d bytes)\n", ESHM_MAX_DATA_SIZE);

    std::printf("\n[1/2] single process API checks\n");
    local_api_checks((base + "_solo").c_str());

    std::printf("\n[2/2] master <-> slave exchange\n");
    std::fflush(stdout);

    const std::string pair_name = base + "_pair";
    const pid_t pid = fork();
    if (pid < 0) {
        check(false, "fork() a slave process");
        return 1;
    }
    if (pid == 0) {
        _exit(run_slave(pair_name.c_str()));
    }

    run_master(pair_name.c_str());

    // Reap the slave, killing it if it outlives the master.
    int status = 0;
    const int64_t deadline = now_ms() + 5000;
    while (waitpid(pid, &status, WNOHANG) == 0) {
        if (now_ms() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "slave process exited cleanly");

    std::printf("\n%d checks, %d failed -> %s\n", g_checks, g_failed,
                g_failed == 0 ? "PASS" : "FAIL");
    return g_failed == 0 ? 0 : 1;
}

void usage(const char* argv0) {
    std::printf("usage: %s [master|slave] [shm_name]\n"
                "  (no arguments)   run the self-test\n"
                "  master [name]    run the master side only\n"
                "  slave  [name]    run the slave side only\n",
                argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) return run_self_test();

    const std::string mode = argv[1];
    const char* name = (argc > 2) ? argv[2] : "test_eshm";

    if (mode == "master") {
        run_master(name);
        std::printf("\n%d checks, %d failed -> %s\n", g_checks, g_failed,
                    g_failed == 0 ? "PASS" : "FAIL");
        return g_failed == 0 ? 0 : 1;
    }
    if (mode == "slave") return run_slave(name);

    usage(argv[0]);
    return 2;
}
