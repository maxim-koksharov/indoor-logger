# Fonts — Bitmap Font Library

Compile-time bitmap font library for small OLED/LCD displays. Derived from the esp-open-rtos font project.

## Files

| File | Description |
|---|---|
| `fonts.c` / `fonts.h` | Font access API and built-in font registry |
| `data/` | Binary font data (embedded at compile time) |
| `tools/` | Font generation tools |
| `LICENSE` / `OFL.txt` | License files |

## Available Fonts

| Enum | Name | Char Range |
|---|---|---|
| `FONT_FACE_GLCD5x7` | GLCD 5×7 | 0–255 |
| `FONT_FACE_ROBOTO_8PT` | Roboto 8pt | — |
| `FONT_FACE_ROBOTO_10PT` | Roboto 10pt | — |
| `FONT_FACE_BITOCRA_4X7` | Bitocra 4×7 | — |
| `FONT_FACE_BITOCRA_6X11` | Bitocra 6×11 | — |
| `FONT_FACE_BITOCRA_7X13` | Bitocra 7×13 | — |
| `FONT_FACE_TERMINUS_*` | Terminus (various sizes) | ISO8859-1 |
| `FONT_FACE_COMPACT_6X8` | Compact 6×8 | 0x20–0xFF |
| `FONT_FACE_ROBOTO_12PT` | Roboto 12pt | — |
| `FONT_FACE_ROBOTO_16PT` | Roboto 16pt | — |

## Usage

```c
#include "fonts.h"

const font_info_t *font = font_builtin_fonts[FONT_FACE_GLCD5x7];
```

## Recommendation

For 128×32 OLED at 2× scaling, use `FONT_FACE_GLCD5x7` — it's the smallest and most reliable. At 2×, each character is ~10×14 pixels, fitting ~2 lines of 12 characters.

## License

Dual-licensed: OFL-1.1 for Terminus fonts, MIT for the library code.
