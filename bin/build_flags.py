#!/usr/bin/env python3

import os

Import("env")

env.Append(CCFLAGS=[])
env.Append(CFLAGS=[])
env.Append(CXXFLAGS=[])

if env.subst("$PIOENV").startswith("gpu"):
    project_dir = env.subst("$PROJECT_DIR")
    fastled_compat = os.path.join(
        project_dir, "include", "LedDisplay", "detail", "FastLedCompat.hpp"
    )
    fastled_compat_includes = os.path.join(
        project_dir, "include", "LedDisplay", "third_party"
    )
    env.Append(CXXFLAGS=["-I" + fastled_compat_includes, "-include", fastled_compat])

    def _skip_unused_fastled_sources(node):
        path = node.srcnode().get_path().replace(os.sep, "/")
        fastled_esp32 = "/FastLED/src/platforms/esp/32/"
        if fastled_esp32 not in path:
            return node
        if (
            "/audio/" in path
            or "/i2s/" in path
            or "/clockless_i2s" in path
        ):
            return None
        return node

    env.AddBuildMiddleware(_skip_unused_fastled_sources)
