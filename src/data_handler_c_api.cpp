#include "data_handler.h"
#include <cstring>
#include <cstdlib>

// C API for Python ctypes binding
extern "C" {

using namespace shm_protocol;

// Opaque handle for DataHandler
typedef void* DataHandlerHandle;

// Error handling
static thread_local char last_error[256] = {0};

const char* dh_get_last_error() {
    return last_error;
}

// Create/destroy DataHandler
DataHandlerHandle dh_create() {
    try {
        return new DataHandler();
    } catch (const std::exception& e) {
        snprintf(last_error, sizeof(last_error), "Create failed: %s", e.what());
        return nullptr;
    }
}

void dh_destroy(DataHandlerHandle handle) {
    if (handle) {
        delete static_cast<DataHandler*>(handle);
    }
}

// Encode data items into buffer
// Returns size of encoded data, or negative on error
int dh_encode(DataHandlerHandle handle,
              const uint8_t* types,    // DataType values
              const char** keys,       // String keys (null-terminated)
              const void** values,     // Pointers to values
              int count,               // Number of items
              uint8_t* out_buffer,     // Output buffer
              int out_buffer_size)     // Size of output buffer
{
    if (!handle || !types || !keys || !values || !out_buffer) {
        snprintf(last_error, sizeof(last_error), "Invalid parameters");
        return -1;
    }

    try {
        auto* dh = static_cast<DataHandler*>(handle);
        std::vector<DataItem> items;
        items.reserve(count);

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
                    // For binary: values[i] points to a struct { uint8_t* data; size_t len; }
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

        auto buffer = dh->encodeDataBuffer(items);

        if ((int)buffer.size() > out_buffer_size) {
            snprintf(last_error, sizeof(last_error),
                    "Buffer too small: need %zu, have %d", buffer.size(), out_buffer_size);
            return -1;
        }

        memcpy(out_buffer, buffer.data(), buffer.size());
        return buffer.size();

    } catch (const std::exception& e) {
        snprintf(last_error, sizeof(last_error), "Encode failed: %s", e.what());
        return -1;
    }
}

// Decode buffer into data items
// Returns number of items decoded, or negative on error
int dh_decode(DataHandlerHandle handle,
              const uint8_t* buffer,
              int buffer_size,
              uint8_t* out_types,      // Output: DataType values
              char** out_keys,         // Output: String keys (caller must provide array of char*)
              int max_key_len,         // Max length for each key string
              void** out_values,       // Output: Pointers to allocated values
              int max_items)           // Maximum number of items to decode
{
    if (!handle || !buffer || !out_types || !out_keys || !out_values) {
        snprintf(last_error, sizeof(last_error), "Invalid parameters");
        return -1;
    }

    try {
        auto* dh = static_cast<DataHandler*>(handle);
        auto items = dh->decodeDataBuffer(buffer, buffer_size);

        if ((int)items.size() > max_items) {
            snprintf(last_error, sizeof(last_error),
                    "Too many items: got %zu, max %d", items.size(), max_items);
            return -1;
        }

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
                    // Allocate struct { uint8_t* data; size_t len; }
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
        snprintf(last_error, sizeof(last_error), "Decode failed: %s", e.what());
        return -1;
    }
}

// Free a decoded value
void dh_free_value(uint8_t type, void* value) {
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
