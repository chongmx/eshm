// Named triggers: registration, dispatch, call vs event semantics, and the
// read-baseline behaviour the trigger pattern depends on.

#include <eshm.h>
#include <eshm_rpc.h>

#include <sys/wait.h>
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

void bump(void* user) {
    static_cast<std::atomic<int>*>(user)->fetch_add(1);
}

// Wait for a counter to reach a value, so the tests never depend on a fixed
// sleep being long enough on a loaded machine.
bool wait_for(std::atomic<int>& counter, int target, int timeout_ms = 3000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (counter.load() >= target) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// ---------------------------------------------------------------------------
void test_argument_validation() {
    std::printf("\n== argument validation ==\n");

    check(eshm_rpc_create(nullptr, ESHM_ROLE_MASTER) == nullptr,
          "create with a NULL channel is rejected");

    EshmRpc* rpc = eshm_rpc_create("rpc_args", ESHM_ROLE_MASTER);
    if (!rpc) { check(false, "master create"); return; }

    std::atomic<int> hits{0};
    check(eshm_rpc_on_call(rpc, nullptr, bump, &hits) == ESHM_ERROR_INVALID_PARAM,
          "on_call with a NULL name is rejected");
    check(eshm_rpc_on_call(rpc, "x", nullptr, &hits) == ESHM_ERROR_INVALID_PARAM,
          "on_call with a NULL handler is rejected");
    check(eshm_rpc_on_event(nullptr, "x", bump, &hits) == ESHM_ERROR_INVALID_PARAM,
          "on_event with a NULL rpc is rejected");
    check(eshm_rpc_call(rpc, nullptr) == ESHM_ERROR_INVALID_PARAM,
          "call with a NULL name is rejected");

    // start/stop are documented as idempotent.
    check(eshm_rpc_start(rpc) == ESHM_SUCCESS && eshm_rpc_start(rpc) == ESHM_SUCCESS,
          "start is idempotent");
    check(eshm_rpc_stop(rpc) == ESHM_SUCCESS && eshm_rpc_stop(rpc) == ESHM_SUCCESS,
          "stop is idempotent");

    eshm_rpc_destroy(rpc);
}

// ---------------------------------------------------------------------------
// The real thing: two processes, triggers in both directions.
void test_cross_process_triggers() {
    std::printf("\n== triggers between two processes ==\n");

    const char* channel = "rpc_xproc";

    EshmRpc* master = eshm_rpc_create(channel, ESHM_ROLE_MASTER);
    if (!master) { check(false, "master create"); return; }

    std::atomic<int> acks{0};
    std::atomic<int> notices{0};
    eshm_rpc_on_call (master, "ack",    bump, &acks);
    eshm_rpc_on_event(master, "notice", bump, &notices);
    eshm_rpc_start(master);

    pid_t pid = fork();
    if (pid == 0) {
        // Child: attach, answer each "work" with an "ack", then emit a notice.
        EshmRpc* worker = eshm_rpc_create(channel, ESHM_ROLE_SLAVE);
        if (!worker) _exit(1);

        struct Ctx { EshmRpc* rpc; std::atomic<int> seen{0}; } ctx{worker};
        eshm_rpc_on_call(worker, "work", [](void* u) {
            Ctx* c = static_cast<Ctx*>(u);
            c->seen.fetch_add(1);
            eshm_rpc_call(c->rpc, "ack");
        }, &ctx);
        eshm_rpc_start(worker);

        // Give the parent time to send, then say goodbye and exit.
        for (int i = 0; i < 300 && ctx.seen.load() < 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        eshm_rpc_emit(worker, "notice");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        eshm_rpc_destroy(worker);
        _exit(0);
    }

    // Let the child attach before firing anything at it.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    for (int i = 0; i < 3; ++i) {
        check(eshm_rpc_call(master, "work") == ESHM_SUCCESS, "call sent");
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    const bool got_acks = wait_for(acks, 3);
    const bool got_notice = wait_for(notices, 1);

    int status = 0;
    waitpid(pid, &status, 0);

    std::printf("       %d ack(s), %d notice(s), %llu dispatched, %llu coalesced\n",
                acks.load(), notices.load(),
                static_cast<unsigned long long>(eshm_rpc_dispatched(master)),
                static_cast<unsigned long long>(eshm_rpc_missed(master)));

    check(got_acks, "every call was answered by the peer");
    check(got_notice, "the peer's event arrived");
    check(eshm_rpc_dispatched(master) >= 4, "dispatched counts handlers actually run");

    eshm_rpc_destroy(master);
}

// ---------------------------------------------------------------------------
// call: exactly one handler, replaced on re-registration.
// event: any number, all run.
void test_call_vs_event_semantics() {
    std::printf("\n== call takes one handler, event takes many ==\n");

    const char* channel = "rpc_semantics";
    EshmRpc* a = eshm_rpc_create(channel, ESHM_ROLE_MASTER);
    if (!a) { check(false, "master create"); return; }
    EshmRpc* b = eshm_rpc_create(channel, ESHM_ROLE_SLAVE);
    if (!b) { check(false, "slave create"); eshm_rpc_destroy(a); return; }

    std::atomic<int> first{0}, second{0}, ev1{0}, ev2{0};

    // Two registrations of the same call name: the second must win outright.
    eshm_rpc_on_call(b, "once", bump, &first);
    eshm_rpc_on_call(b, "once", bump, &second);

    // Two registrations of the same event name: both must run.
    eshm_rpc_on_event(b, "both", bump, &ev1);
    eshm_rpc_on_event(b, "both", bump, &ev2);

    eshm_rpc_start(b);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Wait for each trigger to land before firing the next.
    //
    // This is not politeness, it is required for *distinct* triggers: the
    // channel holds one value per direction, so firing "once" and "both"
    // back to back would let the second overwrite the first before the
    // dispatcher read it, and the first name would never be seen. Repeated
    // firings of the *same* name coalescing is harmless - that is the
    // level-triggered contract - but two different names are two different
    // signals, and nothing makes them queue.
    eshm_rpc_call(a, "once");
    check(wait_for(second, 1), "the call was delivered before the next trigger");

    eshm_rpc_emit(a, "both");
    check(wait_for(ev2, 1), "the event was delivered before the next trigger");

    // An unknown name: an error for a call, silently ignored for an event.
    // Neither may crash the dispatcher, which the later checks prove.
    eshm_rpc_emit(a, "nobody_listening");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    check(first.load() == 0, "a replaced call handler no longer runs");
    check(second.load() == 1, "the newest call handler runs, exactly once");
    check(ev1.load() == 1 && ev2.load() == 1, "every event handler runs");

    // The dispatcher survived the unknown event and is still working.
    std::atomic<int> after{0};
    eshm_rpc_on_call(b, "still_alive", bump, &after);
    eshm_rpc_call(a, "still_alive");
    check(wait_for(after, 1), "the dispatcher still works after an unmatched trigger");

    // Pin the coalescing property: a burst of the same trigger arrives fewer
    // times than it was fired, and the dispatcher reports the shortfall rather
    // than hiding it. Handlers must therefore be idempotent - never counters.
    std::atomic<int> burst{0};
    eshm_rpc_on_call(b, "burst", bump, &burst);
    for (int i = 0; i < 200; ++i) eshm_rpc_call(a, "burst");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const int seen = burst.load();
    const uint64_t missed = eshm_rpc_missed(b);
    std::printf("       fired 200, handled %d, reported missed %llu\n",
                seen, static_cast<unsigned long long>(missed));
    check(seen >= 1, "a burst still delivers at least the latest trigger");
    check(seen + static_cast<int>(missed) >= 200,
          "handled + missed accounts for everything fired");

    eshm_rpc_destroy(b);
    eshm_rpc_destroy(a);
}

// ---------------------------------------------------------------------------
// The trigger pattern is "write the data, then fire" - which only works if a
// reader that primed itself before the first write actually receives it.
void test_first_message_is_delivered() {
    std::printf("\n== a primed reader receives the very first write ==\n");

    const char* name = "rpc_baseline";
    ESHMConfig mc = eshm_default_config(name);
    mc.role = ESHM_ROLE_MASTER;
    ESHMHandle* master = eshm_init(&mc);
    if (!master) { check(false, "master init"); return; }

    ESHMConfig sc = eshm_default_config(name);
    sc.role = ESHM_ROLE_SLAVE;
    sc.auto_cleanup = false;
    ESHMHandle* slave = eshm_init(&sc);
    if (!slave) { check(false, "slave init"); eshm_destroy(master); return; }

    // Prime: take the read baseline while the channel is still empty.
    char buf[64];
    size_t n = 0;
    eshm_read_ex(slave, buf, sizeof(buf), &n, 0);

    eshm_write(master, "first", 6);

    const int rc = eshm_read_ex(slave, buf, sizeof(buf), &n, 500);
    check(rc == ESHM_SUCCESS, "the first write after priming is delivered");
    check(n == 6 && std::strcmp(buf, "first") == 0, "and its payload is intact");

    eshm_destroy(slave);
    eshm_destroy(master);
}

} // namespace

int main() {
    std::printf("=== ESHM trigger (RPC) tests ===\n");

    test_argument_validation();
    test_cross_process_triggers();
    test_call_vs_event_semantics();
    test_first_message_is_delivered();

    std::printf("\n=== %s ===\n", g_failures == 0 ? "all trigger tests passed"
                                                  : "TRIGGER TESTS FAILED");
    if (g_failures) std::printf("%d check(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
