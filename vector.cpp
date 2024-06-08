#include "vector.hpp"

#include <cstring>

namespace gerben {

[[noreturn]] void ThrowOutOfRange() {
#ifdef __cpp_exceptions
    throw std::out_of_range("Index out of range");
#else
    abort();
#endif
}

class DefaultAlloc : public MemResource {
    void* do_allocate(size_t bytes, size_t) override {
        return std::malloc(bytes);
    }
    void do_deallocate(void* p, size_t, size_t) override {
        std::free(p);
    }
    bool do_is_equal(MemResource const& other) const noexcept override {
        return this == &other;
    }
};

constinit DefaultAlloc def_alloc;


inline MemResource*& MemoryResource(void* ptr) {
    return static_cast<MemResource**>(ptr)[-1];
}

inline void* Alloc(MemResource* mr, size_t cap) noexcept {
    ___try {
        auto ptr = mr->allocate(cap + sizeof(std::max_align_t), sizeof(std::max_align_t));
        ptr = static_cast<std::byte*>(ptr) + sizeof(std::max_align_t);
        MemoryResource(ptr) = mr;
        return ptr;
    } ___catch(...) {
        abort();
    }
}

inline void Dealloc(MemResource* mr, void* base, size_t bytes) noexcept {
    mr->deallocate(static_cast<std::max_align_t*>(base) - 1, bytes + sizeof(std::max_align_t), sizeof(std::max_align_t));
}

inline size_t BufferSize(const void* begin, const void* end) {
    return static_cast<const char*>(end) - static_cast<const char*>(begin);
}

PmrBuffer PmrBuffer::GrowOutline(PmrBuffer buffer, void* end, Relocator relocate, size_t newcap) noexcept {
    if (buffer.end_cap == nullptr) {
        auto mr = static_cast<MemResource*>(buffer.base_or_mr);
        if (mr == nullptr) mr = &def_alloc;
        buffer.base_or_mr = Alloc(mr, newcap);
    } else {
        auto cap = BufferSize(buffer.base_or_mr, buffer.end_cap);
        newcap = std::max<uint32_t>(newcap, cap * 2);
        auto mr = MemoryResource(buffer.base_or_mr);
        auto newbase = Alloc(mr, newcap);
        if (relocate) {
            relocate(newbase, buffer.base_or_mr, end);
        } else {
            std::memcpy(newbase, buffer.base_or_mr, BufferSize(buffer.base_or_mr, end));
        }
        Dealloc(mr, buffer.base_or_mr, cap);
        buffer.base_or_mr = newbase;
    }
    buffer.end_cap = static_cast<char*>(buffer.base_or_mr) + newcap;
    return buffer;
}

void PmrBuffer::FreeOutline(PmrBuffer buffer) noexcept {
    auto bytes = BufferSize(buffer.end_cap, buffer.base_or_mr);
    auto mr = MemoryResource(buffer.base_or_mr);
    Dealloc(mr, buffer.base_or_mr, bytes);
}

}