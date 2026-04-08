Import("env")
import os
import glob
import re

build_dir = env.subst("$BUILD_DIR")
cache_file = os.path.join(build_dir, "CMakeCache.txt")

idf_python = None
with open(cache_file, "r", encoding="utf-8") as f:
    for line in f:
        m = re.match(r"PYTHON:UNINITIALIZED=(.*)", line.strip())
        if m:
            idf_python = m.group(1)
            break

if not idf_python:
    raise RuntimeError("Could not find IDF_PYTHON_ENV_PATH in CMakeCache.txt")

maps = glob.glob(os.path.join(build_dir, "*.map"))
if len(maps) != 1:
    raise RuntimeError(f"Expected exactly one .map in {build_dir}, found {maps}")
map_file = maps[0]


def cmd(*args):
    parts = [idf_python, "-m", "esp_idf_size", map_file, *args]
    return " ".join(f'"{p}"' if " " in p else p for p in parts)


env.AddCustomTarget(
    name="idf-size",
    dependencies=["buildprog"],
    actions=[
        cmd(),
        cmd("--archives"),
        cmd("--files"),
    ],
    title="IDF Size",
    description="ESP-IDF static memory summary",
)

env.AddCustomTarget(
    name="idf-size-components",
    dependencies=["buildprog"],
    actions=[cmd("--archives")],
    title="IDF Size Components",
    description="Per-archive static memory usage",
)

env.AddCustomTarget(
    name="idf-size-files",
    dependencies=["buildprog"],
    actions=[cmd("--files")],
    title="IDF Size Files",
    description="Per-file static memory usage",
)
