# TODO

## Phase 1: Monorepo Restructuring

### New project structure
```
/esp/project/
├── CMakeLists.txt              # Root CMake (includes ESP-IDF)
├── components/                 # Shared libraries
│   ├── display/                # SSD1306 OLED driver + Display wrapper
│   │   ├── CMakeLists.txt
│   │   ├── ssd1306.c
│   │   ├── Display.c
│   │   ├── pthread_stubs.c
│   │   └── include/
│   │       ├── ssd1306.h
│   │       └── Display.h
│   ├── sensors/                # ENS160 + AHT21 sensor drivers
│   │   ├── CMakeLists.txt
│   │   ├── ens160.c
│   │   ├── aht21.c
│   │   └── include/
│   │       ├── ens160.h
│   │       └── aht21.h
│   ├── fonts/                  # Font library
│   │   ├── CMakeLists.txt
│   │   ├── fonts.c
│   │   ├── fonts.h
│   │   └── data/
│   └── wifi/                   # WiFi connection helper (new)
│       ├── CMakeLists.txt
│       ├── wifi_manager.c
│       └── include/
│           └── wifi_manager.h
├── apps/
│   ├── server/                 # Server application (16MB ESP8266)
│   │   ├── CMakeLists.txt
│   │   ├── main/
│   │   │   └── main.c
│   │   └── sdkconfig
│   └── client/                 # Client application (4MB/16MB ESP8266)
│       ├── CMakeLists.txt
│       ├── main/
│       │   └── main.c
│       └── sdkconfig
└── TODO.md
```

### Tasks
- [x] Move sensor drivers from `main/` to `components/sensors/`
- [x] Create `components/wifi/` with WiFi connection helper
- [x] Create `apps/server/` directory structure
- [x] Create `apps/client/` directory structure
- [x] Update root `CMakeLists.txt` for monorepo
- [x] Create `apps/server/CMakeLists.txt` with proper component dependencies
- [x] Create `apps/client/CMakeLists.txt` with proper component dependencies
- [x] Create `apps/server/sdkconfig` (16MB flash, server settings)
- [x] Create `apps/client/sdkconfig` (4MB/16MB flash, client settings)
- [x] Move `main/main.c` to `apps/client/main/main.c` as baseline
- [x] Create `apps/server/main/main.c` with server entry point
- [x] Create `apps/client/main/main.c` with client entry point
- [x] Remove old `main/` directory
- [x] Test build: `cd apps/client && idf.py build`
- [x] Test build: `cd apps/server && idf.py build`
- [ ] Remove old `main/` directory
- [ ] Test build: `cd apps/client && idf.py build`
- [ ] Test build: `cd apps/server && idf.py build`

## Phase 2: WiFi Infrastructure

### WiFi Manager Component
- [ ] Implement `wifi_manager_init()` with STA mode
- [ ] Implement `wifi_manager_init_ap()` for AP mode (server fallback)
- [ ] Add WiFi event handlers for connection/disconnection
- [ ] Add NVS storage for WiFi credentials
- [ ] Add auto-reconnect with exponential backoff

### Server WiFi Setup
- [ ] Server starts in AP mode by default (creates its own network)
- [ ] Server assigns static IP (192.168.4.1)
- [ ] Server can optionally connect to existing WiFi (STA mode)

### Client WiFi Setup
- [ ] Client connects to server's AP or existing network
- [ ] Client stores server IP/hostname in NVS
- [ ] Client retries connection on failure

## Phase 3: Server Application (16MB ESP8266)

### HTTP REST API
- [ ] Embedded HTTP server (use ESP8266's built-in or lwip + httpd)
- [ ] `GET /api/clients` - list connected clients
- [ ] `GET /api/clients/<id>/data` - get raw data from specific client
- [ ] `GET /api/data` - aggregated data from all clients
- [ ] `POST /api/clients/<id>/upload` - receive data from client
- [ ] `GET /api/health` - server health check

### Web UI Dashboard
- [ ] Serve static HTML/CSS/JS from flash (SPIFFS/LittleFS)
- [ ] Real-time data display using WebSocket or polling
- [ ] Charts for temperature, humidity, eCO2, TVOC over time
- [ ] Client status overview (online/offline, last seen)
- [ ] AQI summary across all clients

### Data Storage (Server)
- [ ] Use LittleFS for time-series data storage
- [ ] Implement data retention policy (e.g., keep 7 days)
- [ ] Store per-client data in separate files
- [ ] Implement data aggregation (min/max/avg per hour)

## Phase 4: Client Application (4MB/16MB ESP8266)

### Sensor Integration
- [ ] Initialize ENS160 + AHT21 on I2C bus
- [ ] Read sensor data at configurable interval (default: 60s)
- [ ] Handle sensor errors gracefully (retry, skip, log)
- [ ] Display current readings on OLED

### Display Management
- [ ] Page 1: Temperature + Humidity (from AHT21)
- [ ] Page 2: eCO2 + TVOC + AQI (from ENS160)
- [ ] Page 3: WiFi status + uptime + free heap
- [ ] Button (GPIO12): cycle pages, long press for config
- [ ] Auto display-off after 10s, button wakes

### Local Data Storage
- [ ] NVS: store WiFi credentials, server IP, config settings
- [ ] LittleFS: store sensor readings when offline
- [ ] Implement circular buffer for offline data (store up to N readings)
- [ ] Data format: timestamp, temp, humidity, eCO2, TVOC, AQI

### Data Sync with Server
- [ ] HTTP POST to server with batched readings
- [ ] Retry on failure with exponential backoff
- [ ] Mark synced data, delete from local storage
- [ ] Support server request for historical data

## Phase 5: Quality & Testing

### Build & CI
- [ ] Add `clang-tidy` / `cppcheck` config
- [ ] Add `.editorconfig`
- [ ] Add `CMakePresets.json` for build variants
- [ ] GitHub Actions CI: build both apps on push

### Testing
- [ ] Hardware-in-the-loop test harness
- [ ] I2C scan at boot logs all found devices
- [ ] Self-test: ENS160 PART_ID + AHT21 calibration check
- [ ] Server API integration tests

### Code Quality
- [ ] Enforce naming conventions
- [ ] Const correctness pass
- [ ] Mark internal functions `static`
- [ ] Add logical error codes (enum) instead of magic numbers
- [ ] Add Doxygen-style comments to public APIs

### Safety & Robustness
- [ ] Watchdog feed in main loops
- [ ] I2C bus error handling (retry N times)
- [ ] ENS160 burn-in countdown display (48 hours)
- [ ] Sensor read timeout handling
- [ ] Flash wear leveling for NVS/LittleFS

## Phase 6: Features (Roadmap)

- [ ] Deep sleep between reads (client, configurable interval)
- [ ] OTA firmware updates
- [ ] Multiple client support (server handles N clients)
- [ ] Data export (CSV download from web UI)
- [ ] Alert thresholds (e.g., eCO2 > 1000ppm)
- [ ] Mobile app / Telegram bot integration
