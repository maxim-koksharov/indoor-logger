# Display — SSD1306 OLED Driver

C wrapper for the SSD1306 OLED display (128×32, I2C) with a convenient high-level API.

## Files

| File | Description |
|---|---|
| `ssd1306.c` / `include/ssd1306.h` | Low-level SSD1306 I2C driver (third-party, MIT licensed) |
| `Display.c` / `include/Display.h` | High-level C wrapper with framebuffer and scaled text |

## Dependencies

- `driver/i2c` (ESP-IDF)
- `components/fonts` (bitmap fonts)

## Features

- 128×32 pixel monochrome display at I2C address `0x3C`
- 512-byte framebuffer (`128 × 32 / 8`)
- Scaled text rendering (2x for readability on small display)
- Framebuffer-based drawing (pixel, line, rectangle, fill)
- Init sequence, on/off, contrast, inversion control

## Usage

```c
#include "Display.h"

Display display;

display_init(&display, GPIO4, GPIO5, SSD1306_I2C_ADDR_0);  // SDA=D2, SCL=D1

const font_info_t *font = font_builtin_fonts[FONT_FACE_GLCD5x7];
display_clear_fb(&display);
display_draw_string_scaled(&display, font, 0, 0, "Hello", 2);
display_present(&display);
display_on(&display);
```

## API

| Function | Purpose |
|---|---|
| `display_init()` | Initialize I2C, configure SSD1306, create framebuffer |
| `display_on()` / `display_off()` | Display power control |
| `display_clear_fb()` | Clear framebuffer (in RAM, not sent to display) |
| `display_present()` | Send framebuffer to display via I2C |
| `display_draw_pixel()` | Set individual pixel |
| `display_draw_hline()` / `display_draw_vline()` | Horizontal/vertical line |
| `display_draw_char_scaled()` | Single character with scale factor |
| `display_draw_string_scaled()` | String with scale factor |

## Important Notes

- The display is physically **128×32**, not 128×64. Pixels with `y >= 32` are invisible.
- GLCD 5×7 font at 1× is too small. Use 2× scaling: `display_draw_string_scaled(&d, font, x, y, "text", 2)`.
- At 2× scaling, ~10 characters per line, ~2 lines fit on the display.

## License

SSD1306 driver: MIT (by urx, UncleRus, Zaltora). See `ssd1306.h` header.
