#include "PageBuffer.h"
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pixel::memory {
size_t SystemPageSize() {
    static const size_t page = [] {
#if defined(_WIN32)
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const auto result = size_t(info.dwPageSize);
#else
        const auto native = sysconf(_SC_PAGESIZE);
        if (native <= 0) throw std::runtime_error("cannot query memory page size");
        const auto result = size_t(native);
#endif
        if (!std::has_single_bit(result)) throw std::runtime_error("unsupported memory page size");
        return result;
    }();
    return page;
}
} // namespace pixel::memory
