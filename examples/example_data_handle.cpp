#include "data_handler.h"
#include <iostream>
#include <iomanip>
#include <chrono>

using namespace shm_protocol;
using namespace asn1;

// Helper to print DataValue
void printDataValue(const DataValue& value) {
    std::visit([](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            std::cout << (arg ? "true" : "false");
        } else if constexpr (std::is_same_v<T, int64_t>) {
            std::cout << arg;
        } else if constexpr (std::is_same_v<T, double>) {
            std::cout << arg;
        } else if constexpr (std::is_same_v<T, std::string>) {
            std::cout << "\"" << arg << "\"";
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            std::cout << "[" << arg.size() << " bytes]";
        }
    }, value);
}

// Example 1: Simple data types
void example1_simple_data() {
    std::cout << "\n=== Example 1: Simple Data Types ===\n";
    
    DataHandler handler;
    
    // Create some data items
    std::vector<DataItem> items;
    items.push_back(DataHandler::createInteger("robot_joint_count", 6));
    items.push_back(DataHandler::createReal("temperature", 25.5));
    items.push_back(DataHandler::createBoolean("is_enabled", true));
    items.push_back(DataHandler::createString("status", "Running"));
    
    // Encode
    auto buffer = handler.encodeDataBuffer(items);
    std::cout << "Encoded " << items.size() << " items into " << buffer.size() << " bytes\n";
    
    // Decode
    auto decoded_items = handler.decodeDataBuffer(buffer);
    std::cout << "Decoded " << decoded_items.size() << " items:\n";
    
    for (const auto& item : decoded_items) {
        std::cout << "  " << item.key << " = ";
        printDataValue(item.value);
        std::cout << "\n";
    }
    
    // Extract to map
    auto values = DataHandler::extractSimpleValues(decoded_items);
    std::cout << "\nExtracted to map with " << values.size() << " entries\n";
}

// Example 2: Events
void example2_events() {
    std::cout << "\n=== Example 2: Events ===\n";
    
    DataHandler handler;
    
    // Create an event
    Event emergency_stop;
    emergency_stop.event_name = "emergency_stop";
    emergency_stop.parameters["reason"] = std::string("Safety button pressed");
    emergency_stop.parameters["timestamp"] = int64_t(12345678);
    emergency_stop.parameters["severity"] = int64_t(10);
    
    Event motion_complete;
    motion_complete.event_name = "motion_complete";
    motion_complete.parameters["joint_id"] = int64_t(3);
    motion_complete.parameters["position"] = 1.57;
    
    // Create data items
    std::vector<DataItem> items;
    items.push_back(DataHandler::createEvent("event1", emergency_stop));
    items.push_back(DataHandler::createEvent("event2", motion_complete));
    items.push_back(DataHandler::createInteger("sequence_number", 42));
    
    // Encode/Decode
    auto buffer = handler.encodeDataBuffer(items);
    auto decoded_items = handler.decodeDataBuffer(buffer);
    
    std::cout << "Encoded and decoded " << decoded_items.size() << " items\n";
    
    // Extract events
    auto events = DataHandler::extractEvents(decoded_items);
    std::cout << "Found " << events.size() << " events:\n";
    
    for (const auto& event : events) {
        std::cout << "  Event: " << event.event_name << "\n";
        for (const auto& [key, value] : event.parameters) {
            std::cout << "    " << key << " = ";
            printDataValue(value);
            std::cout << "\n";
        }
    }
}

// Example 3: Function calls
void example3_function_calls() {
    std::cout << "\n=== Example 3: Function Calls ===\n";
    
    DataHandler handler;
    
    // Create function calls
    FunctionCall add_call;
    add_call.function_name = "add";
    add_call.arguments.push_back(int64_t(10));
    add_call.arguments.push_back(int64_t(32));
    add_call.has_return = false;
    
    FunctionCall multiply_call;
    multiply_call.function_name = "multiply";
    multiply_call.arguments.push_back(5.5);
    multiply_call.arguments.push_back(2.0);
    multiply_call.has_return = false;
    
    FunctionCall status_call;
    status_call.function_name = "getStatus";
    status_call.has_return = false;
    
    // Create data items
    std::vector<DataItem> items;
    items.push_back(DataHandler::createFunctionCall("func1", add_call));
    items.push_back(DataHandler::createFunctionCall("func2", multiply_call));
    items.push_back(DataHandler::createFunctionCall("func3", status_call));
    
    // Encode/Decode
    auto buffer = handler.encodeDataBuffer(items);
    auto decoded_items = handler.decodeDataBuffer(buffer);
    
    std::cout << "Before processing:\n";
    for (const auto& item : decoded_items) {
        if (item.type == DataType::FUNCTION_CALL) {
            std::cout << "  " << item.function.function_name 
                      << " - has_return: " << item.function.has_return << "\n";
        }
    }
    
    // Process function calls
    handler.processFunctionCalls(decoded_items);
    
    std::cout << "\nAfter processing:\n";
    for (const auto& item : decoded_items) {
        if (item.type == DataType::FUNCTION_CALL) {
            std::cout << "  " << item.function.function_name << " = ";
            if (item.function.has_return) {
                printDataValue(item.function.return_value);
            } else {
                std::cout << "no return";
            }
            std::cout << "\n";
        }
    }
}

// Example 4: Image frames
void example4_image_frames() {
    std::cout << "\n=== Example 4: Image Frames ===\n";
    
    DataHandler handler;
    
    // Create mock image frames (1920x1080 RGB)
    ImageFrame frame1;
    frame1.width = 1920;
    frame1.height = 1080;
    frame1.channels = 3;
    frame1.timestamp_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    frame1.data.resize(1920 * 1080 * 3, 0x55); // Fill with pattern
    
    ImageFrame frame2;
    frame2.width = 640;
    frame2.height = 480;
    frame2.channels = 3;
    frame2.timestamp_ns = frame1.timestamp_ns + 1000000; // +1ms
    frame2.data.resize(640 * 480 * 3, 0xAA);
    
    // Create data items
    std::vector<DataItem> items;
    items.push_back(DataHandler::createImageFrame("camera_0", frame1));
    items.push_back(DataHandler::createImageFrame("camera_1", frame2));
    items.push_back(DataHandler::createInteger("frame_counter", 1234));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Encode
    auto buffer = handler.encodeDataBuffer(items);
    
    auto encode_end = std::chrono::high_resolution_clock::now();
    
    // Decode
    auto decoded_items = handler.decodeDataBuffer(buffer);
    
    auto decode_end = std::chrono::high_resolution_clock::now();
    
    auto encode_us = std::chrono::duration_cast<std::chrono::microseconds>(encode_end - start).count();
    auto decode_us = std::chrono::duration_cast<std::chrono::microseconds>(decode_end - encode_end).count();
    
    std::cout << "Total data size: " << buffer.size() << " bytes\n";
    std::cout << "Encode time: " << encode_us << " μs\n";
    std::cout << "Decode time: " << decode_us << " μs\n";
    
    // Extract images
    auto images = DataHandler::extractImages(decoded_items);
    std::cout << "\nFound " << images.size() << " images:\n";
    
    for (size_t i = 0; i < images.size(); i++) {
        const auto& img = images[i];
        std::cout << "  Image " << i << ": " 
                  << img.width << "x" << img.height << "x" << img.channels
                  << " (" << img.data.size() << " bytes, ts=" << img.timestamp_ns << ")\n";
    }
    
    // Calculate throughput
    double mb_per_frame = buffer.size() / (1024.0 * 1024.0);
    double encode_rate = (encode_us > 0) ? (1000000.0 / encode_us) : 0;
    double decode_rate = (decode_us > 0) ? (1000000.0 / decode_us) : 0;
    
    std::cout << "\nThroughput analysis:\n";
    std::cout << "  Data per frame: " << std::fixed << std::setprecision(2) << mb_per_frame << " MB\n";
    std::cout << "  Encode rate: " << std::fixed << std::setprecision(0) << encode_rate << " Hz\n";
    std::cout << "  Decode rate: " << std::fixed << std::setprecision(0) << decode_rate << " Hz\n";
    std::cout << "  At 1kHz: " << std::fixed << std::setprecision(2) 
              << (mb_per_frame * 1000.0) << " MB/s\n";
}

// Example 5: Mixed data
void example5_mixed_data() {
    std::cout << "\n=== Example 5: Mixed Data Exchange ===\n";
    
    DataHandler handler;
    
    // Create a realistic mixed payload
    std::vector<DataItem> items;
    
    // Robot state
    items.push_back(DataHandler::createInteger("robot_mode", 2));
    items.push_back(DataHandler::createReal("cycle_time", 0.001));
    items.push_back(DataHandler::createBoolean("estop_active", false));
    
    // Small image for vision feedback
    ImageFrame vision_frame;
    vision_frame.width = 320;
    vision_frame.height = 240;
    vision_frame.channels = 1; // Grayscale
    vision_frame.timestamp_ns = 123456789000;
    vision_frame.data.resize(320 * 240, 128);
    items.push_back(DataHandler::createImageFrame("vision", vision_frame));
    
    // Event
    Event alarm;
    alarm.event_name = "temperature_warning";
    alarm.parameters["sensor_id"] = int64_t(5);
    alarm.parameters["temperature"] = 78.5;
    items.push_back(DataHandler::createEvent("alarm", alarm));
    
    // Function call
    FunctionCall set_param;
    set_param.function_name = "setParameter";
    set_param.arguments.push_back(std::string("max_velocity"));
    set_param.arguments.push_back(1.5);
    items.push_back(DataHandler::createFunctionCall("set_vel", set_param));
    
    // Binary data (e.g., trajectory)
    std::vector<uint8_t> trajectory_data(1024);
    for (size_t i = 0; i < trajectory_data.size(); i++) {
        trajectory_data[i] = i & 0xFF;
    }
    items.push_back(DataHandler::createBinary("trajectory", trajectory_data));
    
    // Encode/Decode
    auto buffer = handler.encodeDataBuffer(items);
    auto decoded_items = handler.decodeDataBuffer(buffer);
    
    std::cout << "Mixed payload:\n";
    std::cout << "  Items: " << items.size() << "\n";
    std::cout << "  Total size: " << buffer.size() << " bytes\n";
    
    // Process functions
    handler.processFunctionCalls(decoded_items);
    
    // Show summary
    auto values = DataHandler::extractSimpleValues(decoded_items);
    auto events = DataHandler::extractEvents(decoded_items);
    auto functions = DataHandler::extractFunctions(decoded_items);
    auto images = DataHandler::extractImages(decoded_items);
    
    std::cout << "\nDecoded summary:\n";
    std::cout << "  Simple values: " << values.size() << "\n";
    std::cout << "  Events: " << events.size() << "\n";
    std::cout << "  Functions: " << functions.size() << "\n";
    std::cout << "  Images: " << images.size() << "\n";
}

int main() {
    try {
        example1_simple_data();
        example2_events();
        example3_function_calls();
        example4_image_frames();
        example5_mixed_data();
        
        std::cout << "\n=== All examples completed successfully ===\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}