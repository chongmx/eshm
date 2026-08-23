// Push wakeup: futex parking, wakeup modes, and the timeout contract.
//
// Covers what changed in protocol v3:
//   - the shared layout did not move
//   - ESHM_WAKEUP_PUSH parks instead of polling (measured in context switches
//     and CPU, not wall clock, so the result does not depend on how fast the
//     machine schedules)
//   - ESHM_WAKEUP_POLL restores the old behaviour
//   - timeout_ms == 0 is non-blocking, ESHM_TIMEOUT_INFINITE waits
//   - a finite timeout still expires on time
//   - a parked reader is woken by a write, from another process
//   - switching modes mid-flight is safe

#include <eshm.h>

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

double cpu_ms(const struct rusage& r) {
    return r.ru_utime.tv_sec * 1000.0 + r.ru_utime.tv_usec / 1000.0
         + r.ru_stime.tv_sec * 1000.0 + r.ru_stime.tv_usec / 1000.0;
}

double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t).count();
}

// ---------------------------------------------------------------------------
void test_layout() {
    std::printf("\n== shared layout ==\n");
    // Push wakeup was carved out of existing padding. If either struct grew,
    // a peer built from another revision reads every field at the wrong offset.
    check(sizeof(ESHMHeader) == 128, "ESHMHeader is still 128 bytes");
    check(sizeof(ESHMChannel) % 64 == 0, "ESHMChannel is still cache-line sized");
    check(sizeof(ESHMChannel) == 64 + ESHM_MAX_DATA_SIZE + 64,
          "ESHMChannel size is header + data + tail, unchanged by v3");
}

// ---------------------------------------------------------------------------
void test_timeout_contract() {
    std::printf("\n== timeout contract ==\n");

    ESHMConfig cfg = eshm_default_config("wakeup_timeouts");
    cfg.role = ESHM_ROLE_MASTER;
    ESHMHandle* h = eshm_init(&cfg);
    if (!h) { check(false, "master init"); return; }

    char buf[64];
    size_t n = 0;
    eshm_read_ex(h, buf, sizeof(buf), &n, 0);      // establish the read baseline

    // 0 means "try once" - the opposite of what 0 means in ESHMConfig.
    auto t0 = std::chrono::steady_clock::now();
    int rc = eshm_read_ex(h, buf, sizeof(buf), &n, 0);
    double elapsed = ms_since(t0);
    check(rc == ESHM_ERROR_NO_DATA, "timeout_ms=0 returns ESHM_ERROR_NO_DATA");
    check(elapsed < 5.0, "timeout_ms=0 returns immediately");

    // A finite timeout must still expire on time, whichever mode is in force.
    for (int pass = 0; pass < 2; ++pass) {
        const bool push = (pass == 0);
        eshm_set_wakeup_mode(h, push ? ESHM_WAKEUP_PUSH : ESHM_WAKEUP_POLL);
        t0 = std::chrono::steady_clock::now();
        rc = eshm_read_ex(h, buf, sizeof(buf), &n, 200);
        elapsed = ms_since(t0);
        std::printf("       %s: 200 ms timeout took %.1f ms\n", push ? "PUSH" : "POLL", elapsed);
        check(rc == ESHM_ERROR_TIMEOUT, push ? "PUSH: 200 ms timeout expires"
                                             : "POLL: 200 ms timeout expires");
        check(elapsed >= 190.0 && elapsed < 400.0,
              push ? "PUSH: 200 ms timeout is accurate" : "POLL: 200 ms timeout is accurate");
    }

    eshm_destroy(h);
}

// ---------------------------------------------------------------------------
void test_mode_accessors() {
    std::printf("\n== wakeup mode accessors ==\n");

    ESHMConfig cfg = eshm_default_config("wakeup_modes");
    cfg.role = ESHM_ROLE_MASTER;
    ESHMHandle* h = eshm_init(&cfg);
    if (!h) { check(false, "master init"); return; }

    ESHMWakeupMode mode;
    check(eshm_get_wakeup_mode(h, &mode) == ESHM_SUCCESS && mode == ESHM_WAKEUP_PUSH,
          "push wakeup is the default");

    check(eshm_set_wakeup_mode(h, ESHM_WAKEUP_POLL) == ESHM_SUCCESS &&
          eshm_get_wakeup_mode(h, &mode) == ESHM_SUCCESS && mode == ESHM_WAKEUP_POLL,
          "mode can be set to POLL");

    check(eshm_set_wakeup_mode(h, ESHM_WAKEUP_PUSH) == ESHM_SUCCESS &&
          eshm_get_wakeup_mode(h, &mode) == ESHM_SUCCESS && mode == ESHM_WAKEUP_PUSH,
          "mode can be set back to PUSH");

    check(eshm_set_wakeup_mode(h, (ESHMWakeupMode)99) == ESHM_ERROR_INVALID_PARAM,
          "an unknown mode is rejected");
    check(eshm_set_wakeup_mode(NULL, ESHM_WAKEUP_PUSH) == ESHM_ERROR_INVALID_PARAM,
          "a NULL handle is rejected");

    eshm_destroy(h);
}

// ---------------------------------------------------------------------------
// The point of the change: PUSH must actually park rather than spin. Measured
// in context switches and CPU time, which are machine-independent, rather than
// wall-clock latency, which on a virtualised host is dominated by the
// scheduler and would make this test flaky.
void test_push_actually_parks() {
    std::printf("\n== push parks instead of polling ==\n");

    struct Result { long ctx; double cpu; };
    auto measure = [](ESHMWakeupMode mode, const char* name) -> Result {
        ESHMConfig cfg = eshm_default_config(name);
        cfg.role = ESHM_ROLE_MASTER;
        cfg.use_threads = false;          // exclude heartbeat/monitor from the count
        ESHMHandle* h = eshm_init(&cfg);
        if (!h) return { -1, -1.0 };

        eshm_set_wakeup_mode(h, mode);
        char buf[64];
        size_t n = 0;
        eshm_read_ex(h, buf, sizeof(buf), &n, 0);     // baseline

        struct rusage a, b;
        getrusage(RUSAGE_SELF, &a);
        eshm_read_ex(h, buf, sizeof(buf), &n, 300);   // nobody ever writes
        getrusage(RUSAGE_SELF, &b);

        eshm_destroy(h);
        return { b.ru_nvcsw - a.ru_nvcsw, cpu_ms(b) - cpu_ms(a) };
    };

    Result push = measure(ESHM_WAKEUP_PUSH, "wakeup_park_push");
    Result poll = measure(ESHM_WAKEUP_POLL, "wakeup_park_poll");

    std::printf("       PUSH: %4ld context switches, %6.1f ms CPU over a 300 ms wait\n",
                push.ctx, push.cpu);
    std::printf("       POLL: %4ld context switches, %6.1f ms CPU over a 300 ms wait\n",
                poll.ctx, poll.cpu);

    check(push.ctx >= 0 && poll.ctx >= 0, "both handles initialised");
    // A 300 ms park wakes only on the internal re-check (~50 ms), so single
    // digits; a 100 us poll loop is three orders of magnitude busier.
    check(push.ctx < 50, "PUSH parks: fewer than 50 context switches");
    check(poll.ctx > 500, "POLL polls: more than 500 context switches");
    check(push.cpu < poll.cpu, "PUSH uses less CPU than POLL while idle");
}

// ---------------------------------------------------------------------------
// A parked reader must be woken by a write from a genuinely different process:
// a futex on a MAP_SHARED page is the only reason this works, and it is
// exactly what FUTEX_PRIVATE_FLAG would silently break.
void test_cross_process_wake() {
    std::printf("\n== a write from another process wakes a parked reader ==\n");

    const char* name = "wakeup_xproc";
    ESHMConfig cfg = eshm_default_config(name);
    cfg.role = ESHM_ROLE_MASTER;
    ESHMHandle* master = eshm_init(&cfg);
    if (!master) { check(false, "master init"); return; }

    char buf[64];
    size_t n = 0;
    eshm_read_ex(master, buf, sizeof(buf), &n, 0);    // baseline before forking

    pid_t pid = fork();
    if (pid == 0) {
        // Child: attach as slave, wait for the parent to be parked, then write.
        ESHMConfig scfg = eshm_default_config(name);
        scfg.role = ESHM_ROLE_SLAVE;
        scfg.auto_cleanup = false;
        ESHMHandle* slave = eshm_init(&scfg);
        if (!slave) _exit(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        eshm_write(slave, "wake", 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        eshm_destroy(slave);
        _exit(0);
    }

    auto t0 = std::chrono::steady_clock::now();
    int rc = eshm_read_ex(master, buf, sizeof(buf), &n, ESHM_TIMEOUT_INFINITE);
    double elapsed = ms_since(t0);

    int status = 0;
    waitpid(pid, &status, 0);

    std::printf("       woke after %.1f ms with \"%s\"\n",
                elapsed, rc == ESHM_SUCCESS ? buf : "");
    check(rc == ESHM_SUCCESS, "ESHM_TIMEOUT_INFINITE returned on the write");
    check(n == 5 && std::strcmp(buf, "wake") == 0, "payload arrived intact");
    // The child writes at 150 ms. Returning much earlier would mean the read
    // never waited; much later would mean the wake was missed and we only
    // escaped via the internal re-check.
    check(elapsed > 100.0, "the read really did wait");
    check(elapsed < 250.0, "the write woke it promptly, not via the re-check timer");

    eshm_destroy(master);
}

// ---------------------------------------------------------------------------
// Switching mode while another thread is blocked in a read must not hang or
// crash - the parked reader picks the change up on its next wait.
void test_mode_switch_while_reading() {
    std::printf("\n== switching mode under a blocked reader ==\n");

    ESHMConfig cfg = eshm_default_config("wakeup_switch");
    cfg.role = ESHM_ROLE_MASTER;
    ESHMHandle* h = eshm_init(&cfg);
    if (!h) { check(false, "master init"); return; }

    char buf[64];
    size_t n = 0;
    eshm_read_ex(h, buf, sizeof(buf), &n, 0);

    std::atomic<bool> done{false};
    std::atomic<int> rc{0};
    std::thread reader([&] {
        size_t got = 0;
        char local[64];
        rc = eshm_read_ex(h, local, sizeof(local), &got, 400);
        done = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    eshm_set_wakeup_mode(h, ESHM_WAKEUP_POLL);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    eshm_set_wakeup_mode(h, ESHM_WAKEUP_PUSH);

    reader.join();
    check(done.load(), "the blocked reader returned");
    check(rc.load() == ESHM_ERROR_TIMEOUT, "it returned its timeout, not an error");

    eshm_destroy(h);
}

// ---------------------------------------------------------------------------
// A writer killed between seqlock_write_begin() and seqlock_write_end() leaves
// the sequence number odd forever. The reader used to spin on that until its
// own process was killed - the one failure mode this library exists to
// survive. It must now give up and let the caller's timeout apply.
void test_torn_write_does_not_hang() {
    std::printf("\n== a writer that died mid-write does not hang the reader ==\n");

    ESHMConfig cfg = eshm_default_config("wakeup_torn");
    cfg.role = ESHM_ROLE_MASTER;
    cfg.use_threads = false;
    ESHMHandle* h = eshm_init(&cfg);
    if (!h) { check(false, "master init"); return; }

    char buf[64];
    size_t n = 0;
    eshm_read_ex(h, buf, sizeof(buf), &n, 0);      // baseline

    // Reach into the segment and stage what a half-finished write looks like.
    int fd = shm_open("/eshm_wakeup_torn", O_RDWR, 0666);
    if (fd < 0) { check(false, "reopen segment"); eshm_destroy(h); return; }
    ESHMData* d = (ESHMData*)mmap(NULL, sizeof(ESHMData), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, 0);
    if (d == MAP_FAILED) { check(false, "map segment"); close(fd); eshm_destroy(h); return; }

    ESHMChannel* ch = &d->slave_to_master;   // the channel a master reads
    ch->data_size = 4;
    ch->seqlock.sequence = 2;                // even: a completed write
    ch->write_count = 1;
    eshm_read_ex(h, buf, sizeof(buf), &n, 100);   // move last_read off zero

    ch->write_count = 2;
    ch->seqlock.sequence = 3;                // odd: writer died mid-write

    auto t0 = std::chrono::steady_clock::now();
    int rc = eshm_read_ex(h, buf, sizeof(buf), &n, 200);
    double elapsed = ms_since(t0);

    std::printf("       read returned rc=%d after %.1f ms\n", rc, elapsed);
    check(rc != ESHM_SUCCESS, "a torn snapshot is not reported as success");
    check(elapsed < 2000.0, "the read returned instead of spinning forever");

    munmap(d, sizeof(ESHMData));
    close(fd);
    eshm_destroy(h);
}

} // namespace

int main() {
    std::printf("=== ESHM push wakeup tests ===\n");

    test_layout();
    test_timeout_contract();
    test_mode_accessors();
    test_push_actually_parks();
    test_cross_process_wake();
    test_mode_switch_while_reading();
    test_torn_write_does_not_hang();

    std::printf("\n=== %s ===\n", g_failures == 0 ? "all wakeup tests passed"
                                                  : "WAKEUP TESTS FAILED");
    if (g_failures) std::printf("%d check(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
