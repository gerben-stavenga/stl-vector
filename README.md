The standard implementations of std::vector often result in bloated and in cases very inefficient code. This repo presents a substitute implementation, that should cover almost all cases, that vastly reduces code bloat and produces equivalent or in some cases vastly superior performance. A few points of note
1) The code only works for no-throw move constructible types (this should cover all sane types you would want to put in vectors) and does never throw exceptions. In particular, it does not attempt to throw exceptions on memory allocation failure and instead aborts.
2) Instead of using `size_t` for size and capacity, this code uses `uint32_t`. A limit of ~4 billion elements suffices for all but the rarest cases and the size reduction from 24 bytes to a nice power-of-two 16 bytes can be a substantial performance enhancement.

The biggest code bloat due to `std::vector` comes from exceptions crap and inlining fallback paths related to growing the capacity of the backing buffer. The exception code can just be discarded, types with throwing move constructors are unreasonable and fighting out-of-memory conditions is a fools errant in all but the rarest cases. The excesive inlining of fallback code is likely done because of these two reasons: 
1) Because growing the buffer requires relocating generically typed elements, the buffer growing code has to be templated at some level. This means it cannot be completely be done in external .cc files and has to be generated in the translation unit (TU) of the using code.
2) If the buffer growing is done by code outside of the function, than typically the `this` pointer of the vector escapes. This drastically reduces the compilers (aliasing) analysis capability and prevents the compiler from keeping the `size` member in register and instead constantly store and reload it (it's unclear to the compiler that stores to buffer will not overwrite the `size` member, for all the compiler knows the returned new buffer could overlap with the `this` pointer). Godbolting codegen of libc++ and libstdc++, shows that everything is inlined, which mitigates the aliasing problem at the cost of oodles of extra code for each `push_back`.

This implementation solves most of these issues to a large extend. The first reason of bloat is mitigated by using type erasure to make most of buffer growing code non-templated. Only relocating a consecutive range of elements requires custom code and only in the case where memcpy does not work. Thus in a lot of cases like `Vec<int>` no custom code is required and in remaining cases just a single function per TU will be generated and further deduped by the linker. The aliasing problem is solved by using a `static` fallback function that takes the members of the vector by value and returns the new buffer by value as well. Therefore `this` will not escape unless the user explicitly does so. Resulting in the same optimal fast path code as `std::vector` but with enormously reduced code size.

Below are some benchmarks where we measure push_back 10000 integers to vector. The benchmarks compare the code in this repo `gerben::Vec`, the standard library `std::vector` and `RepeatedField`, which is used by google's protobuf implementation.


```
void Add(int n, vector<int>* x) {
    for (int i = 0; i < n; i++) x->push_back(i);
}

CPU Caches:
  L1 Data 64 KiB
  L1 Instruction 128 KiB
  L2 Unified 4096 KiB (x10)
Load Average: 16.97, 15.76, 15.70
------------------------------------------------------------------------------------
Benchmark                                          Time             CPU   Iterations
------------------------------------------------------------------------------------
BM_PushBack<gerben::Vec<int>>                   4098 ns         4096 ns       170245
BM_PushBack<std::vector<int>>                  21887 ns        21872 ns        31939
BM_PushBack<ProtoVec<int>>                      4106 ns         4103 ns       170777
BM_PushBackLocalCapture<gerben::Vec<int>>       3289 ns         3289 ns       213444
BM_PushBackLocalCapture<std::vector<int>>       3307 ns         3303 ns       211355
BM_PushBackLocalCapture<ProtoVec<int>>          4121 ns         4118 ns       169294
```
Note that the vector in `Add` is passed in as an output pointer parameter to a vector. Thus the aliasing analysis of the compiler fails and you see the disastrous performance of `std::vector` (libc++), which is 5x slower. This due to store/reload on the critical dependency chain of the `size` member of the vector. My implementation is carefully implemented that this store/reload latency is not present although in this case the compiler will still generate unnecessary stores. 

The `AddLocalCapture` variant is the same function with the difference that it moves the content of the output vector into a local variable at entry and moves the content with the added integers back into the output vector before returning. This solves the aliasing problem and the compiler is able to generate optimal machine code for both `std::vector` and `gerben::Vec`. However `RepeatedField` is significant slower, this is because it uses an templated but outline `Grow` method that escapes the `this` pointer and thus the compiler produces the same loop as this first case.


