#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <variant>
#include <unordered_map>
#include <stdexcept>
#include <cstring>

namespace asn1 {

// ASN.1 Universal Tags
enum class Tag : uint8_t {
    INTEGER = 0x02,
    OCTET_STRING = 0x04,
    NULL_TYPE = 0x05,
    SEQUENCE = 0x10,
    UTF8_STRING = 0x0C,
    BOOLEAN = 0x01,
    REAL = 0x09
};

// Custom application tags for our protocol
enum class AppTag : uint8_t {
    EVENT = 0x80,           // Application tag 0
    FUNCTION_CALL = 0x81,   // Application tag 1
    FUNCTION_RETURN = 0x82, // Application tag 2
    IMAGE_FRAME = 0x83      // Application tag 3
};

// Generic variant type for data exchange
using DataValue = std::variant<
    bool,
    int64_t,
    double,
    std::string,
    std::vector<uint8_t>  // For binary data like images
>;

// Function signature structure
struct FunctionCall {
    std::string function_name;
    std::vector<DataValue> arguments;
    DataValue return_value;
    bool has_return = false;
};

// Event structure
struct Event {
    std::string event_name;
    std::unordered_map<std::string, DataValue> parameters;
};

// Image frame structure
struct ImageFrame {
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint64_t timestamp_ns;
    std::vector<uint8_t> data;
};

class DEREncoder {
public:
    DEREncoder() { buffer_.reserve(4096); }
    
    // Encode tag and length
    void encodeTag(uint8_t tag);
    void encodeLength(size_t length);
    
    // Basic type encoders
    void encodeInteger(int64_t value);
    void encodeBoolean(bool value);
    void encodeOctetString(const uint8_t* data, size_t length);
    void encodeOctetString(const std::vector<uint8_t>& data);
    void encodeUtf8String(const std::string& str);
    void encodeNull();
    void encodeReal(double value);
    
    // Sequence handling
    size_t beginSequence();
    void endSequence(size_t start_pos);
    
    // High-level encoders
    void encodeDataValue(const DataValue& value);
    void encodeFunctionCall(const FunctionCall& func);
    void encodeEvent(const Event& event);
    void encodeImageFrame(const ImageFrame& frame);
    
    // Get encoded data
    const std::vector<uint8_t>& getData() const { return buffer_; }
    std::vector<uint8_t> extractData() { return std::move(buffer_); }
    void clear() { buffer_.clear(); }
    
private:
    std::vector<uint8_t> buffer_;
    
    void appendBytes(const uint8_t* data, size_t length);
    void appendByte(uint8_t byte);
};

class DERDecoder {
public:
    DERDecoder(const uint8_t* data, size_t length)
        : data_(data), length_(length), pos_(0) {}
    
    DERDecoder(const std::vector<uint8_t>& data)
        : data_(data.data()), length_(data.size()), pos_(0) {}
    
    // Decode tag and length
    uint8_t decodeTag();
    size_t decodeLength();
    
    // Basic type decoders
    int64_t decodeInteger();
    bool decodeBoolean();
    std::vector<uint8_t> decodeOctetString();
    std::string decodeUtf8String();
    void decodeNull();
    double decodeReal();
    
    // Sequence handling
    size_t beginSequence();
    void endSequence(size_t end_pos);
    
    // High-level decoders
    DataValue decodeDataValue(uint8_t tag);
    FunctionCall decodeFunctionCall();
    Event decodeEvent();
    ImageFrame decodeImageFrame();
    
    // Utilities
    bool hasMoreData() const { return pos_ < length_; }
    size_t getPosition() const { return pos_; }
    size_t remaining() const { return length_ - pos_; }
    
private:
    const uint8_t* data_;
    size_t length_;
    size_t pos_;
    
    uint8_t readByte();
    void readBytes(uint8_t* dest, size_t length);
    void skip(size_t length);
};

// Exception class
class DERException : public std::runtime_error {
public:
    explicit DERException(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace asn1