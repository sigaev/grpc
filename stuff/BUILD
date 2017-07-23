load("//bazel:grpc_build_system.bzl", "grpc_proto_library")

grpc_proto_library(
    name = "helloworld",
    srcs = ["helloworld.proto"],
)

cc_library(
    name = "utils",
    hdrs = ["utils.h"],
)

cc_binary(
    name = "client0",
    srcs = ["client0.cc"],
    deps = [
        ":helloworld",
        ":utils",
    ],
)

cc_binary(
    name = "client1",
    srcs = ["client1.cc"],
    deps = [":helloworld"],
)

cc_binary(
    name = "client2",
    srcs = ["client2.cc"],
    deps = [":helloworld"],
)

cc_binary(
    name = "server",
    srcs = ["server.cc"],
    deps = [
        ":helloworld",
        ":utils",
    ],
)
