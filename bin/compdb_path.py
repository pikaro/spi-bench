import os

Import("env")
# Do NOT enable toolchain includes! Breaks clangd by adding _both_ platforms.
# env.Replace(COMPILATIONDB_INCLUDE_TOOLCHAIN=True)
env.Replace(
    COMPILATIONDB_PATH=os.path.join("compiledb", env["PIOENV"], "compile_commands.json")
)
