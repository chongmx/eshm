// Master side of the trigger example.
//
// Owns two channels:
//   "demo"      the data channel - a plain ESHM channel holding the values
//   "demo_ctl"  the control channel, opened for us by eshm_rpc_create("demo")
//
// The pattern this exists to show:
//
//     write the data  ->  fire the trigger  ->  peer's handler reads the data
//
// A trigger carries a name and nothing else. That is deliberate: it makes the
// handler level-triggered, so it always acts on current state, and two
// triggers that coalesce into one delivery are correct rather than lost.
//
// Demonstrates: eshm_rpc_create / on_call / on_event / start / call / emit,
//               and why the control channel is separate from the data channel.
//
// Run:   ./trigger_master [channel] [rounds]
// Pairs with ./trigger_worker or `python3 peer.py worker <channel>`.

#include <eshm.h>
#include <eshm_rpc.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }

// What the two sides exchange. Plain struct in the data channel - the trigger
// layer never sees it, and does not need to.
struct Reading {
    uint64_t sample;
    double   temperature;
    char     label[32];
};

struct Context {
    ESHMHandle* data = nullptr;
    int replies = 0;
    bool worker_ready = false;
};

// --- handlers: these run on the dispatcher thread, not main -----------------

void on_worker_ready(void* user) {
    Context* ctx = static_cast<Context*>(user);
    ctx->worker_ready = true;
    std::printf("   [event] worker says it is ready\n");
    std::fflush(stdout);
}

void on_done(void* user) {
    Context* ctx = static_cast<Context*>(user);
    ++ctx->replies;
    std::printf("   [call ] worker finished a round (%d so far)\n", ctx->replies);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    const char* channel = (argc > 1) ? argv[1] : "demo";
    const int   rounds  = (argc > 2) ? std::atoi(argv[2]) : 5;

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    // 1. The data channel: an ordinary ESHM channel, nothing to do with RPC.
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_MASTER;
    ESHMHandle* data = eshm_init(&config);
    if (!data) {
        std::fprintf(stderr, "master: could not create data channel '%s'\n", channel);
        return 1;
    }

    // 2. The control channel: eshm_rpc_create appends "_ctl" itself, so the
    //    dispatcher can never fight the data channel for reads.
    EshmRpc* rpc = eshm_rpc_create(channel, ESHM_ROLE_MASTER);
    if (!rpc) {
        std::fprintf(stderr, "master: %s\n", eshm_rpc_get_last_error());
        eshm_destroy(data);
        return 1;
    }

    Context ctx;
    ctx.data = data;

    eshm_rpc_on_event(rpc, "worker_ready", on_worker_ready, &ctx);
    eshm_rpc_on_call (rpc, "done",         on_done,         &ctx);
    eshm_rpc_start(rpc);

    std::printf("master: data on '%s', triggers on '%s_ctl'\n", channel, channel);
    std::printf("master: waiting for a worker...\n");
    std::fflush(stdout);

    for (int i = 0; g_running && !ctx.worker_ready && i < 150; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!ctx.worker_ready) {
        std::fprintf(stderr, "master: no worker appeared\n");
        eshm_rpc_destroy(rpc);
        eshm_destroy(data);
        return 1;
    }

    for (int i = 0; g_running && i < rounds; ++i) {
        // Write the data FIRST, then trigger. The handler on the other side
        // reads whatever is current when it runs - which is why the ordering
        // matters and why no arguments are needed.
        Reading reading;
        reading.sample = static_cast<uint64_t>(i);
        reading.temperature = 20.0 + (i % 10) * 0.5;
        std::snprintf(reading.label, sizeof(reading.label), "round-%d", i);

        eshm_write(data, &reading, sizeof(reading));
        std::printf("-> wrote sample %d (%.1f C), calling 'process'\n",
                    i, reading.temperature);
        std::fflush(stdout);

        eshm_rpc_call(rpc, "process");

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // An event, not a call: the worker may or may not care, and we do not
    // want an error logged if nobody is listening.
    eshm_rpc_emit(rpc, "shutting_down");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::printf("\nmaster: %d round(s) acknowledged, %llu trigger(s) dispatched here,"
                " %llu coalesced away\n",
                ctx.replies,
                static_cast<unsigned long long>(eshm_rpc_dispatched(rpc)),
                static_cast<unsigned long long>(eshm_rpc_missed(rpc)));

    eshm_rpc_destroy(rpc);      // stops the dispatcher, then closes the channel
    eshm_destroy(data);
    return ctx.replies > 0 ? 0 : 1;
}
