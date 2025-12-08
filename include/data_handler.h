#pragma once

#include "asn1_der.h"
#include <memory>
#include <functional>

namespace shm_protocol {

// Type descriptor for the three-sequence protocol
enum class DataType : uint8_t {
    INTEGER = 0,
    BOOLEAN = 1,
    REAL = 2,
    STRING = 3,
    BINARY = 4,
    EVENT = 5,
    FUNCTION_CALL = 6,
    IMAGE_FRAME = 7
};

// Data item in the exchange
struct DataItem {
    DataType type;
    std::string key;
    asn1::DataValue value;
    
    // For complex types
    asn1::Event event;
    asn1::FunctionCall function;
    asn1::ImageFrame image;
};

// Function registry for handling function calls
using FunctionHandler = std::function<asn1::DataValue(const std::vector<asn1::DataValue>&)>;

class DataHandler {
public:
    DataHandler();
    
    // Register function handlers
    void registerFunction(const std::string& name, FunctionHandler handler);
    
    // Build data buffer from items
    std::vector<uint8_t> encodeDataBuffer(const std::vector<DataItem>& items);
    
    // Parse data buffer into items
    std::vector<DataItem> decodeDataBuffer(const uint8_t* buffer, size_t length);
    std::vector<DataItem> decodeDataBuffer(const std::vector<uint8_t>& buffer);
    
    // Process function calls found in items
    void processFunctionCalls(std::vector<DataItem>& items);
    
    // Utility: Create data items
    static DataItem createInteger(const std::string& key, int64_t value);
    static DataItem createBoolean(const std::string& key, bool value);
    static DataItem createReal(const std::string& key, double value);
    static DataItem createString(const std::string& key, const std::string& value);
    static DataItem createBinary(const std::string& key, const std::vector<uint8_t>& value);
    static DataItem createEvent(const std::string& key, const asn1::Event& event);
    static DataItem createFunctionCall(const std::string& key, const asn1::FunctionCall& func);
    static DataItem createImageFrame(const std::string& key, const asn1::ImageFrame& frame);
    
    // Utility: Extract values from items
    static std::unordered_map<std::string, asn1::DataValue> extractSimpleValues(
        const std::vector<DataItem>& items);
    
    static std::vector<asn1::Event> extractEvents(const std::vector<DataItem>& items);
    static std::vector<asn1::FunctionCall> extractFunctions(const std::vector<DataItem>& items);
    static std::vector<asn1::ImageFrame> extractImages(const std::vector<DataItem>& items);
    
private:
    std::unordered_map<std::string, FunctionHandler> function_registry_;
    
    // Encode/decode helpers for the three-sequence structure
    void encodeTypeSequence(asn1::DEREncoder& encoder, const std::vector<DataItem>& items);
    void encodeKeySequence(asn1::DEREncoder& encoder, const std::vector<DataItem>& items);
    void encodeDataSequence(asn1::DEREncoder& encoder, const std::vector<DataItem>& items);
    
    std::vector<DataType> decodeTypeSequence(asn1::DERDecoder& decoder);
    std::vector<std::string> decodeKeySequence(asn1::DERDecoder& decoder);
    std::vector<DataItem> decodeDataSequence(
        asn1::DERDecoder& decoder,
        const std::vector<DataType>& types,
        const std::vector<std::string>& keys);
};

// Predefined function signatures (examples)
namespace functions {
    
    // Simple math operations
    asn1::DataValue add(const std::vector<asn1::DataValue>& args);
    asn1::DataValue multiply(const std::vector<asn1::DataValue>& args);
    
    // Image operations
    asn1::DataValue getImageInfo(const std::vector<asn1::DataValue>& args);
    asn1::DataValue resizeImage(const std::vector<asn1::DataValue>& args);
    
    // Control operations
    asn1::DataValue setParameter(const std::vector<asn1::DataValue>& args);
    asn1::DataValue getStatus(const std::vector<asn1::DataValue>& args);
    
} // namespace functions

} // namespace shm_protocol