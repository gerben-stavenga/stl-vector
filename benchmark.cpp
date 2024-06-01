#include "vector.hpp"

#include <vector>

#include "benchmark/benchmark.h"
#include "google/protobuf/repeated_field.h"

template <typename T>
struct ProtoVec {
    template <typename U>
    void push_back(U&& x) { rep_field_.Add(std::forward<U>(x)); }
    void clear() { rep_field_.Clear(); }
    const T* data() { return rep_field_.data(); }

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
void BM_PushBack(benchmark::State& state) {
    T x;
    for (auto _ : state) {
        Add(10000, &x);
        x.clear();
        benchmark::DoNotOptimize(x.data());
    }
}

BENCHMARK_TEMPLATE(BM_PushBack, gerben::Vec<int>);
BENCHMARK_TEMPLATE(BM_PushBack, std::vector<int>);
BENCHMARK_TEMPLATE(BM_PushBack, ProtoVec<int>);

template <typename T>
void BM_PushBackLocalCapture(benchmark::State& state) {
    T x;
    for (auto _ : state) {
        AddLocalCapture(10000, &x);
        x.clear();
        benchmark::DoNotOptimize(x.data());
    }
}

BENCHMARK_TEMPLATE(BM_PushBackLocalCapture, gerben::Vec<int>);
BENCHMARK_TEMPLATE(BM_PushBackLocalCapture, std::vector<int>);
BENCHMARK_TEMPLATE(BM_PushBackLocalCapture, ProtoVec<int>);
