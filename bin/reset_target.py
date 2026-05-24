import glob
import os
import subprocess
import sys

Import("env")


def package_file(package_name, *path_parts):
    package_path = os.path.join(package_name, *path_parts)
    try:
        package_dir = env.PioPlatform().get_package_dir(package_name)
    except KeyError:
        package_dir = None

    candidates = []
    if package_dir:
        candidates.append(os.path.join(package_dir, *path_parts))

        parent = os.path.dirname(package_dir)
        base_name = os.path.basename(package_dir).split("@", 1)[0]
        for versioned_dir in sorted(
            glob.glob(os.path.join(parent, f"{base_name}@*")),
            reverse=True,
        ):
            candidates.append(os.path.join(versioned_dir, *path_parts))

    candidates.append(
        os.path.expanduser(
            os.path.join("~", ".platformio", "packages", package_path),
        ),
    )
    for versioned_dir in sorted(
        glob.glob(
            os.path.expanduser(
                os.path.join("~", ".platformio", "packages", f"{package_name}@*"),
            ),
        ),
        reverse=True,
    ):
        candidates.append(os.path.join(versioned_dir, *path_parts))

    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate

    if candidates:
        return candidates[0]
    return os.path.join(*path_parts)


def subst(name):
    value = env.subst(name)
    if value == name:
        return ""
    return value


def board_upload_protocols():
    protocols = env.BoardConfig().get("upload.protocols", [])
    if isinstance(protocols, str):
        return {protocols}
    return set(protocols or [])


def upload_protocol():
    configured = subst("$UPLOAD_PROTOCOL")
    protocols = board_upload_protocols()
    if configured and (not protocols or configured in protocols):
        return configured
    return env.BoardConfig().get("upload.protocol", configured)


def reset_protocol():
    configured = str(
        env.GetProjectOption("custom_reset_protocol", "") or "",
    ).strip()
    if configured:
        return configured
    return upload_protocol()


def mcu_name():
    return str(env.BoardConfig().get("build.mcu", "") or "").lower()


def touch_upload_port_1200(target, source, env):
    upload_options = env.BoardConfig().get("upload", {}) or {}
    if not bool(upload_options.get("disable_flushing", False)):
        env.FlushSerialBuffer("$UPLOAD_PORT")
    env.TouchSerialPort("$UPLOAD_PORT", 1200)


def configure_esptool_reset():
    env.Replace(
        RESETTOOL=package_file("tool-esptoolpy", "esptool.py"),
        RESETFLAGS=[
            "--no-stub",
            "--chip",
            env.BoardConfig().get("build.mcu", "esp32"),
            "--port",
            "$UPLOAD_PORT",
            "flash_id",
        ],
        RESETCMD='"$PYTHONEXE" "$RESETTOOL" $RESETFLAGS',
    )
    return (
        [env.VerboseAction("$RESETCMD", "Resetting target")],
        "Reset ESP target",
        "This command resets ESP targets via esptoolpy",
    )


def configure_nrf_reset(protocol):
    if protocol == "nrfjprog":
        env.Replace(
            RESETTOOL="nrfjprog",
            RESETFLAGS=["--reset", "-f", "nrf52"],
            RESETCMD='"$RESETTOOL" $RESETFLAGS',
        )
        return (
            [env.VerboseAction("$RESETCMD", "Resetting target")],
            "Reset nRF target",
            "This command resets nRF targets via nrfjprog",
        )

    if protocol.startswith("jlink"):
        interface = "jtag" if protocol == "jlink-jtag" else "swd"
        env.Replace(
            JLINK_RESET_SCRIPT=os.path.join("$BUILD_DIR", "reset.jlink"),
            RESETTOOL=package_file(
                "tool-jlink",
                "JLink.exe" if os.name == "nt" else "JLinkExe",
            ),
            RESETFLAGS=[
                "-device",
                env.BoardConfig().get("debug", {}).get("jlink_device"),
                "-speed",
                env.GetProjectOption("debug_speed", "4000"),
                "-if",
                interface,
                "-autoconnect",
                "1",
                "-ExitOnError",
                "1",
                "-NoGui",
                "1",
                "-CommanderScript",
                "$JLINK_RESET_SCRIPT",
            ],
            RESETCMD='"$RESETTOOL" $RESETFLAGS',
        )
        return (
            [
                env.VerboseAction(
                    write_jlink_reset_script,
                    "Preparing J-Link reset",
                ),
                env.VerboseAction(run_jlink_reset, "Resetting target"),
            ],
            "Reset nRF target",
            "This command resets nRF targets via J-Link",
        )

    return configure_unsupported_reset(protocol)


def configure_nrf_bootloader_reset():
    return (
        [
            env.VerboseAction(
                touch_upload_port_1200,
                "Resetting target into bootloader",
            ),
        ],
        "Reset nRF to bootloader",
        "This command resets nRF serial targets into bootloader mode",
    )


def write_jlink_reset_script(target, source, env):
    path = env.subst("$JLINK_RESET_SCRIPT")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as script:
        script.write("connect\nr\ng\nq\n")


def run_jlink_reset(target, source, env):
    tool = env.subst("$RESETTOOL")
    flags = [env.subst(str(flag)) for flag in env.get("RESETFLAGS", [])]
    command = [tool, *flags]
    result = subprocess.run(
        command,
        cwd=os.path.dirname(tool) or None,
        text=True,
        capture_output=True,
    )
    output = f"{result.stdout}{result.stderr}"
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)

    if result.returncode != 0 or "FAILED" in output or "ERROR" in output:
        raise RuntimeError(
            f"J-Link reset failed with exit code {result.returncode}: "
            f"{' '.join(command)}",
        )


def unsupported_reset(target, source, env):
    raise RuntimeError(
        f"reset target does not support reset protocol {reset_protocol()!r} "
        f"for MCU {mcu_name()!r}; use reset-bootloader for serial DFU touch "
        "or configure nrfjprog/J-Link for hardware reset",
    )


def configure_unsupported_reset(protocol):
    return (
        [env.VerboseAction(unsupported_reset, "Resetting target")],
        "Reset target",
        f"This command does not support reset protocol {protocol}",
    )


protocol = reset_protocol()
mcu = mcu_name()

if protocol == "esptool" or mcu.startswith("esp"):
    actions, title, description = configure_esptool_reset()
elif mcu.startswith("nrf"):
    actions, title, description = configure_nrf_reset(protocol)
else:
    actions, title, description = configure_unsupported_reset(protocol)

env.AddCustomTarget(
    name="reset",
    dependencies=None,
    actions=actions,
    title=title,
    description=description,
)

if mcu.startswith("nrf"):
    bootloader_actions, bootloader_title, bootloader_description = (
        configure_nrf_bootloader_reset()
    )
    env.AddCustomTarget(
        name="reset-bootloader",
        dependencies=None,
        actions=bootloader_actions,
        title=bootloader_title,
        description=bootloader_description,
    )
