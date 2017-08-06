load("//bazel:grpc_build_system.bzl", "grpc_proto_library")

grpc_proto_library(
    name = "unstructured_proto",
    srcs = ["unstructured.proto"],
)

cc_library(
    name = "unstructured",
    srcs = ["unstructured.cc"],
    deps = [":unstructured_proto"],
)

cc_library(
    name = "utils",
    hdrs = ["utils.h"],
)

cc_binary(
    name = "client0",
    srcs = ["client0.cc"],
    deps = [
        ":unstructured_proto",
        ":utils",
    ],
)

cc_binary(
    name = "client1",
    srcs = ["client1.cc"],
    deps = [":unstructured_proto"],
)

cc_binary(
    name = "client2",
    srcs = ["client2.cc"],
    deps = [
        ":unstructured_proto",
        ":utils",
    ],
)

cc_binary(
    name = "server",
    srcs = ["server.cc"],
    deps = [
        ":unstructured",
        ":utils",
    ],
)
