#include "data_handler.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace shm_protocol;
using namespace asn1;

void test_simple_data() {
    std::cout << "Testing simple data types... ";

    DataHandler handler;

    // Create data items with all basic types
    std::vector<DataItem> items;
    items.push_back(DataHandler::createInteger("count", 42));
    items.push_back(DataHandler::createInteger("sensor_reading", -15));
    items.push_back(DataHandler::createBoolean("enabled", true));
    items.push_back(DataHandler::createString("status", "OK"));
    items.push_back(DataHandler::createReal("temperature", 23.5));
    items.push_back(DataHandler::createReal("pressure", 101.325));

    // Encode
    auto buffer = handler.encodeDataBuffer(items);
    assert(buffer.size() > 0);

    // Decode
    auto decoded = handler.decodeDataBuffer(buffer);
    assert(decoded.size() == 6);

    // Verify values
    auto values = DataHandler::extractSimpleValues(decoded);
    assert(std::get<int64_t>(values["count"]) == 42);
    assert(std::get<int64_t>(values["sensor_reading"]) == -15);
    assert(std::get<bool>(values["enabled"]) == true);
    assert(std::get<std::string>(values["status"]) == "OK");
    assert(std::abs(std::get<double>(values["temperature"]) - 23.5) < 0.0001);
    assert(std::abs(std::get<double>(values["pressure"]) - 101.325) < 0.0001);

    std::cout << "PASSED\n";
}

void test_events() {
    std::cout << "Testing events... ";

    DataHandler handler;

    Event alarm;
    alarm.event_name = "temperature_warning";
    alarm.parameters["sensor_id"] = int64_t(5);
    alarm.parameters["alert_level"] = int64_t(3);
    alarm.parameters["message"] = std::string("High temperature detected");

    std::vector<DataItem> items;
    items.push_back(DataHandler::createEvent("alarm1", alarm));

    auto buffer = handler.encodeDataBuffer(items);
    auto decoded = handler.decodeDataBuffer(buffer);

    auto events = DataHandler::extractEvents(decoded);
    assert(events.size() == 1);
    assert(events[0].event_name == "temperature_warning");
    assert(std::get<int64_t>(events[0].parameters["sensor_id"]) == 5);
    assert(std::get<int64_t>(events[0].parameters["alert_level"]) == 3);
    assert(std::get<std::string>(events[0].parameters["message"]) == "High temperature detected");

    std::cout << "PASSED\n";
}

void test_function_calls() {
    std::cout << "Testing function calls... ";

    DataHandler handler;

    FunctionCall add_call;
    add_call.function_name = "add";
    add_call.arguments.push_back(int64_t(10));
    add_call.arguments.push_back(int64_t(32));
    add_call.has_return = false;

    std::vector<DataItem> items;
    items.push_back(DataHandler::createFunctionCall("func1", add_call));

    auto buffer = handler.encodeDataBuffer(items);
    auto decoded = handler.decodeDataBuffer(buffer);

    // Process functions
    handler.processFunctionCalls(decoded);

    assert(decoded[0].function.has_return == true);
    assert(std::get<int64_t>(decoded[0].function.return_value) == 42);

    std::cout << "PASSED\n";
}

void test_image_frames() {
    std::cout << "Testing image frames... ";

    DataHandler handler;

    ImageFrame frame;
    frame.width = 640;
    frame.height = 480;
    frame.channels = 3;
    frame.timestamp_ns = 123456789;
    frame.data.resize(640 * 480 * 3, 0xAA);

    std::vector<DataItem> items;
    items.push_back(DataHandler::createImageFrame("camera1", frame));

    auto buffer = handler.encodeDataBuffer(items);
    auto decoded = handler.decodeDataBuffer(buffer);

    auto images = DataHandler::extractImages(decoded);
    assert(images.size() == 1);
    assert(images[0].width == 640);
    assert(images[0].height == 480);
    assert(images[0].channels == 3);
    assert(images[0].timestamp_ns == 123456789);
    assert(images[0].data.size() == 640 * 480 * 3);

    std::cout << "PASSED\n";
}

void test_mixed_data() {
    std::cout << "Testing mixed data... ";

    DataHandler handler;

    std::vector<DataItem> items;
    items.push_back(DataHandler::createInteger("mode", 2));
    items.push_back(DataHandler::createInteger("cycle_count", 1000));

    Event event;
    event.event_name = "alert";
    event.parameters["level"] = int64_t(3);
    items.push_back(DataHandler::createEvent("evt1", event));

    FunctionCall func;
    func.function_name = "add";
    func.arguments.push_back(int64_t(100));
    func.arguments.push_back(int64_t(200));
    items.push_back(DataHandler::createFunctionCall("func1", func));

    auto buffer = handler.encodeDataBuffer(items);
    auto decoded = handler.decodeDataBuffer(buffer);

    handler.processFunctionCalls(decoded);

    assert(decoded.size() == 4);
    assert(decoded[3].function.has_return == true);
    // Check add result
    int64_t result = std::get<int64_t>(decoded[3].function.return_value);
    assert(result == 300);

    std::cout << "PASSED\n";
}

int main() {
    try {
        std::cout << "=== Data Handler Tests ===\n";

        test_simple_data();
        test_events();
        test_function_calls();
        test_image_frames();
        test_mixed_data();

        std::cout << "\nAll tests PASSED!\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << "\n";
        return 1;
    }
}
