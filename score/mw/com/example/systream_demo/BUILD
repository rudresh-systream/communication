# *******************************************************************************
# Copyright (c) 2025 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
# https://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")

# ── Shared data type library ────────────────────────────────────────────────
# demo_types.h defines SensorData and the typed Skeleton/Proxy aliases.
# Both demo_server and demo_client depend on this library.
cc_library(
    name = "demo_types",
    hdrs = ["demo_types.h"],
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//examples:__pkg__"],
    deps = [
        "//score/mw/com",
    ],
)

# ── Skeleton (Publisher / Server) ────────────────────────────────────────────
# Build:  bazel build //score/mw/com/example/systream_demo:demo_server
# Output: bazel-bin/score/mw/com/example/systream_demo/demo_server
cc_binary(
    name = "demo_server",
    srcs = ["demo_server.cpp"],
    data = ["etc/mw_com_config.json"],
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//examples:__pkg__"],
    deps = [
        ":demo_types",
        "//score/mw/com",
        "@boost.program_options",
        "@score_baselibs//score/language/futurecpp",
        "@score_logging//score/mw/log",
    ],
)

# ── Proxy (Subscriber / Client) ──────────────────────────────────────────────
# Build:  bazel build //score/mw/com/example/systream_demo:demo_client
# Output: bazel-bin/score/mw/com/example/systream_demo/demo_client
cc_binary(
    name = "demo_client",
    srcs = ["demo_client.cpp"],
    data = ["etc/mw_com_config.json"],
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//examples:__pkg__"],
    deps = [
        ":demo_types",
        "//score/mw/com",
        "@boost.program_options",
        "@score_baselibs//score/language/futurecpp",
        "@score_logging//score/mw/log",
    ],
)

# ── Config files ──────────────────────────────────────────────────────────────
exports_files(["etc/mw_com_config.json"])
