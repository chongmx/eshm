#ifndef ESHM_DATA_API_H
#define ESHM_DATA_API_H

/*
 * Combined ESHM + DataHandler C API: encode and write, or read and decode, in
 * a single call. Useful from C and from language bindings, where doing the DER
 * work on the other side of the FFI boundary costs more than the transfer.
 *
 * Reading is eshm_read_data(), declared in eshm.h. Values follow the same
 * representation as data_handler_c_api.h; release them with
 * eshm_data_free_value() (or eshm_free_value(), which takes its arguments in
 * the opposite order).
 */

#include "eshm.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Description of the last failure on this thread; never NULL. */
const char* eshm_data_get_last_error(void);

/*
 * Encode count items and write them to the channel in one call.
 * Returns the number of bytes written, or a negative value on error.
 */
int eshm_write_data(ESHMHandle* eshm,
                    const uint8_t* types,
                    const char** keys,
                    const void** values,
                    int count);

/* Release one value produced by eshm_read_data(). */
void eshm_data_free_value(uint8_t type, void* value);

#ifdef __cplusplus
}
#endif

#endif /* ESHM_DATA_API_H */
