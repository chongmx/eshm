// Worker side of the trigger example.
//
// Attaches to both channels, registers handlers, and does nothing until the
// master triggers it. The "process" handler reads the data channel when it
// runs - the trigger itself carries no values.
//
// Demonstrates: eshm_rpc_create as SLAVE, on_call / on_event, and a handler
//               that reads shared state on demand.
//
// Run:   ./trigger_worker [channel]      (start the master first)
// Pairs with ./trigger_master or `python3 peer.py master <channel>`.

#include <eshm.h>
#include <eshm_rpc.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }

struct Reading {
    uint64_t sample;
    double   temperature;
    char     label[32];
};

struct Context {
    ESHMHandle* data = nullptr;
    EshmRpc*    rpc  = nullptr;
    int processed = 0;
};

// Runs on the dispatcher thread when the master calls "process".
void on_process(void* user) {
    Context* ctx = static_cast<Context*>(user);

    // The trigger said "go look" - so look. Non-blocking: the data is already
    // there, because the master wrote it before firing.
    Reading reading;
    size_t received = 0;
    const int rc = eshm_read_ex(ctx->data, &reading, sizeof(reading), &received, 0);

    if (rc == ESHM_SUCCESS && received == sizeof(reading)) {
        ++ctx->processed;
        std::printf("<- [call ] process: sample %llu, %.1f C, label \"%s\"\n",
                    static_cast<unsigned long long>(reading.sample),
                    reading.temperature, reading.label);
    } else {
        // Possible if the master fires faster than this side drains: the
        // channel holds one value, so a burst coalesces. Level-triggered
        // handlers tolerate that by design.
        std::printf("<- [call ] process: nothing new on the data channel\n");
    }
    std::fflush(stdout);

    // Triggers can go the other way from inside a handler.
    eshm_rpc_call(ctx->rpc, "done");
}

void on_shutting_down(void* user) {
    Context* ctx = static_cast<Context*>(user);
    std::printf("<- [event] master is shutting down after %d round(s)\n", ctx->processed);
    std::fflush(stdout);
    g_running = 0;
}

} // namespace

int main(int argc, char** argv) {
    const char* channel = (argc > 1) ? argv[1] : "demo";

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    // Attach to the data channel; the master owns it, so retry until it exists.
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;
    config.max_reconnect_attempts = 0;
    config.reconnect_wait_ms = 0;

    ESHMHandle* data = nullptr;
    for (int i = 0; g_running && i < 100 && !data; ++i) {
        data = eshm_init(&config);
        if (!data) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!data) {
        std::fprintf(stderr, "worker: no data channel '%s' - is the master running?\n",
                     channel);
        return 1;
    }

    // eshm_rpc_create retries for a slave too.
    EshmRpc* rpc = eshm_rpc_create(channel, ESHM_ROLE_SLAVE);
    if (!rpc) {
        std::fprintf(stderr, "worker: %s\n", eshm_rpc_get_last_error());
        eshm_destroy(data);
        return 1;
    }

    Context ctx;
    ctx.data = data;
    ctx.rpc = rpc;

    eshm_rpc_on_call (rpc, "process",        on_process,        &ctx);
    eshm_rpc_on_event(rpc, "shutting_down",  on_shutting_down,  &ctx);
    eshm_rpc_start(rpc);

    std::printf("worker: attached to '%s' and '%s_ctl', waiting for triggers\n",
                channel, channel);
    std::fflush(stdout);

    // Prime the data channel BEFORE announcing readiness.
    //
    // A reader only sees writes made after its first read: the read path
    // baselines the channel's write counter on the first call. Without this
    // throwaway read, the first trigger would arrive, the handler would do its
    // first read, and that read would baseline instead of returning the master's
    // very first sample - which then looks like a lost message. Doing it here,
    // before the master is told we exist, closes that window entirely.
    {
        Reading discard;
        size_t ignored = 0;
        eshm_read_ex(data, &discard, sizeof(discard), &ignored, 0);
    }

    // Announce ourselves, then idle. The dispatcher thread does the work; this
    // thread parks. With push wakeup an idle dispatcher costs no CPU.
    eshm_rpc_emit(rpc, "worker_ready");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::printf("\nworker: processed %d round(s), %llu trigger(s) coalesced away\n",
                ctx.processed,
                static_cast<unsigned long long>(eshm_rpc_missed(rpc)));

    eshm_rpc_destroy(rpc);
    eshm_destroy(data);
    return ctx.processed > 0 ? 0 : 1;
}
