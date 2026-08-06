Import("env")

import csv
import os

APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"


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

# Flash only the MSC application image into OTA app1 partition.
app_offset = _get_app1_offset()
env.Replace(
    UPLOADERFLAGS=[
        f
        for f in env.get("UPLOADERFLAGS")
        if f not in env.Flatten(env.get("FLASH_EXTRA_IMAGES"))
    ]
    + [app_offset, APP_BIN],
    UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS',
)
