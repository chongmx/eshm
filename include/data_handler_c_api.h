#ifndef DATA_HANDLER_C_API_H
#define DATA_HANDLER_C_API_H

/*
 * C API for the DataHandler ASN.1 DER codec.
 *
 * shm_protocol::DataHandler is a C++ class whose interface uses std::string,
 * std::vector and std::variant, so it cannot be called from C, from ctypes, or
 * safely across compiler/standard-library versions. This opaque-handle C API is
 * the ABI-stable surface: it is what the Python bindings bind to, and what any
 * non-C++ consumer should use.
 *
 * Values are passed as pointers whose target depends on the DataType:
 *   INTEGER -> int64_t*      BOOLEAN -> bool*        REAL -> double*
 *   STRING  -> char*         BINARY  -> struct { uint8_t* data; size_t len; }*
 *
 * Decoded values are heap allocated; release each one with dh_free_value().
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque DataHandler instance. */
typedef void* DataHandlerHandle;

/* Description of the last failure on this thread; never NULL. */
const char* dh_get_last_error(void);

/* Create/destroy a handler. dh_create() returns NULL on failure. */
DataHandlerHandle dh_create(void);
void dh_destroy(DataHandlerHandle handle);

/*
 * Encode count items into out_buffer.
 * Returns the number of bytes written, or -1 on error.
 */
int dh_encode(DataHandlerHandle handle,
              const uint8_t* types,
              const char** keys,
              const void** values,
              int count,
              uint8_t* out_buffer,
              int out_buffer_size);

/*
 * Decode buffer into caller-provided arrays (out_keys must point at max_items
 * buffers of max_key_len bytes each).
 * Returns the number of items decoded, or -1 on error.
 */
int dh_decode(DataHandlerHandle handle,
              const uint8_t* buffer,
              int buffer_size,
              uint8_t* out_types,
              char** out_keys,
              int max_key_len,
              void** out_values,
              int max_items);

/* Release one value produced by dh_decode(). */
void dh_free_value(uint8_t type, void* value);

#ifdef __cplusplus
}
#endif

#endif /* DATA_HANDLER_C_API_H */
