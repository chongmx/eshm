#include "data_handler.h"
#include <stdexcept>

namespace shm_protocol {

// ============================================================================
// DataHandler Implementation
// ============================================================================

DataHandler::DataHandler() {
    // Register default functions
    registerFunction("add", functions::add);
    registerFunction("multiply", functions::multiply);
    registerFunction("getImageInfo", functions::getImageInfo);
    registerFunction("resizeImage", functions::resizeImage);
    registerFunction("setParameter", functions::setParameter);
    registerFunction("getStatus", functions::getStatus);
}

void DataHandler::registerFunction(const std::string& name, FunctionHandler handler) {
    function_registry_[name] = handler;
}

std::vector<uint8_t> DataHandler::encodeDataBuffer(const std::vector<DataItem>& items) {
    asn1::DEREncoder encoder;
    
    // Main sequence containing all three sequences
    size_t main_seq = encoder.beginSequence();
    
    // Sequence 1: Type descriptors
    encodeTypeSequence(encoder, items);
    
    // Sequence 2: Key names
    encodeKeySequence(encoder, items);
    
    // Sequence 3: Actual data
    encodeDataSequence(encoder, items);
    
    encoder.endSequence(main_seq);
    
    return encoder.extractData();
}

void DataHandler::encodeTypeSequence(asn1::DEREncoder& encoder, const std::vector<DataItem>& items) {
    size_t type_seq = encoder.beginSequence();
    
    for (const auto& item : items) {
        encoder.encodeInteger(static_cast<int64_t>(item.type));
    }
    
    encoder.endSequence(type_seq);
}

void DataHandler::encodeKeySequence(asn1::DEREncoder& encoder, const std::vector<DataItem>& items) {
    size_t key_seq = encoder.beginSequence();
    
    for (const auto& item : items) {
        encoder.encodeUtf8String(item.key);
    }
    
    encoder.endSequence(key_seq);
}

void DataHandler::encodeDataSequence(asn1::DEREncoder& encoder, const std::vector<DataItem>& items) {
    size_t data_seq = encoder.beginSequence();
    
    for (const auto& item : items) {
        switch (item.type) {
            case DataType::INTEGER:
            case DataType::BOOLEAN:
            case DataType::REAL:
            case DataType::STRING:
            case DataType::BINARY:
                encoder.encodeDataValue(item.value);
                break;
            
            case DataType::EVENT:
                encoder.encodeEvent(item.event);
                break;
            
            case DataType::FUNCTION_CALL:
                encoder.encodeFunctionCall(item.function);
                break;
            
            case DataType::IMAGE_FRAME:
                encoder.encodeImageFrame(item.image);
                break;
        }
    }
    
    encoder.endSequence(data_seq);
}

std::vector<DataItem> DataHandler::decodeDataBuffer(const uint8_t* buffer, size_t length) {
    asn1::DERDecoder decoder(buffer, length);
    
    // Main sequence
    size_t main_end = decoder.beginSequence();
    
    // Sequence 1: Type descriptors
    std::vector<DataType> types = decodeTypeSequence(decoder);
    
    // Sequence 2: Key names
    std::vector<std::string> keys = decodeKeySequence(decoder);
    
    // Validate
    if (types.size() != keys.size()) {
        throw asn1::DERException("Type and key count mismatch");
    }
    
    // Sequence 3: Actual data
    std::vector<DataItem> items = decodeDataSequence(decoder, types, keys);
    
    decoder.endSequence(main_end);
    
    return items;
}

std::vector<DataItem> DataHandler::decodeDataBuffer(const std::vector<uint8_t>& buffer) {
    return decodeDataBuffer(buffer.data(), buffer.size());
}

std::vector<DataType> DataHandler::decodeTypeSequence(asn1::DERDecoder& decoder) {
    std::vector<DataType> types;
    
    size_t type_end = decoder.beginSequence();
    
    while (decoder.getPosition() < type_end) {
        int64_t type_val = decoder.decodeInteger();
        types.push_back(static_cast<DataType>(type_val));
    }
    
    decoder.endSequence(type_end);
    
    return types;
}

std::vector<std::string> DataHandler::decodeKeySequence(asn1::DERDecoder& decoder) {
    std::vector<std::string> keys;
    
    size_t key_end = decoder.beginSequence();
    
    while (decoder.getPosition() < key_end) {
        keys.push_back(decoder.decodeUtf8String());
    }
    
    decoder.endSequence(key_end);
    
    return keys;
}

std::vector<DataItem> DataHandler::decodeDataSequence(
    asn1::DERDecoder& decoder,
    const std::vector<DataType>& types,
    const std::vector<std::string>& keys)
{
    std::vector<DataItem> items;
    
    size_t data_end = decoder.beginSequence();
    
    for (size_t i = 0; i < types.size(); i++) {
        DataItem item;
        item.type = types[i];
        item.key = keys[i];
        
        switch (item.type) {
            case DataType::INTEGER:
                item.value = decoder.decodeInteger();
                break;
            
            case DataType::BOOLEAN:
                item.value = decoder.decodeBoolean();
                break;
            
            case DataType::REAL:
                item.value = decoder.decodeReal();
                break;
            
            case DataType::STRING:
                item.value = decoder.decodeUtf8String();
                break;
            
            case DataType::BINARY:
                item.value = decoder.decodeOctetString();
                break;
            
            case DataType::EVENT:
                item.event = decoder.decodeEvent();
                break;
            
            case DataType::FUNCTION_CALL:
                item.function = decoder.decodeFunctionCall();
                break;
            
            case DataType::IMAGE_FRAME:
                item.image = decoder.decodeImageFrame();
                break;
        }
        
        items.push_back(item);
    }
    
    decoder.endSequence(data_end);
    
    return items;
}

void DataHandler::processFunctionCalls(std::vector<DataItem>& items) {
    for (auto& item : items) {
        if (item.type == DataType::FUNCTION_CALL) {
            auto it = function_registry_.find(item.function.function_name);
            if (it != function_registry_.end()) {
                // Execute the function
                try {
                    item.function.return_value = it->second(item.function.arguments);
                    item.function.has_return = true;
                } catch (const std::exception& e) {
                    // Function execution failed, set error return
                    item.function.return_value = std::string("ERROR: ") + e.what();
                    item.function.has_return = true;
                }
            } else {
                // Function not found
                item.function.return_value = std::string("ERROR: Function not found: ") + 
                                            item.function.function_name;
                item.function.has_return = true;
            }
        }
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

DataItem DataHandler::createInteger(const std::string& key, int64_t value) {
    DataItem item;
    item.type = DataType::INTEGER;
    item.key = key;
    item.value = value;
    return item;
}

DataItem DataHandler::createBoolean(const std::string& key, bool value) {
    DataItem item;
    item.type = DataType::BOOLEAN;
    item.key = key;
    item.value = value;
    return item;
}

DataItem DataHandler::createReal(const std::string& key, double value) {
    DataItem item;
    item.type = DataType::REAL;
    item.key = key;
    item.value = value;
    return item;
}

DataItem DataHandler::createString(const std::string& key, const std::string& value) {
    DataItem item;
    item.type = DataType::STRING;
    item.key = key;
    item.value = value;
    return item;
}

DataItem DataHandler::createBinary(const std::string& key, const std::vector<uint8_t>& value) {
    DataItem item;
    item.type = DataType::BINARY;
    item.key = key;
    item.value = value;
    return item;
}

DataItem DataHandler::createEvent(const std::string& key, const asn1::Event& event) {
    DataItem item;
    item.type = DataType::EVENT;
    item.key = key;
    item.event = event;
    return item;
}

DataItem DataHandler::createFunctionCall(const std::string& key, const asn1::FunctionCall& func) {
    DataItem item;
    item.type = DataType::FUNCTION_CALL;
    item.key = key;
    item.function = func;
    return item;
}

DataItem DataHandler::createImageFrame(const std::string& key, const asn1::ImageFrame& frame) {
    DataItem item;
    item.type = DataType::IMAGE_FRAME;
    item.key = key;
    item.image = frame;
    return item;
}

std::unordered_map<std::string, asn1::DataValue> DataHandler::extractSimpleValues(
    const std::vector<DataItem>& items)
{
    std::unordered_map<std::string, asn1::DataValue> values;
    
    for (const auto& item : items) {
        if (item.type == DataType::INTEGER ||
            item.type == DataType::BOOLEAN ||
            item.type == DataType::REAL ||
            item.type == DataType::STRING ||
            item.type == DataType::BINARY)
        {
            values[item.key] = item.value;
        }
    }
    
    return values;
}

std::vector<asn1::Event> DataHandler::extractEvents(const std::vector<DataItem>& items) {
    std::vector<asn1::Event> events;
    
    for (const auto& item : items) {
        if (item.type == DataType::EVENT) {
            events.push_back(item.event);
        }
    }
    
    return events;
}

std::vector<asn1::FunctionCall> DataHandler::extractFunctions(const std::vector<DataItem>& items) {
    std::vector<asn1::FunctionCall> functions;
    
    for (const auto& item : items) {
        if (item.type == DataType::FUNCTION_CALL) {
            functions.push_back(item.function);
        }
    }
    
    return functions;
}

std::vector<asn1::ImageFrame> DataHandler::extractImages(const std::vector<DataItem>& items) {
    std::vector<asn1::ImageFrame> images;
    
    for (const auto& item : items) {
        if (item.type == DataType::IMAGE_FRAME) {
            images.push_back(item.image);
        }
    }
    
    return images;
}

// ============================================================================
// Predefined Function Implementations
// ============================================================================

namespace functions {

asn1::DataValue add(const std::vector<asn1::DataValue>& args) {
    if (args.size() != 2) {
        throw std::runtime_error("add requires 2 arguments");
    }
    
    // Support int + int or double + double
    if (std::holds_alternative<int64_t>(args[0]) && std::holds_alternative<int64_t>(args[1])) {
        return std::get<int64_t>(args[0]) + std::get<int64_t>(args[1]);
    } else if (std::holds_alternative<double>(args[0]) && std::holds_alternative<double>(args[1])) {
        return std::get<double>(args[0]) + std::get<double>(args[1]);
    }
    
    throw std::runtime_error("add requires numeric arguments");
}

asn1::DataValue multiply(const std::vector<asn1::DataValue>& args) {
    if (args.size() != 2) {
        throw std::runtime_error("multiply requires 2 arguments");
    }
    
    if (std::holds_alternative<int64_t>(args[0]) && std::holds_alternative<int64_t>(args[1])) {
        return std::get<int64_t>(args[0]) * std::get<int64_t>(args[1]);
    } else if (std::holds_alternative<double>(args[0]) && std::holds_alternative<double>(args[1])) {
        return std::get<double>(args[0]) * std::get<double>(args[1]);
    }
    
    throw std::runtime_error("multiply requires numeric arguments");
}

asn1::DataValue getImageInfo(const std::vector<asn1::DataValue>& args) {
    (void)args;  // Unused
    // Placeholder - would inspect an image and return info
    return std::string("1920x1080x3");
}

asn1::DataValue resizeImage(const std::vector<asn1::DataValue>& args) {
    (void)args;  // Unused
    // Placeholder - would resize an image
    return std::string("Image resized successfully");
}

asn1::DataValue setParameter(const std::vector<asn1::DataValue>& args) {
    if (args.size() != 2) {
        throw std::runtime_error("setParameter requires 2 arguments: name and value");
    }
    
    // In real implementation, would set a parameter
    return true; // Success
}

asn1::DataValue getStatus(const std::vector<asn1::DataValue>& args) {
    (void)args;  // Unused
    // Return a status string
    return std::string("OK");
}

} // namespace functions

} // namespace shm_protocol