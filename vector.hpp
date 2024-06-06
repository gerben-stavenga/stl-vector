#include <type_traits>
#include <cstdint>
#include <tuple>
#include <new>
#include <vector>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <string>

#ifdef __cpp_exceptions
#define __try try
#define __catch(x) catch(x)
#define __rethrow throw
#else
#define __try if (true)
#define __catch(x) if (false)
#define __rethrow 0
#endif

namespace gerben {

template <typename T>
inline constexpr bool is_known_relocatable_v = false;

template <typename T>
inline constexpr bool is_known_relocatable_v<std::unique_ptr<T>> = true;
template <typename T>
inline constexpr bool is_known_relocatable_v<std::vector<T>> = true;
template <>
inline constexpr bool is_known_relocatable_v<std::string> = true;

template <typename T>
inline constexpr bool is_relocatable_v = std::is_pod_v<T> || is_known_relocatable_v<T>;

[[noreturn]] void ThrowOutOfRange();

#if 0

using MemResource = std::pmr::memory_resource;

#else

class MemResource {
public:
    void* allocate(size_t bytes, size_t alignment) {
        return do_allocate(bytes, alignment);
    }
    void deallocate(void* ptr, size_t bytes, size_t alignment) {
        return do_deallocate(ptr, bytes, alignment);
    }

private:
    virtual void* do_allocate(size_t bytes, size_t alignment) = 0;
    virtual void do_deallocate(void* ptr, size_t bytes, size_t alignment) = 0;
    virtual bool do_is_equal(MemResource const& other) const noexcept = 0;
};

#endif

struct PmrBuffer {
    constexpr PmrBuffer() = default;
    constexpr PmrBuffer(MemResource* mr) noexcept : base_or_mr(mr) {};

    template <typename T>
    void Free() {
        if (end_cap != nullptr) FreeOutline(*this);
    }

    template<typename T>
    T* Base() const { return static_cast<T*>(base_or_mr); }

    using Relocator = void (*)(void* dst, void *src, void* end) noexcept;

    template <typename T>
    static void Relocate(void* dst, void* src, void* end) noexcept {
        auto d = static_cast<T*>(dst);
        auto s = static_cast<T*>(src);
        auto e = static_cast<T*>(end);

        for (; s != e; ++s, ++d) {
            T tmp = std::move(*s);
            *s.~T();
            new (d) T(std::move(tmp));
        }
    }

    template <typename T>
    T* Add(T* pos, T x) {
        if (pos >= end_cap) {
            pos = Grow(pos, sizeof(T));
        }
        new (pos) T(std::move(x));
        return pos + 1;
    }

    template <typename T>
    T* Grow(T* end, size_t newcap) noexcept {
        Relocator mover = nullptr;
        if constexpr (!is_relocatable_v<T>) {
            mover = &Relocate<T>;
        }
        auto diff = end - Base<T>();
        *this = GrowOutline(*this, end, mover, newcap);
        return Base<T>() + diff;
    }

    static PmrBuffer GrowOutline(PmrBuffer buffer, void* end, Relocator relocate, size_t newcap) noexcept;
    static void FreeOutline(PmrBuffer buffer) noexcept;


    void* base_or_mr = nullptr;
    void* end_cap = nullptr;
};

template <typename T>
concept IsNoThrowMoveConstructible = std::is_nothrow_move_constructible_v<T>;

template <IsNoThrowMoveConstructible T>
class Vec {
    PmrBuffer buffer_;
    T* end_ = nullptr;

    T* end_cap() const { return static_cast<T*>(buffer_.end_cap); }

public:
    constexpr Vec() noexcept = default;
    __attribute__((always_inline))
    ~Vec() noexcept {
        clear();
        buffer_.Free<T>();
    }

    constexpr Vec(Vec&& other) noexcept { swap(other); }
    constexpr Vec& operator=(Vec&& other) noexcept { swap(other); }

    template <typename U>
    Vec(const std::initializer_list<U>& list) : Vec() {
        reserve(list.size());
        for (auto& x : list) AddAlreadyReserved<T>(x);
    }

    template <typename U>
    Vec(uint32_t n, U x) : Vec() { 
        reserve(n);
        for (uint32_t i = 0; i < n; i++) AddAlreadyReserved<T>(x);
    }

    void swap(Vec& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(end_, other.end_);
    }

    constexpr size_t size() const { return end_ - data(); }
    constexpr bool empty() const { return end_ == data(); }
    constexpr size_t capacity() const {
        return end_cap() == nullptr ? 0 : end_cap() - data();
    }

    constexpr T* data() noexcept { return buffer_.Base<T>(); }
    constexpr T const* data() const noexcept  { return buffer_.Base<T>(); }

    constexpr T* begin() noexcept  { return data(); }
    constexpr T const* begin() const noexcept  { return data(); }
    constexpr T* end() noexcept { return data() + size(); }
    constexpr T const* end() const noexcept  { return data() + size(); }

    constexpr auto cbegin() const noexcept  { return begin(); }
    constexpr auto cend() noexcept  { return end(); }

    constexpr auto rbegin() noexcept  { return std::reverse_iterator(end()); }
    constexpr auto rbegin() const noexcept  { return std::reverse_iterator(end()); }
    constexpr auto rend() noexcept  { return std::reverse_iterator(begin()); }
    constexpr auto rend() const noexcept  { return std::reverse_iterator(begin()); }
    constexpr auto crbegin() const noexcept  { return rbegin(); }
    constexpr auto crend() const noexcept  { return rend(); }

    void reserve(size_t newcap) noexcept { 
        if (newcap > capacity()) Grow<T>(end_, newcap);
    }

    void push_back(const T& x) { end_ = buffer_.Add<T>(end_, x); }
    void push_back(T&& x) { end_ = buffer_.Add<T>(end_, std::move(x)); }

    T pop_back() {
        auto e = end_ - 1; 
        T res = std::move(*e);
        e->~T();
        end_ = e;
        return res;
    }

    void clear() { 
        for (auto& x : *this) x.~T();
        end_ = data();
    }

    void resize(size_t s) {
        if (s <= size()) {
            for (auto& x : Postfix(s)) x.~T();            
        } else {
            reserve(s);
            auto p = data();
            for (uint32_t i = size(); i < s; i++) new (p + i) T();
        }
        SetSize(s);
    }

    void resize(uint32_t s, const T& value) {
        if (s <= size()) {
            for (auto& x : Postfix(s)) x.~T();            
        } else {
            reserve(s);
            auto p = data();
            for (uint32_t i = size(); i < s; i++) {
                __try {
                    new (p + i) T(value);
                } __catch (...) {
                    SetSize(i);
                    __rethrow;
                }
            }
        }
        SetSize(s);
    }

    template <typename It>
    void assign(It first, It last) noexcept {
        uint32_t idx = 0;
        for (auto &x : *this) {
            if (first == last) {
                resize(idx);
                return;
            }
            x = *first;
            ++first; ++idx;
        }
        for (; first != last; ++first) push_back(*first);
    }
    // At is specified to throw
    auto at(uint32_t idx) { if (idx >= size()) ThrowOutOfRange(); return Get(idx); }
    auto at(uint32_t idx) const { if (idx >= size()) ThrowOutOfRange(); return Get(idx); }

    T& operator[](uint32_t idx) noexcept { return data()[idx]; }
    T const& operator[](uint32_t idx) const noexcept { return data()[idx]; }

    auto front() noexcept { return Get(0); }
    auto front() const noexcept { return Get(0); }
    auto back() noexcept { return Get(size() - 1); }
    auto back() const noexcept { return Get(size() - 1); }

    void shrink_to_fit(uint32_t) noexcept {}

    T* erase(T* first) noexcept {
        return erase(first, first + 1);
    }

    T* erase(T* first, T* last) noexcept {
        auto d = last - first;
        auto ret = first;
        if (d == 0) return;
        auto e = end();
        while (last != e) {
            *first = std::move(*last);
            ++first; ++last;
        }
        while (first != e) {
            first->~T();
            ++first;
        }
        SetSize(size() - d);
        return ret;
    }
    void insert(T* position, T res) noexcept {
        auto i = position - data();
        auto s = size();
        push_back(std::move(res));
        std::rotate(data() + i, data() + s, data() + size());
    }
    void insert(T* position, uint32_t n, const T& res) noexcept {
        auto i = position - data();
        auto s = size();
        reserve(n + s);
        while (n--) AddAlreadyReserved<T>(res);
        std::rotate(data() + i, data() + s, data() + size());
    }
    template <class InputIterator>
    void insert(T* position, InputIterator first, InputIterator last) noexcept {
        auto s = size();
        auto i = position - data();
        while (first != last) { push_back(*first); ++first; }
        std::rotate(data() + i, data() + s, data() + size());
    }
    template <typename... Args>
    void emplace(T* position, Args&&... args) noexcept {
        insert(position, T(std::forward<Args>(args)...));
    }
    template <typename... Args>
    void emplace_back(T* position, Args&&... args) noexcept {
        push_back(T(std::forward<Args>(args)...));
    }

    T& Get(uint32_t idx) noexcept { return data()[idx]; }
    T const& Get(uint32_t idx) const noexcept { return data()[idx]; }

    std::span<T> Prefix(uint32_t idx) { return {data(), idx}; }
    std::span<T const> Prefix(uint32_t idx) const { return {data(), idx}; }
    std::span<T> Postfix(uint32_t idx) { return {data() + idx, size() - idx}; }
    std::span<T const> Postfix(uint32_t idx) const { return {data() + idx, size() - idx}; }

    void SetSize(size_t s) { end_ = data() + s; } 
};

template <typename T>
inline constexpr bool is_known_relocatable_v<Vec<T>> = true;

template <typename T>
class LocalCapture : public T {
    T* global_;
public:
    __attribute__((always_inline))
    LocalCapture(T* global) noexcept : T(std::move(*global)), global_(global) {
        global->~T();
    }
    __attribute__((always_inline))
    ~LocalCapture() noexcept {
        new (global_) T(std::move(*static_cast<T*>(this)));
    }
};

}  // namespace
