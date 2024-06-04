#include "vector.hpp"

#include <cstdint>

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

inline void* Alloc(MemResource* mr, size_t cap, size_t elem_size) noexcept {
    ___try {
        auto ptr = mr->allocate(cap *  elem_size + sizeof(std::max_align_t), sizeof(std::max_align_t));
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

std::pair<void*, uint32_t> VecBase::GrowOutline(void* base, uint32_t size, uint32_t cap, uint32_t elem_size, Relocator relocate, uint32_t newcap) noexcept {
    if (cap == 0) {
        auto mr = static_cast<MemResource*>(base);
        if (mr == nullptr) mr = &def_alloc;
        newcap = std::max<uint32_t>(newcap, 1);
        auto newbase = Alloc(mr, newcap, elem_size);
        return {newbase, newcap};
    } else {
        auto mr = MemoryResource(base);
        newcap = std::max<uint32_t>(newcap, cap * 2);
        auto newbase = Alloc(mr, newcap, elem_size);
        if (relocate) {
            relocate(newbase, base, size);
        } else {
            std::memcpy(newbase, base, size * elem_size);
        }
        Dealloc(mr, base, cap * elem_size);
        return {newbase, newcap};
    }
}

void VecBase::FreeOutline(void* base, size_t bytes) {
    auto mr = MemoryResource(base);
    Dealloc(mr, base, bytes);
}

}