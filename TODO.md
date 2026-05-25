# TODO

## Согласованные архитектурные решения

- **Client ID**: кастомный строковый ID (`living_room`, `kitchen` и т.д.), задается при прошивке через Kconfig/NVS.
- **Время**: гибрид — сервер ставит `time(NULL)` при получении. Client отправляет `uptime_sec` как fallback.
- **Хранение**: бинарный packed формат (14 байт/запись), кольцевой буфер per-client в SPIFFS.
- **Веб-интерфейс**: zero-SPIFFS — HTML/CSS/JS встроены в `.rodata` через `const char[]`. Весь SPIFFS под данные.
- **WiFi Server**: AP mode (`AirMon-Server`) + STA fallback (если заданы креденшиалы).
- **Client**: мониторинговый режим — deep sleep 5 мин → измерение → отправка → сон. Кнопка GPIO12: wake-up, показ текущих данных 10 сек.
- **Upload**: JSON (`POST /api/clients/<id>/upload`), хранение на сервере — бинарное.
- **OTA**: не нужен ни на сервере, ни на клиенте.

---

## Phase 1: Server — Partition Table & Data Store

### S1: Partition Table (Server 16MB, no OTA)
- [ ] Создать `apps/server/partitions_server.csv`:
  - nvs (0x4000), phy_init (0x1000)
  - ota_0 (1MB), ota_1 (1MB) — оставить на всякий случай, но OTA не используется
  - spiffs (~14MB, offset 0x210000) — под данные клиентов
- [ ] Обновить `apps/server/sdkconfig`:
  - `CONFIG_PARTITION_TABLE_CUSTOM=y`
  - `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_server.csv"`
- [ ] Проверка: `cd apps/server && idf.py build` проходит без ошибок.

### S2: Data Store Module (`apps/server/main/data_store.c/h`)
- [ ] Определить `data_record_t` (packed, 14 байт):
  - `uint32_t timestamp`, `int16_t temp*100`, `uint16_t hum*100`, `uint16_t eco2`, `uint16_t tvoc`, `uint8_t aqi`, `uint8_t _pad`
- [ ] Определить `client_file_header_t` (magic, version, client_id[32], max_records, write_idx, count)
- [ ] Реализовать `data_store_init()` — mount SPIFFS, форматировать при первом старте.
- [ ] Реализовать `data_store_append(client_id, rec)` — циклическая запись в `/spiffs/<id>.dat`.
- [ ] Реализовать `data_store_read_range(client_id, offset, limit, out, capacity)` — чтение записей.
- [ ] Реализовать `data_store_get_count(client_id)`.
- [ ] Реализовать `data_store_delete_client(client_id)`.
- [ ] Тест в `main.c`: append 3 записи, read, print — проверить через логи.

### S3: Client Registry (`apps/server/main/client_registry.c/h`)
- [ ] `client_info_t` (id[32], name[32], last_seen, online, ip_str[16]).
- [ ] Массив в RAM (max 16 клиентов).
- [ ] `client_registry_init()`, `update()`, `get()`, `get_all()`.
- [ ] `client_registry_check_stale(timeout_sec=360)` — пометить offline.
- [ ] Сохранение/загрузка в NVS (`client_registry_save/load`).
- [ ] Тест: рестарт сервера → реестр восстанавливается из NVS.

---

## Phase 2: Server — HTTP REST API

### S4: HTTP Server Core (`apps/server/main/http_server.c/h`)
- [ ] `http_server_init()`, `http_server_start()`.
- [ ] Endpoint `GET /api/health` → JSON: status, uptime, free_heap, clients_online.
- [ ] Endpoint `GET /api/clients` → JSON-массив: id, name, online, last_seen, latest values.
- [ ] Endpoint `GET /api/clients/<id>` → JSON: info + последние N записей (default 24).
- [ ] Endpoint `POST /api/clients/<id>/upload` → JSON array:
  - Парсинг `cJSON`, для каждого объекта: `uptime`, `temp`, `hum`, `eco2`, `tvoc`, `aqi`.
  - Конвертация в `data_record_t` (timestamp = time(NULL), float → fixed-point).
  - `data_store_append()` + `client_registry_update()`.
- [ ] Endpoint `GET /api/clients/<id>/data?limit=&offset=` → JSON-массив записей для графиков.
- [ ] Проверка через `curl`: все endpoints отвечают корректно.

### S5: Embedded Web UI (`apps/server/main/web_ui.h`)
- [ ] `index_html[]` — одностраничный UI, inline CSS + JS.
- [ ] Таблица клиентов: ID, имя, статус (online/offline), время, текущие значения.
- [ ] Детальная карточка по клику: последние значения крупно + `<canvas>` график (24 точки).
- [ ] Автообновление: `fetch('/api/clients')` каждые 5 сек.
- [ ] Тёмная тема, моноширинные цифры, AQI-индикаторы (цвета).
- [ ] Подключить к `http_server.c` (URI `/` → `index_html`).
- [ ] Проверка: браузер открывает страницу, видит данные.

---

## Phase 3: Server — WiFi, NTP & Integration

### S6: WiFi Manager Доработка (`components/wifi/`)
- [ ] Добавить `wifi_manager_init_ap_with_sta_fallback(ap_ssid, ap_pass, sta_ssid, sta_pass)`.
- [ ] Если STA креденшиалы заданы — `WIFI_MODE_APSTA`, попытка подключения.
- [ ] Если STA не удался за 30 сек — оставить только AP.
- [ ] `wifi_manager_get_ap_ip()` — получить IP AP-интерфейса.

### S7: NTP & Main Loop
- [ ] Если STA подключился — запустить SNTP (`esp_netif_sntp_init` или `sntp_setoperatingmode`).
- [ ] Подождать синхронизации времени.
- [ ] `main.c` flow: NVS → data_store → registry_load → WiFi → SNTP → http_server.
- [ ] Основной цикл (vTaskDelay 30000):
  - `client_registry_check_stale(360)`
  - `esp_task_wdt_feed()`
  - ESP_LOGI stats (uptime, heap, clients online)

### S8: Server Build & Test
- [ ] Полная сборка: `cd apps/server && idf.py build`
- [ ] Эмуляция client через `curl`: отправка JSON upload, проверка отображения в UI.
- [ ] Рестарт сервера: данные на месте, реестр восстановлен.

---

## Phase 4: Client Application

### C1: Partition Table (Client 4MB, no OTA)
- [ ] Создать `apps/client/partitions_client.csv`:
  - nvs (0x4000), phy_init (0x1000)
  - ota_0 (1MB), ota_1 (1MB)
  - spiffs (~1.5MB) — под offline-хранилище
- [ ] Обновить `apps/client/sdkconfig` → custom partition table.

### C2: Local Data Storage (`apps/client/main/data_storage.c/h`)
- [ ] Тот же `data_record_t` (14 байт), но без `timestamp` → `uint32_t uptime_sec`.
- [ ] Кольцевой буфер в SPIFFS (`/spiffs/offline.dat`), max ~1440 записей (сутки при 1 мин).
- [ ] API: `storage_init()`, `storage_append(rec)`, `storage_read_unsynced()`, `storage_mark_synced(count)`.

### C3: Sensor Task & Deep Sleep
- [ ] Задача `sensor_task`: read ENS160 + AHT21 → `storage_append()` → `display_show_quick()` (если wake-up по таймеру).
- [ ] Deep sleep 5 мин (300 сек) по таймеру (`esp_sleep_enable_timer_wakeup`).
- [ ] Wake-up source: GPIO12 (кнопка, active low) + таймер.
- [ ] При пробуждении по кнопке: показать текущие данные 10 сек → `display_off()` → deep sleep.
- [ ] При пробуждении по таймеру: измерение → попытка sync → deep sleep.

### C4: WiFi & Sync Task
- [ ] `wifi_manager_init_sta_from_nvs()` — читать SSID/PWD из NVS.
- [ ] Задача `sync_task`:
  - Если WiFi connected: читать `storage_read_unsynced()`, отправить `POST /api/clients/<id>/upload`.
  - При успехе: `storage_mark_synced()`.
  - При ошибке: отложить до следующего wake-up.
- [ ] Задать `client_id` через Kconfig (`CONFIG_CLIENT_ID="living_room"`) или NVS.

### C5: Display & Button (Wake-up Mode)
- [ ] При wake-up по таймеру: мгновенное измерение, короткий показ (1–2 сек) статуса (WiFi, sync), затем сон.
- [ ] При wake-up по кнопке: полный показ 10 сек:
  - Страница 1: Temp + Hum
  - Страница 2: eCO2 + TVOC + AQI
  - Страница 3: Client ID + WiFi status + uptime
  - ENS160 burn-in countdown (48 часов).
- [ ] Добавить debounce 50 мс в `button.c`.

### C6: Client Build & Test
- [ ] `cd apps/client && idf.py build`
- [ ] Тест без сервера: deep sleep → wake → измерение → локальное хранение → сон.
- [ ] Тест с сервером: данные доходят, отображаются в веб-UI.

---

## Phase 5: Quality & Robustness

- [ ] I2C retry (3 попытки с 10 мс delay) во всех драйверах.
- [ ] Sensor read timeout: если ENS160 status не ready за X сек — skip reading.
- [ ] Watchdog feed в каждом цикле (`esp_task_wdt_feed()`).
- [ ] Doxygen-комментарии к публичным API (`data_store.h`, `client_registry.h`, `http_server.h`).
- [ ] `.editorconfig` и `clang-format` (опционально).
- [ ] Удалить старый `main/` каталог в корне (если ещё есть).
- [ ] Обновить `README.md` (или `AGENTS.md`) с инструкциями по сборке и прошивке.

---

## Phase 6: Roadmap (будущие фичи)

- [ ] Alert thresholds (eCO2 > 1000 ppm, AQI > 3) — server-side логирование warning.
- [ ] Data export CSV (`GET /api/clients/<id>/export.csv`).
- [ ] Telegram бот / Push-уведомления при алертах.
- [ ] Настройка имён клиентов через веб-UI.
- [ ] Поддержка нескольких сенсоров на одном client (опционально).
