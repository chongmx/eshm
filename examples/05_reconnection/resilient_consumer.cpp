// Reconnection: what a slave does when the master dies and comes back.
//
// Every knob in ESHMConfig that governs recovery is a command-line flag here,
// so the policies can be compared against the same misbehaving publisher.
//
// Demonstrates: max_reconnect_attempts, reconnect_wait_ms,
//               reconnect_retry_interval_ms, stale_threshold_ms,
//               disconnect_behavior (IMMEDIATELY / ON_TIMEOUT / NEVER),
//               ESHM_ROLE_AUTO negotiation, ESHM_ERROR_MASTER_STALE.
//
// Run:   ./resilient_consumer [channel] [options]
// Pairs with ./flaky_publisher or `python3 peer.py publish <channel>`.

#include <eshm.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {
volatile sig_atomic_t g_running = 1;
void stop(int) { g_running = 0; }

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s [channel] [options]\n\n"
        "  --attempts N      max_reconnect_attempts   (0 = unlimited, default 50)\n"
        "  --wait MS         reconnect_wait_ms        (0 = unlimited, default 5000)\n"
        "  --interval MS     reconnect_retry_interval_ms       (default 100)\n"
        "  --stale MS        stale_threshold_ms                (default 100)\n"
        "  --behavior WHICH  immediately | on-timeout | never  (default on-timeout)\n"
        "  --auto            join with ESHM_ROLE_AUTO instead of SLAVE\n\n"
        "Presets worth comparing against the same publisher:\n"
        "  %s demo                                  # give up after 50 tries (~5 s)\n"
        "  %s demo --attempts 0 --wait 0            # never give up\n"
        "  %s demo --behavior immediately           # fail the read the moment it goes stale\n"
        "  %s demo --behavior never --attempts 0 --wait 0   # wait forever, never error\n",
        argv0, argv0, argv0, argv0, argv0);
}

const char* behavior_name(ESHMDisconnectBehavior b) {
    switch (b) {
        case ESHM_DISCONNECT_IMMEDIATELY: return "IMMEDIATELY";
        case ESHM_DISCONNECT_ON_TIMEOUT:  return "ON_TIMEOUT";
        case ESHM_DISCONNECT_NEVER:       return "NEVER";
    }
    return "?";
}

const char* role_name(ESHMRole role) {
    switch (role) {
        case ESHM_ROLE_MASTER: return "MASTER";
        case ESHM_ROLE_SLAVE:  return "SLAVE";
        case ESHM_ROLE_AUTO:   return "AUTO";
    }
    return "?";
}
}

int main(int argc, char** argv) {
    const char* channel = "reconnect";
    bool use_auto_role = false;

    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;          // the master owns the segment

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const char* next = (i + 1 < argc) ? argv[i + 1] : nullptr;

        if (arg == "-h" || arg == "--help")            { usage(argv[0]); return 0; }
        else if (arg == "--auto")                      { use_auto_role = true; }
        else if (arg == "--attempts" && next)          { config.max_reconnect_attempts = std::strtoul(argv[++i], nullptr, 10); }
        else if (arg == "--wait" && next)              { config.reconnect_wait_ms = std::strtoul(argv[++i], nullptr, 10); }
        else if (arg == "--interval" && next)          { config.reconnect_retry_interval_ms = std::strtoul(argv[++i], nullptr, 10); }
        else if (arg == "--stale" && next)             { config.stale_threshold_ms = std::strtoul(argv[++i], nullptr, 10); }
        else if (arg == "--behavior" && next) {
            const std::string which = argv[++i];
            if (which == "immediately")     config.disconnect_behavior = ESHM_DISCONNECT_IMMEDIATELY;
            else if (which == "on-timeout") config.disconnect_behavior = ESHM_DISCONNECT_ON_TIMEOUT;
            else if (which == "never")      config.disconnect_behavior = ESHM_DISCONNECT_NEVER;
            else { std::fprintf(stderr, "unknown --behavior '%s'\n", which.c_str()); return 2; }
        }
        else if (!arg.empty() && arg[0] != '-')        { channel = argv[i]; }
        else                                           { usage(argv[0]); return 2; }
    }

    config.shm_name = channel;
    // ESHM_ROLE_AUTO takes whichever role is free: master if the segment does
    // not exist yet, slave if it does. Useful when start order is not fixed -
    // but then either side may end up owning the segment, so leave cleanup off.
    if (use_auto_role) config.role = ESHM_ROLE_AUTO;

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    std::printf("consumer: channel '%s'\n", channel);
    std::printf("  requested role  %s\n", role_name(config.role));
    std::printf("  attempts        %u%s\n", config.max_reconnect_attempts,
                config.max_reconnect_attempts == 0 ? "  (unlimited)" : "");
    std::printf("  wait            %u ms%s\n", config.reconnect_wait_ms,
                config.reconnect_wait_ms == 0 ? "  (unlimited)" : "");
    std::printf("  retry interval  %u ms\n", config.reconnect_retry_interval_ms);
    std::printf("  stale threshold %u ms\n", config.stale_threshold_ms);
    std::printf("  on stale peer   %s\n\n", behavior_name(config.disconnect_behavior));

    // A slave cannot attach before the master exists; AUTO always succeeds.
    ESHMHandle* handle = nullptr;
    for (int i = 0; g_running && i < 100 && !handle; ++i) {
        handle = eshm_init(&config);
        if (!handle) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!handle) {
        std::fprintf(stderr, "consumer: could not join '%s' - is a publisher running?\n", channel);
        return 1;
    }

    ESHMRole actual = ESHM_ROLE_AUTO;
    eshm_get_role(handle, &actual);
    std::printf("consumer: joined as %s (Ctrl-C to stop)\n\n", role_name(actual));

    long messages = 0, timeouts = 0, stale_reports = 0, outages = 0;
    bool connected = false;

    while (g_running) {
        char buffer[ESHM_MAX_DATA_SIZE];
        size_t received = 0;
        const int rc = eshm_read_ex(handle, buffer, sizeof(buffer), &received, 500);

        if (rc == ESHM_SUCCESS) {
            if (!connected) {
                std::printf("[up]   publisher is back (outage %ld over)\n", outages);
                connected = true;
            }
            ++messages;
            if (messages % 10 == 1) {
                std::printf("<- %.*s\n", static_cast<int>(received), buffer);
                std::fflush(stdout);
            }
            continue;
        }

        // ESHM_ERROR_MASTER_STALE only ever surfaces under
        // ESHM_DISCONNECT_IMMEDIATELY. The other two behaviours report a
        // plain timeout while the library retries underneath.
        if (rc == ESHM_ERROR_MASTER_STALE) {
            ++stale_reports;
            if (connected || stale_reports == 1) {
                std::printf("[down] master stale: %s\n", eshm_error_string(rc));
                std::fflush(stdout);
                if (connected) { connected = false; ++outages; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (rc == ESHM_ERROR_TIMEOUT || rc == ESHM_ERROR_NO_DATA) {
            ++timeouts;
            bool alive = false;
            eshm_check_remote_alive(handle, &alive);
            if (connected && !alive) {
                std::printf("[down] publisher went away - reconnecting in the background\n");
                std::fflush(stdout);
                connected = false;
                ++outages;
            }

            // Sleep, do not spin. While the library is detached and retrying,
            // eshm_read_ex() returns TIMEOUT *immediately* instead of waiting
            // out timeout_ms - there is no segment to wait on. The obvious
            // `continue` loop therefore burns a full core for the whole
            // outage. Any small sleep on the not-connected path fixes it.
            if (!alive) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        // Anything else means reconnection gave up: the attempt or wait
        // budget ran out. This is the difference the --attempts/--wait flags
        // make visible.
        std::printf("[end]  read failed: %s\n", eshm_error_string(rc));
        break;
    }

    std::printf("\nconsumer: %ld message(s), %ld outage(s), %ld timeout(s), %ld stale report(s)\n",
                messages, outages, timeouts, stale_reports);
    eshm_destroy(handle);
    return 0;
}
