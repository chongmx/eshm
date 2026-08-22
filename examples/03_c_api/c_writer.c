/*
 * Writer, written in plain C. Encodes a record and writes it in one call.
 *
 * shm_protocol::DataHandler is a C++ class whose interface uses std::string,
 * std::vector and std::variant, so it cannot be called from C, from ctypes, or
 * safely across compiler and standard-library versions. eshm_write_data() is
 * the ABI-stable equivalent: types, keys and value pointers in, DER on the
 * channel out - and it is exactly what the Python bindings call.
 *
 * Demonstrates: eshm_write_data, the value representation each DataType
 *               expects, eshm_data_get_last_error.
 *
 * Build: cc c_writer.c -o c_writer -leshm -lpthread -lrt      (or use CMake)
 * Run:   ./c_writer [channel] [count]
 * Pairs with ./c_reader or `python3 peer.py read <channel>`.
 */

#include <eshm.h>
#include <eshm_data_api.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* DataType values from data_handler.h - stable, part of the wire format. */
#define DT_INTEGER 0
#define DT_BOOLEAN 1
#define DT_REAL    2
#define DT_STRING  3
#define DT_BINARY  4

/* BINARY values are passed as a pointer to this pair. */
struct binary_value {
    const uint8_t* data;
    size_t len;
};

static volatile sig_atomic_t g_running = 1;
static void stop(int sig) { (void)sig; g_running = 0; }

int main(int argc, char** argv) {
    const char* channel = (argc > 1) ? argv[1] : "c_api";
    long count = (argc > 2) ? atol(argv[2]) : 0;      /* 0 = until Ctrl-C */
    int i;

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_MASTER;

    ESHMHandle* handle = eshm_init(&config);
    if (!handle) {
        fprintf(stderr, "c_writer: could not create channel '%s'\n", channel);
        return 1;
    }
    printf("c_writer: channel '%s' is live, waiting for a reader...\n", channel);
    fflush(stdout);

    /* Wait for a peer: a reader only sees writes made after its first read.
     *
     * eshm_check_remote_alive() answers "has the peer been detected as stale",
     * which is false before anyone has ever attached - so it reports alive on
     * an empty channel. slave_alive in the stats is the flag that means a peer
     * is actually there. */
    for (i = 0; g_running && i < 150; ++i) {
        ESHMStats stats;
        if (eshm_get_stats(handle, &stats) == ESHM_SUCCESS && stats.slave_alive) break;
        usleep(100 * 1000);
    }
    printf("c_writer: reader attached (Ctrl-C to stop)\n");

    for (long n = 0; g_running && (count == 0 || n < count); ++n) {
        /* One record, five types. Every value is passed by address; the type
         * tag tells eshm_write_data() how to read what the pointer points at.
         *
         *   INTEGER -> int64_t*   BOOLEAN -> bool*    REAL -> double*
         *   STRING  -> char*      BINARY  -> struct binary_value*
         */
        int64_t counter = n;
        double  temperature = 20.0 + (double)(n % 10) * 0.5;
        bool    enabled = (n % 2) == 0;
        uint8_t raw[5] = { (uint8_t)(n & 0xff), 0xde, 0xad, 0xbe, 0xef };
        struct binary_value checksum = { raw, sizeof(raw) };

        const uint8_t types[] = { DT_INTEGER, DT_REAL, DT_BOOLEAN, DT_STRING, DT_BINARY };
        const char*   keys[]  = { "counter", "temperature", "enabled", "source", "checksum" };
        const void*   values[] = { &counter, &temperature, &enabled, "C writer", &checksum };

        /* Encode and write in one crossing of the boundary. */
        int written = eshm_write_data(handle, types, keys, values, 5);
        if (written < 0) {
            fprintf(stderr, "c_writer: write failed: %s\n", eshm_data_get_last_error());
            break;
        }

        if (n % 10 == 0) {
            printf("-> #%ld temperature=%.2f enabled=%s (%d bytes on the wire)\n",
                   n, temperature, enabled ? "true" : "false", written);
            fflush(stdout);
        }

        usleep(10 * 1000);
    }

    printf("\nc_writer: closing channel\n");
    eshm_destroy(handle);
    return 0;
}
