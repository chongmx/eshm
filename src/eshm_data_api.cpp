#include "eshm_data_api.h"
#include "eshm.h"
#include "data_handler.h"
#include <cstring>

// High-performance C API for ESHM + DataHandler
// Combines encoding/decoding with ESHM read/write in single calls
//
// eshm_read_data() is not defined here: eshm.h declares it and src/eshm.cpp
// implements it, with an item_count out-parameter and a timeout that this file
// once lacked.
extern "C" {

using namespace shm_protocol;

// Error handling
static thread_local char last_error[256] = {0};

const char* eshm_data_get_last_error() {
    return last_error;
}

// Write data items directly to ESHM (encode + write in one call)
// Returns bytes written, or negative on error
int eshm_write_data(ESHMHandle* eshm,
                    const uint8_t* types,      // DataType values
                    const char** keys,         // String keys
                    const void** values,       // Pointers to values
                    int count)                 // Number of items
{
    if (!eshm || !types || !keys || !values || count <= 0) {
        snprintf(last_error, sizeof(last_error), "Invalid parameters");
        return -1;
    }

    try {
        DataHandler handler;
        std::vector<DataItem> items;
        items.reserve(count);

        // Build items
        for (int i = 0; i < count; i++) {
            DataType type = static_cast<DataType>(types[i]);
            std::string key = keys[i];

            switch (type) {
                case DataType::INTEGER: {
                    int64_t val = *static_cast<const int64_t*>(values[i]);
                    items.push_back(DataHandler::createInteger(key, val));
                    break;
                }
                case DataType::BOOLEAN: {
                    bool val = *static_cast<const bool*>(values[i]);
                    items.push_back(DataHandler::createBoolean(key, val));
                    break;
                }
                case DataType::REAL: {
                    double val = *static_cast<const double*>(values[i]);
                    items.push_back(DataHandler::createReal(key, val));
                    break;
                }
                case DataType::STRING: {
                    const char* val = static_cast<const char*>(values[i]);
                    items.push_back(DataHandler::createString(key, std::string(val)));
                    break;
                }
                case DataType::BINARY: {
                    struct BinaryData { const uint8_t* data; size_t len; };
                    auto* bin = static_cast<const BinaryData*>(values[i]);
                    std::vector<uint8_t> vec(bin->data, bin->data + bin->len);
                    items.push_back(DataHandler::createBinary(key, vec));
                    break;
                }
                default:
                    snprintf(last_error, sizeof(last_error), "Unsupported type: %d", (int)type);
                    return -1;
            }
        }

        // Encode
        auto buffer = handler.encodeDataBuffer(items);

        // Write to ESHM
        int ret = eshm_write(eshm, buffer.data(), buffer.size());
        if (ret < 0) {
            snprintf(last_error, sizeof(last_error), "ESHM write failed: %d", ret);
            return ret;
        }

        return buffer.size();

    } catch (const std::exception& e) {
        snprintf(last_error, sizeof(last_error), "Write failed: %s", e.what());
        return -1;
    }
}

// Read and decode data from ESHM (read + decode in one call)
// Returns number of items decoded, or negative on error
// Free a decoded value (same as data_handler_c_api)
void eshm_data_free_value(uint8_t type, void* value) {
    if (!value) return;

    switch (static_cast<DataType>(type)) {
        case DataType::INTEGER:
        case DataType::BOOLEAN:
        case DataType::REAL:
        case DataType::STRING:
            free(value);
            break;
        case DataType::BINARY: {
            struct BinaryData { uint8_t* data; size_t len; };
            auto* bin = static_cast<BinaryData*>(value);
            free(bin->data);
            free(bin);
            break;
        }
        default:
            break;
    }
}

} // extern "C"
