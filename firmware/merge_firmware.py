"""
Post-build script: merge bootloader + partitions + firmware into a single
flashable binary named mclite-vX.Y.Z.bin.

Usage (automatic via platformio.ini extra_scripts):
    Runs after each successful build.

Manual flash:
    esptool.py write_flash 0x0 mclite-vX.Y.Z.bin
"""

Import("env")

import re
import os


def merge_bin(source, target, env):
    # Extract version from defaults.h
    defaults_path = os.path.join(env.subst("$PROJECT_SRC_DIR"), "config", "defaults.h")
    version = "unknown"
    try:
        with open(defaults_path, "r") as f:
            for line in f:
                m = re.search(r'#define\s+MCLITE_VERSION\s+"([^"]+)"', line)
                if m:
                    version = m.group(1)
                    break
    except FileNotFoundError:
        print("WARNING: defaults.h not found, using 'unknown' version")

    build_dir = env.subst("$BUILD_DIR")
    output_name = f"mclite-v{version}.bin"
    output_path = os.path.join(build_dir, output_name)

    # ESP32-S3 flash layout offsets
    bootloader_offset = 0x0000
    partitions_offset = 0x8000
    firmware_offset   = 0x10000

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware   = os.path.join(build_dir, "firmware.bin")

    for path in [bootloader, partitions, firmware]:
        if not os.path.exists(path):
            print(f"ERROR: {path} not found, skipping merge")
            return

    # Read all parts
    with open(bootloader, "rb") as f:
        bootloader_data = f.read()
    with open(partitions, "rb") as f:
        partitions_data = f.read()
    with open(firmware, "rb") as f:
        firmware_data = f.read()

    # Build merged binary (fill gaps with 0xFF like blank flash)
    total_size = firmware_offset + len(firmware_data)
    merged = bytearray(b'\xff' * total_size)

    merged[bootloader_offset:bootloader_offset + len(bootloader_data)] = bootloader_data
    merged[partitions_offset:partitions_offset + len(partitions_data)] = partitions_data
    merged[firmware_offset:firmware_offset + len(firmware_data)] = firmware_data

    with open(output_path, "wb") as f:
        f.write(merged)

    size_kb = len(merged) / 1024
    print(f"Merged firmware: {output_name} ({size_kb:.0f} KB)")


env.AddPostAction("$BUILD_DIR/firmware.bin", merge_bin)
