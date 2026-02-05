#include "asn1_der.h"
#include <cmath>

namespace asn1 {

// ============================================================================
// DERDecoder Implementation
// ============================================================================

uint8_t DERDecoder::readByte() {
    if (pos_ >= length_) {
        throw DERException("Unexpected end of data");
    }
    return data_[pos_++];
}

void DERDecoder::readBytes(uint8_t* dest, size_t length) {
    if (pos_ + length > length_) {
        throw DERException("Unexpected end of data");
    }
    std::memcpy(dest, data_ + pos_, length);
    pos_ += length;
}

void DERDecoder::skip(size_t length) {
    if (pos_ + length > length_) {
        throw DERException("Unexpected end of data");
    }
    pos_ += length;
}

uint8_t DERDecoder::decodeTag() {
    return readByte();
}

size_t DERDecoder::decodeLength() {
    uint8_t first_byte = readByte();
    
    if ((first_byte & 0x80) == 0) {
        // Short form
        return first_byte;
    } else {
        // Long form
        uint8_t num_bytes = first_byte & 0x7F;
        if (num_bytes > 4) {
            throw DERException("Length too large");
        }
        
        size_t length = 0;
        for (uint8_t i = 0; i < num_bytes; i++) {
            length = (length << 8) | readByte();
        }
        return length;
    }
}

int64_t DERDecoder::decodeInteger() {
    uint8_t tag = decodeTag();
    if (tag != static_cast<uint8_t>(Tag::INTEGER)) {
        throw DERException("Expected INTEGER tag");
    }
    
    size_t length = decodeLength();
    if (length == 0 || length > 8) {
        throw DERException("Invalid integer length");
    }
    
    std::vector<uint8_t> bytes(length);
    readBytes(bytes.data(), length);
    
    // Check sign bit
    bool negative = (bytes[0] & 0x80) != 0;
    
    int64_t value = 0;
    for (size_t i = 0; i < length; i++) {
        value = (value << 8) | bytes[i];
    }
    
    // Handle negative values (two's complement)
    if (negative && length < 8) {
        // Sign extend
        int64_t sign_extend = static_cast<int64_t>(~0ULL << (length * 8));
        value |= sign_extend;
    }
    
    return value;
}

bool DERDecoder::decodeBoolean() {
    uint8_t tag = decodeTag();
    if (tag != static_cast<uint8_t>(Tag::BOOLEAN)) {
        throw DERException("Expected BOOLEAN tag");
    }
    
    size_t length = decodeLength();
    if (length != 1) {
        throw DERException("Invalid boolean length");
    }
    
    uint8_t value = readByte();
    return value != 0;
}

std::vector<uint8_t> DERDecoder::decodeOctetString() {
    uint8_t tag = decodeTag();
    if (tag != static_cast<uint8_t>(Tag::OCTET_STRING)) {
        throw DERException("Expected OCTET_STRING tag");
    }
    
    size_t length = decodeLength();
    std::vector<uint8_t> data(length);
    if (length > 0) {
        readBytes(data.data(), length);
    }
    return data;
}

std::string DERDecoder::decodeUtf8String() {
    uint8_t tag = decodeTag();
    if (tag != static_cast<uint8_t>(Tag::UTF8_STRING)) {
        throw DERException("Expected UTF8_STRING tag");
    }
    
    size_t length = decodeLength();
    std::string str(length, '\0');
    if (length > 0) {
        readBytes(reinterpret_cast<uint8_t*>(&str[0]), length);
    }
    return str;
}

void DERDecoder::decodeNull() {
    uint8_t tag = decodeTag();
    if (tag != static_cast<uint8_t>(Tag::NULL_TYPE)) {
        throw DERException("Expected NULL tag");
    }
    
    size_t length = decodeLength();
    if (length != 0) {
        throw DERException("NULL must have zero length");
    }
}

double DERDecoder::decodeReal() {
    uint8_t tag = decodeTag();
    if (tag != static_cast<uint8_t>(Tag::REAL)) {
        throw DERException("Expected REAL tag");
    }

    size_t length = decodeLength();

    if (length == 0) {
        return 0.0;
    }

    uint8_t header = readByte();

    // ISO 6093 NR3 format (simple binary encoding)
    if (header == 0x03) {
        // Read 8 bytes of IEEE 754 double in big-endian order
        uint64_t bits = 0;
        for (int i = 0; i < 8; i++) {
            bits = (bits << 8) | readByte();
        }

        double value;
        std::memcpy(&value, &bits, sizeof(double));
        return value;
    }

    // Binary encoding (legacy format for backwards compatibility)
    if (header & 0x80) {
        int sign = (header & 0x40) ? -1 : 1;
        size_t exp_len = (header & 0x03) + 1;

        // Read exponent
        std::vector<uint8_t> exp_bytes(exp_len);
        readBytes(exp_bytes.data(), exp_len);

        int64_t exponent = 0;
        bool exp_negative = (exp_bytes[0] & 0x80) != 0;

        for (size_t i = 0; i < exp_len; i++) {
            exponent = (exponent << 8) | exp_bytes[i];
        }

        if (exp_negative && exp_len < 8) {
            int64_t sign_extend = static_cast<int64_t>(~0ULL << (exp_len * 8));
            exponent |= sign_extend;
        }

        // Read mantissa
        size_t mantissa_len = length - 1 - exp_len;
        uint64_t mantissa = 0;
        for (size_t i = 0; i < mantissa_len && i < 8; i++) {
            mantissa = (mantissa << 8) | readByte();
        }

        // Skip any extra mantissa bytes
        if (mantissa_len > 8) {
            skip(mantissa_len - 8);
        }

        // Reconstruct double
        double value = static_cast<double>(mantissa) * std::pow(2.0, exponent);
        return sign * value;
    }

    throw DERException("Unsupported REAL encoding");
}

size_t DERDecoder::beginSequence() {
    uint8_t tag = decodeTag();
    if ((tag & 0x1F) != static_cast<uint8_t>(Tag::SEQUENCE)) {
        throw DERException("Expected SEQUENCE tag");
    }
    
    size_t length = decodeLength();
    size_t end_pos = pos_ + length;
    
    if (end_pos > length_) {
        throw DERException("Sequence extends beyond data");
    }
    
    return end_pos;
}

void DERDecoder::endSequence(size_t end_pos) {
    if (pos_ != end_pos) {
        throw DERException("Sequence not fully consumed");
    }
}

DataValue DERDecoder::decodeDataValue(uint8_t tag) {
    switch (static_cast<Tag>(tag)) {
        case Tag::BOOLEAN:
            pos_--; // Put tag back
            return decodeBoolean();
        
        case Tag::INTEGER:
            pos_--; // Put tag back
            return decodeInteger();
        
        case Tag::REAL:
            pos_--; // Put tag back
            return decodeReal();
        
        case Tag::UTF8_STRING:
            pos_--; // Put tag back
            return decodeUtf8String();
        
        case Tag::OCTET_STRING:
            pos_--; // Put tag back
            return decodeOctetString();
        
        default:
            throw DERException("Unsupported data type tag");
    }
}

FunctionCall DERDecoder::decodeFunctionCall() {
    FunctionCall func;
    
    size_t end_pos = beginSequence();
    
    // Decode function name
    func.function_name = decodeUtf8String();
    
    // Decode return value
    uint8_t tag = decodeTag();
    if (tag == static_cast<uint8_t>(Tag::NULL_TYPE)) {
        size_t len = decodeLength();
        if (len != 0) throw DERException("Invalid NULL length");
        func.has_return = false;
    } else {
        func.return_value = decodeDataValue(tag);
        func.has_return = true;
    }
    
    // Decode arguments
    size_t args_end = beginSequence();
    while (pos_ < args_end) {
        tag = decodeTag();
        func.arguments.push_back(decodeDataValue(tag));
    }
    endSequence(args_end);
    
    endSequence(end_pos);
    
    return func;
}

Event DERDecoder::decodeEvent() {
    Event event;
    
    size_t end_pos = beginSequence();
    
    // Decode event name
    event.event_name = decodeUtf8String();
    
    // Decode parameters
    size_t params_end = beginSequence();
    while (pos_ < params_end) {
        // Each parameter is [key, value]
        size_t param_end = beginSequence();
        std::string key = decodeUtf8String();
        uint8_t tag = decodeTag();
        DataValue value = decodeDataValue(tag);
        event.parameters[key] = value;
        endSequence(param_end);
    }
    endSequence(params_end);
    
    endSequence(end_pos);
    
    return event;
}

ImageFrame DERDecoder::decodeImageFrame() {
    ImageFrame frame;
    
    size_t end_pos = beginSequence();
    
    // Decode metadata
    frame.width = static_cast<uint32_t>(decodeInteger());
    frame.height = static_cast<uint32_t>(decodeInteger());
    frame.channels = static_cast<uint32_t>(decodeInteger());
    frame.timestamp_ns = static_cast<uint64_t>(decodeInteger());
    
    // Decode image data
    frame.data = decodeOctetString();
    
    endSequence(end_pos);
    
    return frame;
}

} // namespace asn1