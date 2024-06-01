#include "vector.hpp"

#include <cstdint>

namespace gerben {

std::pair<void*, uint32_t> VecBase::GrowOutline(void* base, uint32_t size, uint32_t cap, uint32_t elem_size, Relocator relocate, uint32_t newcap) noexcept {
    if (cap == 0) {
        newcap = std::max<uint32_t>(newcap, 1);
        auto newbase = malloc(newcap * elem_size);
        return {newbase, newcap};
    }
    newcap = std::max<uint32_t>(newcap, cap * 2);
    auto newbase = malloc(newcap * elem_size);
    if (newbase == nullptr) abort();
    if (relocate) {
        relocate(newbase, base, size);
    } else {
        std::memcpy(newbase, base, size * elem_size);
    }
    free(base);
    return {newbase, newcap};
}

}