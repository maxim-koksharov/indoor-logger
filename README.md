# indoor-logger — ESP8266 RTOS SDK C++ base

This repository contains a minimal C++ project skeleton for the ESP8266 using the official ESP8266 RTOS SDK.

Prerequisites
- Install the ESP8266 toolchain (xtensa-lx106) and the ESP8266 RTOS SDK from Espressif.
- Set the environment variable `RTOS_SDK` to the path of your `ESP8266_RTOS_SDK` checkout.

Quick start

1. Clone or place this project inside a working directory.
2. Export SDK path, for example:

```bash
export RTOS_SDK=~/ESP8266_RTOS_SDK
```

3. Build:

```bash
make
```

4. Flash with the usual SDK tools (for example `esptool.py` or from the SDK Makefile targets).

Files
- `Makefile`: Simple wrapper that includes the SDK `Makefile` (expects `RTOS_SDK`).
- `main/main.cpp`: Minimal C++ entry point (`user_init`) printing a startup message.
- `.gitignore`: Common ignores for build artifacts.

Notes
- This is a minimal starting point. Adapt includes, tasks, and build flags according to your SDK version.
- If your SDK uses a different entry point (e.g. `app_main`), adjust `main/main.cpp` accordingly.
