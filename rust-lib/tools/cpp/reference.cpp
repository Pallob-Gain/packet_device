#include "DevicePacketParser.h"
#include <cassert>
#include <cstdio>
#include <vector>

static std::vector<uint8_t> captured;
static void capture(const uint8_t* bytes, uint16_t size) {
    captured.insert(captured.end(), bytes, bytes + size);
}
static void emit(const char* name) {
    std::printf("%s ", name);
    for (uint8_t byte : captured) std::printf("%02x", byte);
    std::puts("");
    captured.clear();
}
static int command_count = 0;
static void command() { ++command_count; }

int main() {
    std::puts("# Generated from the unchanged C++ parser using tools/cpp_reference.py.");
    DevicePacketParser parser(4096);
    parser.setStreamCaptureCallback(capture);
    parser.restOutStr("version", "Packet Device"); emit("string_version");
    parser.restOut<float>("amplitude", 5.0f); emit("scalar_amplitude");
    parser.restOut<uint8_t>("age", 20); emit("scalar_age");
    parser.restOut<int32_t>("signed", -123456); emit("scalar_signed");
    float values[] = {1.0f, 2.5f, -3.0f};
    parser.restArrayOut<float>("counts", values, 3); emit("array_counts");
    struct LockInInfo { float amplitude; float phase; } lock {5.0f, 0.5f};
    static_assert(sizeof(lock) == 8, "reference struct must match ESP32 layout");
    parser.restRawOut<LockInInfo>("ULX", &lock); emit("struct_ulx");
    struct MassiveData { char name[100]; float other_data[200]; } massive {};
    static_assert(sizeof(massive) == 900, "reference struct must match ESP32 layout");
    std::memcpy(massive.name, "Pallob Kumar Gain", sizeof("Pallob Kumar Gain") - 1);
    for (std::size_t i = 0; i < 200; ++i) massive.other_data[i] = static_cast<float>(i) / 2;
    parser.restRawOut<MassiveData>("mdt", &massive); emit("struct_massive");
    std::vector<uint8_t> large(600);
    for (std::size_t i = 0; i < large.size(); ++i) large[i] = static_cast<uint8_t>(i);
    parser.restArrayOut<uint8_t>("large", large.data(), static_cast<uint16_t>(large.size())); emit("array_600");
    parser.restOutStr("empty", "");
    assert(captured.empty());
    parser.setBufferMode(false);
    parser.restRawOut<LockInInfo>("ULX", &lock); emit("raw_delimited");
    parser.setInitialCRC(0xffff);
    parser.restOutStr("version", "Packet Device"); emit("text_custom_seed");
    parser.setVerifyCRC(false);
    parser.restArrayOut<float>("env", values, 3); emit("text_float_array");

    // Exercise current C++ CRC-disabled short command handling.
    parser.onReceive("X", command);
    const uint8_t short_command[] = {'<', 0, '-', 0, '*', 0, '-', 5, '>', 0x2a, 0x5e, 0, 1, 'X'};
    for (uint8_t byte : short_command) parser.processBytes(&byte, 1);
    assert(command_count == 1);
}
