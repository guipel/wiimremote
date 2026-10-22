import os
Import("env")

def merge_factory_bin(source, target, env):
    platform = env.PioPlatform()
    esptool_dir = platform.get_package_dir("tool-esptoolpy")
    framework_dir = platform.get_package_dir("framework-arduinoespressif32")
    if not esptool_dir or not framework_dir:
        print("Warning: Could not locate tool-esptoolpy or framework-arduinoespressif32 for factory binary merge.")
        return

    esptool_py = os.path.join(esptool_dir, "esptool.py")
    boot_app0 = os.path.join(framework_dir, "tools", "partitions", "boot_app0.bin")

    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    bootloader_bin = os.path.join(build_dir, "bootloader.bin")
    partitions_bin = os.path.join(build_dir, "partitions.bin")
    firmware_bin = os.path.join(build_dir, "firmware.bin")

    out_dir = os.path.join(project_dir, "firmware")
    os.makedirs(out_dir, exist_ok=True)
    factory_bin = os.path.join(out_dir, "wiimremote-factory.bin")

    cmd = " ".join([
        f'"{env.subst("$PYTHONEXE")}"',
        f'"{esptool_py}"',
        "--chip esp32s3",
        "merge_bin",
        "-o", f'"{factory_bin}"',
        "--flash_mode dio",
        "--flash_freq 80m",
        "--flash_size 16MB",
        "0x0000", f'"{bootloader_bin}"',
        "0x8000", f'"{partitions_bin}"',
        "0xe000", f'"{boot_app0}"',
        "0x10000", f'"{firmware_bin}"'
    ])

    print(f"Generating merged factory binary: {factory_bin}")
    env.Execute(cmd)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_factory_bin)

# Also generate if firmware.bin already exists and wiimremote-factory.bin is missing or older
_build_dir = env.subst("$BUILD_DIR")
_fw_bin = os.path.join(_build_dir, "firmware.bin")
_factory_bin = os.path.join(env.subst("$PROJECT_DIR"), "firmware", "wiimremote-factory.bin")
if os.path.exists(_fw_bin) and (not os.path.exists(_factory_bin) or os.path.getmtime(_fw_bin) > os.path.getmtime(_factory_bin)):
    merge_factory_bin(None, None, env)
