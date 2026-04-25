#pragma once
#include <chrono>
#include <ctime>
#include <string>

namespace FreeAI {
namespace Utils {

    // Time shortcuts
    inline auto Now() { return std::chrono::steady_clock::now(); }
    
    inline auto NowEpoch() { return std::chrono::system_clock::now(); }
    
    inline std::string NowFormatted(const char* fmt = "%Y-%m-%d %H:%M:%S") {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        char buf[64];
        std::strftime(buf, sizeof(buf), fmt, &tm);
        return std::string(buf);
    }
    
    template<typename T1, typename T2>
    inline int64_t DurationMs(T1 start, T2 end) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    }
    
    template<typename T1, typename T2>
    inline int64_t DurationUs(T1 start, T2 end) {
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }

} // namespace Utils
} // namespace FreeAI
