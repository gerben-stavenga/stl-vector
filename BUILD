cc_library(
    name = "vector",
    hdrs = ["vector.hpp"],
    srcs = ["vector.cpp"],
)

cc_binary(
    name = "benchmark",
    srcs = ["benchmark.cpp"],
    deps = [
        ":vector", 
        "@com_github_google_benchmark//:benchmark_main",
        "@com_google_protobuf//:protobuf_lite",
    ],
)