#pragma once
#include <stdint.h>

struct ThemePaletteConfig {
    const char* name;
    uint32_t bg;
    uint32_t surface;
    uint32_t surface_light;
    uint32_t accent;
    uint32_t text_primary;
    uint32_t text_muted;
    uint32_t success;
    uint32_t warning;
};

// Declarative theme configuration isolated from UI logic.
// Tuned for 16-bit RGB565 ST7789 TFT gamma so backgrounds and surfaces have rich visible color.
static constexpr ThemePaletteConfig kThemes[] = {
    // 0: Cyber Cyan (Default)
    { "Cyber Cyan",     0x182232, 0x26364D, 0x384F6E, 0x00E5FF, 0xFFFFFF, 0x9EB0C7, 0x00E676, 0xFFB300 },
    // 1: Tube Amber (Warm Vacuum Tube Glow)
    { "Tube Amber",     0x2E1F10, 0x452F18, 0x634424, 0xFFB300, 0xFFF8EE, 0xC4AD93, 0x00E676, 0xFFB300 },
    // 2: Naim Emerald (Classic British Hi-Fi Green)
    { "Naim Emerald",   0x102B1E, 0x1A422E, 0x286145, 0x00E676, 0xF2FBF6, 0x95BAA6, 0x00E676, 0xFFB300 },
    // 3: McIntosh Blue (Iconic Meter Blue)
    { "McIntosh Blue",  0x10203B, 0x1B345E, 0x2B508C, 0x29B6F6, 0xF0F6FC, 0x93B5D9, 0x00E676, 0xFFB300 },
    // 4: Velvet Rose (Synthwave / Midnight Magenta)
    { "Velvet Rose",    0x2A1638, 0x3F2254, 0x5B3378, 0xFF4081, 0xFAF5FF, 0xB8A3CC, 0x00E676, 0xFFB300 },
    // 5: Studio Crimson (Darkroom Red)
    { "Studio Crimson", 0x331419, 0x4D1F26, 0x6E2D37, 0xFF5252, 0xFFF5F5, 0xC29CA3, 0x00E676, 0xFFB300 },
};

static constexpr uint8_t kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
