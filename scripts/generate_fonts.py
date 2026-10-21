import os
import subprocess

# Built-in font paths from LVGL package
TTF_FONT = ".pio/libdeps/esp32-s3-wiim-remote/lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
WOFF_FONT = ".pio/libdeps/esp32-s3-wiim-remote/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
OUTPUT_DIR = "src/ui/fonts"

os.makedirs(OUTPUT_DIR, exist_ok=True)

# Glyph ranges:
# 0x20-0xFF: ASCII + Latin-1 Supplement (á, é, í, ó, ú, ñ, ç, etc.)
# 0x0100-0x017F: Latin Extended-A (č, ć, š, ž, etc.)
# 0x2010-0x2027: General Punctuation (’, ‘, “, ”, –, —, •, …, etc.)
RANGES = "0x20-0xFF,0x0100-0x017F,0x2010-0x2027"

# LVGL Standard Symbols (Transport controls, volume, wifi, bluetooth, etc.)
SYMS = "61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

SIZES = [10, 12, 14, 16, 20, 24]

for size in SIZES:
    font_name = f"ui_font_montserrat_{size}"
    out_file = f"{OUTPUT_DIR}/{font_name}.c"
    print(f"Generating {out_file}...")
    cmd = [
        "cmd.exe",
        "/c",
        "npx",
        "-y",
        "lv_font_conv",
        "--no-compress",
        "--no-prefilter",
        "--bpp", "4",
        "--size", str(size),
        "--font", TTF_FONT,
        "-r", RANGES,
        "--font", WOFF_FONT,
        "-r", SYMS,
        "--format", "lvgl",
        "-o", out_file,
        "--force-fast-kern-format",
        "--lv-include", "lvgl.h",
        "--lv-font-name", font_name
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode == 0:
        print(f"Successfully generated {out_file} ({os.path.getsize(out_file)} bytes)")
    else:
        print(f"Error generating {size}px: {res.stderr}")
        raise SystemExit(1)

print("Font generation finished!")
