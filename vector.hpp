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

#if 1

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

class VecBase {
public:
    constexpr uint32_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size() == 0; }
    constexpr uint32_t capacity() const noexcept { return cap_; }

protected:
    constexpr VecBase() noexcept = default;
    constexpr VecBase(MemResource* mr) noexcept : base_or_mr_(mr) {};
    VecBase(VecBase&& other) noexcept : VecBase() {
        Swap(other);
    }
    VecBase& operator=(VecBase&& other) noexcept {
        Swap(other);
        return *this;       
    }

    template <typename T>
    void Free() {
        if (cap_ != 0) FreeOutline(base_or_mr_, cap_ * sizeof(T));
    }

    template<typename T>
    T* Base() const { return static_cast<T*>(base_or_mr_); }

    using Relocator = void (*)(void* dst, void *src, uint32_t size) noexcept;

    template <typename T>
    static void Relocate(void* dst, void* src, uint32_t size) noexcept {
        auto d = static_cast<T*>(dst);
        auto s = static_cast<T*>(src);
        for (uint32_t i = 0; i < size; i++) {
            T tmp = std::move(s[i]);
            s[i].~T();
            new (d + i) T(std::move(tmp));
        }
    }

    void Swap(VecBase& other) noexcept {
        std::swap(base_or_mr_, other.base_or_mr_);
        std::swap(size_, other.size_);
        std::swap(cap_, other.cap_);
    }

    template <typename T>
    void AddAlreadyReserved(T x) noexcept {
        auto s = size_;
        new (Base<T>() + s) T(std::move(x));
        size_ = s + 1;
    }

    template <typename T>
    void Add(T x) noexcept {
        auto s = size_;
        auto c = cap_;
        if (s >= c) {
            Grow<T>();
        }
        new (Base<T>() + s) T(std::move(x));
        size_ = s + 1;
    }
    template <typename T>
    T Remove() noexcept {
        auto p = Base<T>();
        auto s = size_ - 1;
        T res = std::move(p[s]);
        p[s].~T();
        size_ = s;
        return res;
    }
    template <typename T>
    void Reserve(uint32_t newcap) noexcept {
        if (newcap > cap_) {
            Grow<T>(newcap);
        }
    }
    constexpr void SetSize(uint32_t s) noexcept { size_ = s; }

    template <typename T>
    void Grow(uint32_t newcap = 0) noexcept {
        Relocator mover = nullptr;
        if constexpr (!is_relocatable_v<T>) {
            mover = &Relocate<T>;
        }
        std::tie(base_or_mr_, cap_) = GrowOutline(base_or_mr_, size_, cap_, sizeof(T), mover, newcap);
    }

private:
    static std::pair<void*, uint32_t> GrowOutline(void* base, uint32_t size, uint32_t cap, uint32_t elem_size, Relocator relocate, uint32_t newcap) noexcept;
    static void FreeOutline(void* base, size_t bytes);

    // If cap_ is 0 it's a memory resource otherwise it's pointing to base of buffer
    void* base_or_mr_ = nullptr;
    uint32_t size_ = 0;
    uint32_t cap_ = 0;
};

template <typename T>
concept IsNoThrowMoveConstructible = std::is_nothrow_move_constructible_v<T>;

template <IsNoThrowMoveConstructible T>
struct Vec : public VecBase {
    constexpr Vec() noexcept = default;
    ~Vec() noexcept {
        clear();
        Free<T>();
    }

    constexpr Vec(Vec&&) noexcept = default;
    constexpr Vec& operator=(Vec&& other) noexcept = default;

    template <typename U>
    Vec(const std::initializer_list<U>& list) {
        reserve(list.size());
        for (auto& x : list) AddAlreadyReserved<T>(x);
    }

    template <typename U>
    Vec(uint32_t n, U x) { 
        reserve(n);
        for (uint32_t i = 0; i < n; i++) AddAlreadyReserved<T>(x);
    }

    constexpr T* data() noexcept { return Base<T>(); }
    constexpr T const* data() const noexcept  { return Base<T>(); }

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
    constexpr auto crend() const noexcept  { return rend; }


    void reserve(uint32_t newcap) noexcept { return Reserve<T>(newcap); }

    template <typename U>
    void push_back(U x) noexcept { Add<T>(std::move(x)); }

    T pop_back() noexcept { return Remove<T>(); }

    void clear() noexcept { for (auto& x : *this) x.~T(); SetSize(0); }
    void swap(Vec& other) noexcept { Swap(other); }
    void resize(uint32_t s) noexcept {
        if (s <= size()) {
            for (auto& x : Postfix(s)) x.~T();            
        } else {
            Reserve(s);
            auto p = data();
            for (uint32_t i = size(); i < s; i++) new (p + i) T();
        }
        SetSize(s);
    }
    void resize(uint32_t s, const T& value) {
        if (s <= size()) {
            for (auto& x : Postfix(s)) x.~T();            
        } else {
            Reserve(s);
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
