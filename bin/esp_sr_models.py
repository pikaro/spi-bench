#!/usr/bin/env python3

from __future__ import annotations

from pathlib import Path

Import("env")

if env.subst("$PIOENV") == "ai":
    project_dir = Path(env.subst("$PROJECT_DIR"))
    build_dir = Path(env.subst("$BUILD_DIR"))
    component_dir = (
        build_dir / "managed_components" / "espressif__esp-sr"
    )
    packer = component_dir / "model" / "movemodel.py"
    sdkconfig = project_dir / "sdkconfig.ai"
    image = build_dir / "srmodels" / "srmodels.bin"

    # PlatformIO imports ordinary ESP-IDF component archives from CMake's code
    # model, but not archives created with add_prebuilt_library(). ESP-SR ships
    # its runtime that way, so mirror the library set from ESP-SR's CMakeLists
    # into PlatformIO's existing --start-group/--end-group link group.
    env.Append(
        LIBS=[
            "dl_lib",
            "c_speech_features",
            "esp_audio_front_end",
            "esp_audio_processor",
            "esp_tts_chinese",
            "voice_set_xiaole",
            "fst",
            "flite_g2p",
            "hufzip",
            "multinet",
            "nsnet",
            "vadnet",
            "wakenet",
        ]
    )

    model_image = env.Command(
        str(image),
        [str(sdkconfig), str(packer)],
        env.VerboseAction(
            '"$ESPIDF_PYTHONEXE" "{}" -d1 "{}" -d2 "{}" -d3 "{}"'.format(
                packer, sdkconfig, component_dir, build_dir
            ),
            "Packing ESP-SR models $TARGET",
        ),
    )
    env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", model_image)
