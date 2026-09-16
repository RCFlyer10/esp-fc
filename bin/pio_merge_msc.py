Import("env")

import csv
import os
import shutil

APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"
APP1_BIN = "$BUILD_DIR/${PROGNAME}_app1.bin"
MERGED_BIN = "$BUILD_DIR/${PROGNAME}_combined.bin"
MERGED_0X00_BIN = "$BUILD_DIR/${PROGNAME}_0x00.bin"


def _parse_int(value):
    return int(value, 0)


def _get_app1_offset():
    partitions_file = env.GetProjectOption("board_build.partitions")
    partitions_path = os.path.join(env.subst("$PROJECT_DIR"), partitions_file)

    with open(partitions_path, newline="", encoding="utf-8") as f:
        for row in csv.reader(f):
            if not row:
                continue
            name = row[0].strip()
            if not name or name.startswith("#"):
                continue
            if len(row) < 4:
                continue
            if name != "app1":
                continue
            return hex(_parse_int(row[3].strip()))

    raise RuntimeError(f"app1 partition not found in {partitions_file}")


def _get_base_firmware_bin():
    build_dir = env.subst("$PROJECT_BUILD_DIR")
    base_bin = os.path.join(build_dir, "esp32s3", "firmware_0x00.bin")
    if not os.path.isfile(base_bin):
        raise RuntimeError(
            f"base firmware not found: {base_bin}. Build the esp32s3 environment first."
        )
    return base_bin


def merge_bin(source, target, env):
    app_offset = _get_app1_offset()
    base_bin = _get_base_firmware_bin()
    app_bin = env.subst(APP_BIN)
    app1_bin = env.subst(APP1_BIN)
    merged_bin = env.subst(MERGED_BIN)
    merged_0x00_bin = env.subst(MERGED_0X00_BIN)

    shutil.copyfile(app_bin, app1_bin)

    env.Execute(
        " ".join(
            [
                "$PYTHONEXE",
                "$OBJCOPY",
                "--chip",
                env.BoardConfig().get("build.mcu", "esp32"),
                "merge_bin",
                "--flash_mode",
                "qio",
                "--flash_size",
                env.BoardConfig().get("upload.flash_size", "4MB"),
                "-o",
                merged_bin,
                "0x0",
                base_bin,
                app_offset,
                app1_bin,
            ]
        )
    )

    # Keep a 0x0-compatible filename for manual flashing workflows.
    shutil.copyfile(merged_bin, merged_0x00_bin)

# Flash only the MSC application image into OTA app1 partition.
env.AddPostAction(APP_BIN, merge_bin)

app_offset = _get_app1_offset()
env.Replace(
    UPLOADERFLAGS=[
        f
        for f in env.get("UPLOADERFLAGS")
        if f not in env.Flatten(env.get("FLASH_EXTRA_IMAGES"))
    ]
    + [app_offset, APP1_BIN],
    UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS',
)
