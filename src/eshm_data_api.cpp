#include "eshm.h"
#include "data_handler.h"
#include <cstring>

// High-performance C API for ESHM + DataHandler
// Combines encoding/decoding with ESHM read/write in single calls
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
int eshm_read_data(ESHMHandle* eshm,
                   uint8_t* out_types,        // Output: DataType values
                   char** out_keys,           // Output: String keys (caller provides array of char*)
                   int max_key_len,           // Max length for each key string
                   void** out_values,         // Output: Pointers to allocated values
                   int max_items)             // Maximum number of items to decode
{
    if (!eshm || !out_types || !out_keys || !out_values || max_items <= 0) {
        snprintf(last_error, sizeof(last_error), "Invalid parameters");
        return -1;
    }

    try {
        uint8_t buffer[ESHM_MAX_DATA_SIZE];

        // Read from ESHM
        int bytes_read = eshm_read(eshm, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (bytes_read == ESHM_ERROR_NO_DATA) {
                return 0;  // No data available (not an error)
            }
            snprintf(last_error, sizeof(last_error), "ESHM read failed: %d", bytes_read);
            return bytes_read;
        }

        if (bytes_read == 0) {
            return 0;  // No data
        }

        // Decode
        DataHandler handler;
        auto items = handler.decodeDataBuffer(buffer, bytes_read);

        if ((int)items.size() > max_items) {
            snprintf(last_error, sizeof(last_error),
                    "Too many items: got %zu, max %d", items.size(), max_items);
            return -1;
        }

        // Extract values
        for (size_t i = 0; i < items.size(); i++) {
            out_types[i] = static_cast<uint8_t>(items[i].type);

            // Copy key
            strncpy(out_keys[i], items[i].key.c_str(), max_key_len - 1);
            out_keys[i][max_key_len - 1] = '\0';

            // Allocate and copy value
            switch (items[i].type) {
                case DataType::INTEGER: {
                    int64_t* val = (int64_t*)malloc(sizeof(int64_t));
                    *val = std::get<int64_t>(items[i].value);
                    out_values[i] = val;
                    break;
                }
                case DataType::BOOLEAN: {
                    bool* val = (bool*)malloc(sizeof(bool));
                    *val = std::get<bool>(items[i].value);
                    out_values[i] = val;
                    break;
                }
                case DataType::REAL: {
                    double* val = (double*)malloc(sizeof(double));
                    *val = std::get<double>(items[i].value);
                    out_values[i] = val;
                    break;
                }
                case DataType::STRING: {
                    const auto& str = std::get<std::string>(items[i].value);
                    char* val = (char*)malloc(str.size() + 1);
                    strcpy(val, str.c_str());
                    out_values[i] = val;
                    break;
                }
                case DataType::BINARY: {
                    const auto& vec = std::get<std::vector<uint8_t>>(items[i].value);
                    struct BinaryData { uint8_t* data; size_t len; };
                    auto* bin = (BinaryData*)malloc(sizeof(BinaryData));
                    bin->len = vec.size();
                    bin->data = (uint8_t*)malloc(vec.size());
                    memcpy(bin->data, vec.data(), vec.size());
                    out_values[i] = bin;
                    break;
                }
            }
        }

        return items.size();

    } catch (const std::exception& e) {
        snprintf(last_error, sizeof(last_error), "Read failed: %s", e.what());
        return -1;
    }
}

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
