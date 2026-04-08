Import("env")
import os
import glob

build_dir = env.subst("$BUILD_DIR")
python = env.subst("$PYTHONEXE")
cache_file = os.path.join(build_dir, "CMakeCache.txt")

maps = glob.glob(os.path.join(build_dir, "*.map"))
if len(maps) != 1:
    raise RuntimeError(f"Expected exactly one .map in {build_dir}, found {maps}")
map_file = maps[0]


def cmd(*args):
    parts = [python, "-m", "esp_idf_size", map_file, *args]
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
