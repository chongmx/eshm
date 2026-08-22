/*
 * Reader, written in plain C. Reads and decodes a record in one call.
 *
 * eshm_read_data() does the DER work in C++ and hands back plain C values, so
 * the decode never crosses the FFI boundary field by field. Every decoded
 * value is heap allocated - release each one with eshm_free_value().
 *
 * Demonstrates: eshm_read_data, the out-parameter layout it expects,
 *               eshm_free_value, and the ownership rule for decoded values.
 *
 * Build: cc c_reader.c -o c_reader -leshm -lpthread -lrt      (or use CMake)
 * Run:   ./c_reader [channel] [count]        (start a writer first)
 * Pairs with ./c_writer or `python3 peer.py write <channel>`.
 */

#include <eshm.h>
#include <eshm_data_api.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DT_INTEGER 0
#define DT_BOOLEAN 1
#define DT_REAL    2
#define DT_STRING  3
#define DT_BINARY  4

#define MAX_ITEMS   16
#define MAX_KEY_LEN 64

struct binary_value {
    uint8_t* data;
    size_t len;
};

static volatile sig_atomic_t g_running = 1;
static void stop(int sig) { (void)sig; g_running = 0; }

static void print_value(const char* key, uint8_t type, void* value) {
    switch (type) {
    case DT_INTEGER:
        printf("  %-12s INTEGER  %lld\n", key, (long long)*(int64_t*)value);
        break;
    case DT_BOOLEAN:
        printf("  %-12s BOOLEAN  %s\n", key, *(bool*)value ? "true" : "false");
        break;
    case DT_REAL:
        printf("  %-12s REAL     %.2f\n", key, *(double*)value);
        break;
    case DT_STRING:
        printf("  %-12s STRING   \"%s\"\n", key, (const char*)value);
        break;
    case DT_BINARY: {
        struct binary_value* bin = (struct binary_value*)value;
        printf("  %-12s BINARY   ", key);
        for (size_t i = 0; i < bin->len; ++i) printf("%02x", bin->data[i]);
        printf(" (%zu bytes)\n", bin->len);
        break;
    }
    default:
        printf("  %-12s type %u (not shown)\n", key, type);
        break;
    }
}

int main(int argc, char** argv) {
    const char* channel = (argc > 1) ? argv[1] : "c_api";
    long count = (argc > 2) ? atol(argv[2]) : 0;
    int i;

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    /* A slave cannot attach before the master has created the segment. */
    ESHMConfig config = eshm_default_config(channel);
    config.role = ESHM_ROLE_SLAVE;
    config.auto_cleanup = false;
    config.max_reconnect_attempts = 0;
    config.reconnect_wait_ms = 0;

    ESHMHandle* handle = NULL;
    for (i = 0; i < 50 && !handle; ++i) {
        handle = eshm_init(&config);
        if (!handle) usleep(100 * 1000);
    }
    if (!handle) {
        fprintf(stderr, "c_reader: no channel '%s' - is the writer running?\n", channel);
        return 1;
    }
    printf("c_reader: attached to '%s' (Ctrl-C to stop)\n", channel);

    /* eshm_read_data() fills caller-owned arrays. The key buffers must exist
     * before the call; the value pointers are allocated by the library. */
    uint8_t types[MAX_ITEMS];
    char    key_storage[MAX_ITEMS][MAX_KEY_LEN];
    char*   keys[MAX_ITEMS];
    void*   values[MAX_ITEMS];
    for (i = 0; i < MAX_ITEMS; ++i) keys[i] = key_storage[i];

    long records = 0;
    long errors = 0;
    int seen_writer = 0;

    while (g_running && (count == 0 || records < count)) {
        int item_count = 0;
        int rc = eshm_read_data(handle, types, keys, MAX_KEY_LEN,
                                values, MAX_ITEMS, &item_count, 200);

        if (rc == ESHM_ERROR_TIMEOUT || rc == ESHM_ERROR_NO_DATA) {
            bool alive = false;
            eshm_check_remote_alive(handle, &alive);
            if (seen_writer && !alive) {
                printf("c_reader: writer went away\n");
                break;
            }
            continue;
        }
        if (rc != ESHM_SUCCESS) {
            fprintf(stderr, "c_reader: read failed: %s\n", eshm_error_string(rc));
            ++errors;
            continue;
        }
        seen_writer = 1;
        ++records;

        if (records % 10 == 1) {
            printf("<- record %ld (%d items)\n", records, item_count);
            for (i = 0; i < item_count; ++i) print_value(keys[i], types[i], values[i]);
            fflush(stdout);
        }

        /* Every decoded value is heap allocated: free each one, every time.
         * Note the argument order - eshm_free_value(value, type), while
         * eshm_data_free_value(type, value) takes them the other way round. */
        for (i = 0; i < item_count; ++i) eshm_free_value(values[i], types[i]);
    }

    printf("\nc_reader: %ld record(s), %ld error(s)\n", records, errors);
    eshm_destroy(handle);
    return (records > 0 && errors == 0) ? 0 : 1;
}
