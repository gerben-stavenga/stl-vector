#include "vector.hpp"

#include <vector>

#include "benchmark/benchmark.h"
#include "google/protobuf/repeated_field.h"

template <typename T>
struct ProtoVec {
    ProtoVec() = default;
    ProtoVec(ProtoVec&& other) : ProtoVec() {
        swap(other);
    } 
    template <typename U>
    void push_back(U&& x) { rep_field_.Add(std::forward<U>(x)); }
    void pop_back() { rep_field_.RemoveLast(); }
    auto& back() const { return rep_field_[rep_field_.size() - 1]; }
    void clear() { rep_field_.Clear(); }
    const T* data() { return rep_field_.data(); }
    void swap(ProtoVec& other) {
        constexpr int n = sizeof rep_field_;
        char buffer[n];
        std::memcpy(buffer, &rep_field_, n);
        std::memcpy(&rep_field_, &other.rep_field_, n);
        std::memcpy(&other.rep_field_, buffer, n);
    }
    bool empty() const { return rep_field_.empty(); }

    google::protobuf::RepeatedField<T> rep_field_;
};

template <bool, typename T>
struct MaybeLocalCapture;

template <typename T>
struct MaybeLocalCapture<true, T> : gerben::LocalCapture<T> {};

template <typename T>
struct NoCapture {
    T* global_;
    NoCapture(T* x) : global_(x) {}

    auto operator->() { return global_; }
    auto operator->() const { return global_; }
};

template <typename T>
struct MaybeLocalCapture<false, T> : NoCapture<T> {};

template <bool kLocalCapture, typename T>
__attribute__((noinline))
void Add(int n, T* x) {
    MaybeLocalCapture<kLocalCapture, T> y(x);
    for (int i = 0; i < n; i++) y->push_back(i);
}

template <bool kLocalCapture, typename T>
__attribute__((noinline))
void PopPush(T* x, T* y) {
    MaybeLocalCapture<kLocalCapture, T> from(x);
    MaybeLocalCapture<kLocalCapture, T> to(y);
    while (!from->empty()) {
        auto tmp = std::move(from->back());
        from->pop_back();
        to->push_back(std::move(tmp));
    }
}

enum LocalCapture {
    kNoCapture,
    kLocalCapture
};

auto dummy = []() {
    const char kCompilerVersion[] = 
#define xstr(s) str(s)
#define str(s) #s
#ifdef __clang__
        "clang " xstr(__clang_major__) "." xstr(__clang_minor__) "." xstr(__clang_patchlevel__);
#elif defined(__GNUC__)
        "gcc " xstr(__GNUC__) "." xstr(__GNUC_MINOR__);
#else
        "unknown";
#endif
    const char kStdLib[] =
#ifdef _LIBCPP_VERSION
        "libc++ version:" xstr(_LIBCPP_VERSION);
#elif defined(__GLIBCXX__)
        "stdlibc++ version: " xstr(__GLIBCXX__);
#else
        "unknown stdlib"
#endif
    std::printf("Build with %s and %s\n", kCompilerVersion, kStdLib);
    return 0;
}();

template <template <typename... U> class T, LocalCapture capture>
void BM_PushBack(benchmark::State& state) {
    T<int> x;
    for (auto _ : state) {
        Add<capture == kLocalCapture>(10000, &x);
        benchmark::DoNotOptimize(x.data());
        x.clear();
    }
}

BENCHMARK_TEMPLATE(BM_PushBack, gerben::Vec, kNoCapture);
BENCHMARK_TEMPLATE(BM_PushBack, std::vector, kNoCapture);
BENCHMARK_TEMPLATE(BM_PushBack, ProtoVec, kNoCapture);
BENCHMARK_TEMPLATE(BM_PushBack, gerben::Vec, kLocalCapture);
BENCHMARK_TEMPLATE(BM_PushBack, std::vector, kLocalCapture);
BENCHMARK_TEMPLATE(BM_PushBack, ProtoVec, kLocalCapture);

template <template <typename... U> class T, LocalCapture capture>
void BM_PopPush(benchmark::State& state) {
    T<int> x;
    T<int> y;
    for (int i = 0; i < 10000; i++) x.push_back(i);
    for (auto _ : state) {
        PopPush<capture == kLocalCapture>(&x, &y);
        benchmark::DoNotOptimize(y.data());
        x.swap(y);
    }
}

BENCHMARK_TEMPLATE(BM_PopPush, gerben::Vec, kNoCapture);
BENCHMARK_TEMPLATE(BM_PopPush, std::vector, kNoCapture);
BENCHMARK_TEMPLATE(BM_PopPush, ProtoVec, kNoCapture);
BENCHMARK_TEMPLATE(BM_PopPush, gerben::Vec, kLocalCapture);
BENCHMARK_TEMPLATE(BM_PopPush, std::vector, kLocalCapture);
BENCHMARK_TEMPLATE(BM_PopPush, ProtoVec, kLocalCapture);
