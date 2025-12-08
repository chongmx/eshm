#include "asn1_der.h"
#include <cmath>
#include <algorithm>

namespace asn1 {

// ============================================================================
// DEREncoder Implementation
// ============================================================================

void DEREncoder::appendByte(uint8_t byte) {
    buffer_.push_back(byte);
}

void DEREncoder::appendBytes(const uint8_t* data, size_t length) {
    buffer_.insert(buffer_.end(), data, data + length);
}

void DEREncoder::encodeTag(uint8_t tag) {
    appendByte(tag);
}

void DEREncoder::encodeLength(size_t length) {
    if (length < 128) {
        // Short form
        appendByte(static_cast<uint8_t>(length));
    } else {
        // Long form
        uint8_t num_bytes = 0;
        size_t temp = length;
        while (temp > 0) {
            num_bytes++;
            temp >>= 8;
        }
        
        appendByte(0x80 | num_bytes);
        
        for (int i = num_bytes - 1; i >= 0; i--) {
            appendByte(static_cast<uint8_t>((length >> (i * 8)) & 0xFF));
        }
    }
}

void DEREncoder::encodeInteger(int64_t value) {
    encodeTag(static_cast<uint8_t>(Tag::INTEGER));
    
    // Determine minimum bytes needed
    std::vector<uint8_t> bytes;
    
    if (value == 0) {
        bytes.push_back(0);
    } else {
        uint64_t uvalue = (value < 0) ? ~static_cast<uint64_t>(value) + 1 : static_cast<uint64_t>(value);
        bool negative = value < 0;
        
        // Extract bytes
        while (uvalue > 0 || bytes.empty()) {
            bytes.push_back(static_cast<uint8_t>(uvalue & 0xFF));
            uvalue >>= 8;
        }
        
        std::reverse(bytes.begin(), bytes.end());
        
        // Handle two's complement sign bit
        if (negative) {
            for (auto& byte : bytes) byte = ~byte;
            
            // Add 1
            int carry = 1;
            for (int i = bytes.size() - 1; i >= 0 && carry; i--) {
                int sum = bytes[i] + carry;
                bytes[i] = sum & 0xFF;
                carry = sum >> 8;
            }
            
            // Ensure sign bit is set
            if ((bytes[0] & 0x80) == 0) {
                bytes.insert(bytes.begin(), 0xFF);
            }
        } else {
            // Ensure sign bit is clear for positive
            if (bytes[0] & 0x80) {
                bytes.insert(bytes.begin(), 0x00);
            }
        }
    }
    
    encodeLength(bytes.size());
    appendBytes(bytes.data(), bytes.size());
}

void DEREncoder::encodeBoolean(bool value) {
    encodeTag(static_cast<uint8_t>(Tag::BOOLEAN));
    encodeLength(1);
    appendByte(value ? 0xFF : 0x00);
}

void DEREncoder::encodeOctetString(const uint8_t* data, size_t length) {
    encodeTag(static_cast<uint8_t>(Tag::OCTET_STRING));
    encodeLength(length);
    appendBytes(data, length);
}

void DEREncoder::encodeOctetString(const std::vector<uint8_t>& data) {
    encodeOctetString(data.data(), data.size());
}

void DEREncoder::encodeUtf8String(const std::string& str) {
    encodeTag(static_cast<uint8_t>(Tag::UTF8_STRING));
    encodeLength(str.length());
    appendBytes(reinterpret_cast<const uint8_t*>(str.data()), str.length());
}

void DEREncoder::encodeNull() {
    encodeTag(static_cast<uint8_t>(Tag::NULL_TYPE));
    encodeLength(0);
}

void DEREncoder::encodeReal(double value) {
    // Simple IEEE 754 binary64 encoding
    // We store the raw 8 bytes with a simple header
    encodeTag(static_cast<uint8_t>(Tag::REAL));

    if (value == 0.0) {
        encodeLength(0);
        return;
    }

    // Use a simple encoding: header byte + 8 bytes of IEEE 754 double
    // Header: 0x80 = binary encoding, base 2, using IEEE 754 binary64 format
    std::vector<uint8_t> bytes;
    bytes.push_back(0x03);  // ISO 6093 NR3 (simple binary encoding marker)

    // Append the 8 bytes of the double in big-endian order
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));

    for (int i = 7; i >= 0; i--) {
        bytes.push_back((bits >> (i * 8)) & 0xFF);
    }

    encodeLength(bytes.size());
    appendBytes(bytes.data(), bytes.size());
}

size_t DEREncoder::beginSequence() {
    encodeTag(static_cast<uint8_t>(Tag::SEQUENCE) | 0x20); // Constructed
    size_t pos = buffer_.size();
    // Placeholder for length (use long form with 4 bytes)
    appendByte(0x84); // Long form, 4 bytes
    appendByte(0x00);
    appendByte(0x00);
    appendByte(0x00);
    appendByte(0x00);
    return pos;
}

void DEREncoder::endSequence(size_t start_pos) {
    size_t content_length = buffer_.size() - start_pos - 5; // -5 for length encoding
    
    // Update length bytes (big-endian)
    buffer_[start_pos + 1] = (content_length >> 24) & 0xFF;
    buffer_[start_pos + 2] = (content_length >> 16) & 0xFF;
    buffer_[start_pos + 3] = (content_length >> 8) & 0xFF;
    buffer_[start_pos + 4] = content_length & 0xFF;
}

void DEREncoder::encodeDataValue(const DataValue& value) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            this->encodeBoolean(arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            this->encodeInteger(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            this->encodeReal(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            this->encodeUtf8String(arg);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            this->encodeOctetString(arg);
        }
    }, value);
}

void DEREncoder::encodeFunctionCall(const FunctionCall& func) {
    size_t seq_start = beginSequence();
    
    // Encode function name
    encodeUtf8String(func.function_name);
    
    // Encode return value if present
    if (func.has_return) {
        encodeDataValue(func.return_value);
    } else {
        encodeNull();
    }
    
    // Encode arguments sequence
    size_t args_start = beginSequence();
    for (const auto& arg : func.arguments) {
        encodeDataValue(arg);
    }
    endSequence(args_start);
    
    endSequence(seq_start);
}

void DEREncoder::encodeEvent(const Event& event) {
    size_t seq_start = beginSequence();
    
    // Encode event name
    encodeUtf8String(event.event_name);
    
    // Encode parameters sequence
    size_t params_start = beginSequence();
    for (const auto& [key, value] : event.parameters) {
        // Each parameter is a sequence of [key, value]
        size_t param_start = beginSequence();
        encodeUtf8String(key);
        encodeDataValue(value);
        endSequence(param_start);
    }
    endSequence(params_start);
    
    endSequence(seq_start);
}

void DEREncoder::encodeImageFrame(const ImageFrame& frame) {
    size_t seq_start = beginSequence();
    
    // Encode metadata
    encodeInteger(frame.width);
    encodeInteger(frame.height);
    encodeInteger(frame.channels);
    encodeInteger(frame.timestamp_ns);
    
    // Encode image data
    encodeOctetString(frame.data);
    
    endSequence(seq_start);
}

} // namespace asn1