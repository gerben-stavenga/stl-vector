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

template <typename T>
__attribute__((noinline))
void Add(int n, T* x) {
    for (int i = 0; i < n; i++) x->push_back(i);
}

template <typename T>
__attribute__((noinline))
void AddLocalCapture(int n, T* x) {
    gerben::LocalCapture y(x);
    for (int i = 0; i < n; i++) y.push_back(i);
}

template <typename T>
__attribute__((noinline))
void PopPush(T* from, T* to) {
    while (!from->empty()) {
        auto tmp = std::move(from->back());
        from->pop_back();
        to->push_back(std::move(tmp));
    }
}

template <typename T>
__attribute__((noinline))
void PopPushLocalCapture(T* x, T* y) {
    gerben::LocalCapture from(x);
    gerben::LocalCapture to(y);
    while (!from.empty()) {
        auto tmp = std::move(from.back());
        from.pop_back();
        to.push_back(std::move(tmp));
    }
}

enum LocalCapture {
    kNoCapture,
    kLocalCapture
};

auto dummy = []() {
#define xstr(s) str(s)
#define str(s) #s
    std::printf("Libc++ version: %s\n", xstr(_LIBCPP_VERSION));
    return 0;
}();

template <typename T, LocalCapture capture>
void BM_PushBack(benchmark::State& state) {
    T x;
    for (auto _ : state) {
        if (capture == kLocalCapture) {
            AddLocalCapture(10000, &x);
        } else {
            Add(10000, &x);
        }
        x.clear();
        benchmark::DoNotOptimize(x.data());
    }
}

BENCHMARK_TEMPLATE(BM_PushBack, gerben::Vec<int>, kNoCapture);
BENCHMARK_TEMPLATE(BM_PushBack, std::vector<int>, kNoCapture);
BENCHMARK_TEMPLATE(BM_PushBack, ProtoVec<int>, kNoCapture);
BENCHMARK_TEMPLATE(BM_PushBack, gerben::Vec<int>, kLocalCapture);
BENCHMARK_TEMPLATE(BM_PushBack, std::vector<int>, kLocalCapture);
BENCHMARK_TEMPLATE(BM_PushBack, ProtoVec<int>, kLocalCapture);

template <typename T, LocalCapture capture>
void BM_PopPush(benchmark::State& state) {
    T x;
    T y;
    for (int i = 0; i < 10000; i++) x.push_back(i);
    for (auto _ : state) {
        if (capture == kLocalCapture) {
            PopPushLocalCapture(&x, &y);
        } else {
            PopPush(&x, &y);
        }
        x.swap(y);
        benchmark::DoNotOptimize(x.data());
    }
}

BENCHMARK_TEMPLATE(BM_PopPush, gerben::Vec<int>, kNoCapture);
BENCHMARK_TEMPLATE(BM_PopPush, std::vector<int>, kNoCapture);
BENCHMARK_TEMPLATE(BM_PopPush, ProtoVec<int>, kNoCapture);
BENCHMARK_TEMPLATE(BM_PopPush, gerben::Vec<int>, kLocalCapture);
BENCHMARK_TEMPLATE(BM_PopPush, std::vector<int>, kLocalCapture);
BENCHMARK_TEMPLATE(BM_PopPush, ProtoVec<int>, kLocalCapture);
