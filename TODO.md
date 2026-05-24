# TODO

## C++ Migration Assessment

**Possible but constrained.** The ESP8266 toolchain links `-lstdc++` which pulls in
`libstdc++.a(guard.o)` — that object references `pthread_cond_*` symbols. The fix
(`pthread_stubs.c`) is already in place. With the stubs, components can be written
in C++ freely.

**Costs:**
- Extra 50–70 KB flash for `libstdc++.a` (significant on 2 MB)
- Higher IRAM/DRAM usage from exception handling support even if unused
- Static constructors consume RAM permanently
- `pthread_stubs.c` is a hack — real pthreads conflict with FreeRTOS

**Verdict:** Keep entry point (`main/`) in C. Use C++ only for sensor drivers /
business logic in `components/` where polymorphism or RAII actually helps.
The display library is fine in C (thin wrapper over C driver).

---

## Quality (5-star project)

### Structure & Build
- [x] CMake-only build (no legacy Makefiles)
- [x] Separate `components/` per domain (display, fonts)
- [ ] Add `clang-tidy` / `cppcheck` config and run in CI
- [ ] Add `.editorconfig` (indent style, charset, EOL)
- [ ] Add `CMakePresets.json` for build variants (debug, release, minimal)
- [x] Prune unused fonts (26 of 28 removed)
- [ ] Split `ssd1306.c` into `ssd1306.c` (driver core) + `ssd1306_gfx.c` (shapes)

### Testing
- [ ] Add hardware-in-the-loop test harness (e.g., pytest + esptool + serial)
- [ ] I2C scan at boot logs all found devices (debug builds)
- [ ] Self-test: ENS160 PART_ID check + AHT21 calibration status on serial

### Code Quality
- [ ] Enforce naming convention: `snake_case` for C, `PascalCase` for C++ classes
- [ ] Add `const` correctness pass across all source files
- [ ] Mark internal functions `static`; expose only public API in headers
- [ ] Remove the `int err = NULL;` warning in `ssd1306.c:862` (+submit upstream)
- [ ] Add LOGICAL error codes (enum) instead of magic `-1`, `-2` returns
- [ ] Fix `ssd1306_draw_char` type: `int err = NULL` → `int err = 0`
- [ ] Wrap `#include <fonts.h>` in `extern "C"` in `ssd1306.h` once and for all

### Safety & Robustness
- [ ] Add watchdog feed in the main loop (`vTaskDelay` is fine but document it)
- [ ] Handle I2C bus errors gracefully (retry N times, display "ERR" instead of hang)
- [ ] ENS160 burn-in period: show countdown on display (first 48 hours)
- [ ] Sensor read timeout — if AHT21/ENS160 don't respond, skip and show dashes
- [ ] Flash wear leveling if logging to NVS

### Documentation
- [ ] Write proper `README.md` with:
  - [ ] Photo of working hardware
  - [ ] Wiring diagram (ASCII or Fritzing)
  - [ ] I2C address table
  - [ ] Screenshot of display output
- [ ] Add `CONTRIBUTING.md` (how to flash, debug, add new sensors)
- [ ] Add Doxygen-style comments to all public API headers
- [x] Replace all Russian comments with English

### Developer Experience
- [ ] Add `docker-compose.yml` for one-command dev environment
- [ ] Add `Makefile` alias targets: `make flash`, `make monitor`, `make build`
  (thin wrappers that call `idf.py`, NOT the old make system)
- [ ] Add `.githooks/pre-push` that runs `idf.py build`
- [ ] UART monitor script (`tools/monitor.py`) wraps the Python serial snippet
- [ ] GitHub Actions CI: build on push, lint on PR

### Performance & Size
- [ ] Profile free heap at boot (`esp_get_free_heap_size()`) and log it
- [ ] Measure flash usage per component (map file analysis)
- [ ] Option to compile-out `printf` / `ESP_LOGx` in release builds
- [ ] Use IRAM placement only for ISR-critical paths; keep everything else in flash

### Features (roadmap)
- [ ] ENS160 + AHT21 real data display
- [ ] Button (GPIO12): toggle display on/off, hold to cycle pages
- [ ] Auto display-off after 10 s (already planned in AGENTS.md)
- [ ] Page 1: temp/hum + AQI/CO2 (current layout)
- [ ] Page 2: TVOC + raw sensor values
- [ ] Page 3: I2C bus health / uptime / free heap
- [ ] NVS logging: store min/max temp per day
- [ ] Deep sleep between reads (ESP8266, ~1 hour interval)
