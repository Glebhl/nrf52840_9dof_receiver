import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(
        description="Convert PlatformIO firmware.hex to UF2 and copy it to UF2 bootloader drive."
    )

    parser.add_argument(
        "--hex",
        default=r".pio\build\nicenano\firmware.hex",
        help="Path to input firmware.hex"
    )

    parser.add_argument(
        "--no-copy",
        action="store_true",
        help="Skip coping uf2 to the drive"
    )

    parser.add_argument(
        "--uf2conv",
        default="utils/uf2conv.py",
        help="Path to uf2conv.py"
    )

    parser.add_argument(
        "--drive",
        default="E:/",
        help="UF2 bootloader drive path, for example E:/"
    )

    parser.add_argument(
        "--family",
        default="0xada52840",
        help="UF2 family ID. For Adafruit nRF52840 usually 0xada52840"
    )

    args = parser.parse_args()

    hex_path = Path(args.hex)
    uf2conv_path = Path(args.uf2conv)
    drive_path = Path(args.drive)

    if not hex_path.exists():
        print(f"ERROR: HEX file not found: {hex_path}")
        sys.exit(1)

    if not uf2conv_path.exists():
        print(f"ERROR: uf2conv.py not found: {uf2conv_path}")
        sys.exit(1)

    if not args.no_copy and not drive_path.exists():
        print(f"ERROR: UF2 drive not found: {drive_path}")
        sys.exit(1)

    uf2_path = hex_path.with_suffix(".uf2")

    cmd = [
        sys.executable,
        str(uf2conv_path),
        str(hex_path),
        "-c",
        "-f",
        args.family,
        "-o",
        str(uf2_path),
    ]

    print("Converting HEX to UF2...")
    print(" ".join(cmd))

    result = subprocess.run(cmd)

    if result.returncode != 0:
        print("ERROR: uf2conv.py failed")
        sys.exit(result.returncode)

    if not uf2_path.exists():
        print(f"ERROR: UF2 was not created: {uf2_path}")
        sys.exit(1)

    print(f"Created: {uf2_path}")

    if args.no_copy:
        print("Skipped copying")
        print("Done.")
        sys.exit(0)

    target_path = drive_path / uf2_path.name
    print(f"Copying UF2 to {target_path}...")

    shutil.copy2(uf2_path, target_path)

    print(f"Copied to: {target_path}")
    print("Done.")


if __name__ == "__main__":
    main()
