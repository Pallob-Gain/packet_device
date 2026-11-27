#pragma once
// Minimal host shim used ONLY by the C++ protocol reference harness.
// It supplies the Arduino String operations the parser needs. FreeRTOS and
// hardware drivers are intentionally not emulated or tested here.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

using SemaphoreHandle_t = void*;

class String {
    std::string value_;
public:
    String() = default;
    String(const char* s) : value_(s ? s : "") {}
    String(const char* s, std::size_t n) : value_(s, n) {}
    String(const uint8_t* s, std::size_t n)
        : value_(reinterpret_cast<const char*>(s), n) {}
    String(const std::string& s) : value_(s) {}
    template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    String(T v) : value_(std::to_string(v)) {}
    template<class T, typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
    String(T v, unsigned decimals = 2) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(decimals) << v;
        value_ = s.str();
    }
    template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    String(T v, unsigned) : value_(std::to_string(v)) {}
    std::size_t length() const { return value_.size(); }
    const char* c_str() const { return value_.c_str(); }
    char operator[](std::size_t i) const { return value_[i]; }
    String substring(std::size_t first, std::size_t last = std::string::npos) const {
        return value_.substr(first, last == std::string::npos ? last : last - first);
    }
    String& operator+=(const String& other) { value_ += other.value_; return *this; }
    friend String operator+(const String& a, const String& b) { return a.value_ + b.value_; }
    friend bool operator<(const String& a, const String& b) { return a.value_ < b.value_; }
};
