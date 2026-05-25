# TODO

## Согласованные архитектурные решения

- **Client ID**: кастомный строковый ID (`living_room`, `kitchen` и т.д.), задается при прошивке через Kconfig/NVS.
- **Время**: гибрид — сервер ставит `time(NULL)` при получении. Client отправляет `uptime_sec` как fallback.
- **Хранение**: бинарный packed формат (14 байт/запись), кольцевой буфер per-client в SPIFFS.
- **Веб-интерфейс**: zero-SPIFFS — HTML/CSS/JS встроены в `.rodata` через `const char[]`. Весь SPIFFS под данные.
- **WiFi Server**: AP mode (`AirMon-Server`) + STA fallback (если заданы креденшиалы).
- **Client**: мониторинговый режим — deep sleep 5 мин → измерение → отправка → сон. Кнопка GPIO12: wake-up, показ текущих данных 10 сек.
- **Upload**: JSON (`POST /api/upload?id=<id>` с телом `{"readings":[{"ts","up","t","h","c","v","a"}]}`), хранение на сервере — бинарное.
- **API стиль**: ESP-IDF v3.4 HTTP-сервер не поддерживает wildcard `*` в URI. Все эндпоинты используют точные URI с query-параметрами (`?id=xxx`).
- **OTA**: не нужен ни на сервере, ни на клиенте.

---

## Phase 1: Server — Partition Table & Data Store

### S1: Partition Table (Server 16MB, no OTA)
- [x] Создать `apps/server/partitions_server.csv`:
  - nvs (0x4000), phy_init (0x1000)
  - ota_0 (1MB), ota_1 (1MB) — оставить на всякий случай, но OTA не используется
  - spiffs (~14MB, offset 0x210000) — под данные клиентов
- [x] Обновить `apps/server/sdkconfig`:
  - `CONFIG_PARTITION_TABLE_CUSTOM=y`
  - `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_server.csv"`
- [x] Проверка: `cd apps/server && idf.py build` проходит без ошибок.

### S2: Data Store Module (`apps/server/main/data_store.c/h`)
- [x] Определить `data_record_t` (packed, 14 байт):
  - `uint32_t timestamp`, `int16_t temp*100`, `uint16_t hum*100`, `uint16_t eco2`, `uint16_t tvoc`, `uint8_t aqi`, `uint8_t _pad`
- [x] Определить `client_file_header_t` (magic, version, client_id[32], max_records, write_idx, count)
- [x] Реализовать `data_store_init()` — mount SPIFFS, форматировать при первом старте.
- [x] Реализовать `data_store_append(client_id, rec)` — циклическая запись в `/spiffs/<id>.dat`.
- [x] Реализовать `data_store_read_range(client_id, offset, limit, out, capacity)` — чтение записей.
- [x] Реализовать `data_store_get_count(client_id)`.
- [x] Реализовать `data_store_delete_client(client_id)`.
- [x] Тест в `main.c`: append 3 записи, read, print — проверить через логи.

### S3: Client Registry (`apps/server/main/client_registry.c/h`)
- [x] `client_info_t` (id[32], name[32], last_seen, online, ip_str[16]).
- [x] Массив в RAM (max 16 клиентов).
- [x] `client_registry_init()`, `update()`, `get()`, `get_all()`.
- [x] `client_registry_check_stale(timeout_sec=360)` — пометить offline.
- [x] Сохранение/загрузка в NVS (`client_registry_save/load`).
- [x] Тест: рестарт сервера → реестр восстанавливается из NVS.

---

## Phase 2: Server — HTTP REST API

### S4: HTTP REST API (`apps/server/main/http_server.c/h`)
- [x] `http_server_init()`, `http_server_start()`.
- [x] Endpoint `GET /api/health` → JSON: status, uptime, free_heap, clients_online.
- [x] Endpoint `GET /api/clients` → JSON-массив: id, name, online, last_seen, latest values.
- [x] Endpoint `GET /api/client?id=<id>` → JSON: info + последние N записей (default 24).
- [x] Endpoint `POST /api/upload?id=<id>` → принимает `{"readings":[...]}` JSON:
  - Парсинг `cJSON`, конвертация в `data_record_t` (timestamp = time(NULL)).
  - `data_store_append()` + `client_registry_update()`.
- [x] Endpoint `GET /api/data?id=<id>&limit=&offset=` → JSON-массив записей для графиков.
- [x] **Важно**: ESP-IDF v3.4 не поддерживает wildcard `*` в URI (только точное сравнение `strncmp`). Все эндпоинты используют точные URI с query-параметрами.
- [x] Проверка через upload клиента: `accepted 5 records` — работает.
- [ ] Проверка через `curl` извне (требуется WiFi подключение к AP).

### S5: Embedded Web UI (`apps/server/main/web_ui.h`)
- [x] `index_html[]` — одностраничный UI, inline CSS + JS.
- [x] Таблица клиентов: ID, имя, статус (online/offline), время, текущие значения.
- [x] Детальная карточка по клику: последние значения крупно + `<canvas>` график (24 точки).
- [x] Автообновление: `fetch('/api/clients')` каждые 5 сек.
- [x] Тёмная тема, моноширинные цифры, AQI-индикаторы (цвета).
- [x] Подключить к `http_server.c` (URI `/` → `index_html`).
- [ ] Проверка: браузер открывает страницу, видит данные.

---

## Phase 3: Server — WiFi, NTP & Integration

### S6: WiFi Manager Доработка (`components/wifi/`)
- [x] Добавить `wifi_manager_init_ap_with_sta_fallback(ap_ssid, ap_pass, sta_ssid, sta_pass)`.
- [x] Если STA креденшиалы заданы — `WIFI_MODE_APSTA`, попытка подключения.
- [x] Если STA не удался за 30 сек — оставить только AP.
- [x] `wifi_manager_get_ap_ip()` — получить IP AP-интерфейса.

### S7: NTP & Main Loop
- [x] Если STA подключился — запустить SNTP (`esp_netif_sntp_init` или `sntp_setoperatingmode`).
- [x] Подождать синхронизации времени.
- [x] `main.c` flow: NVS → data_store → registry_load → WiFi → SNTP → http_server.
- [x] Основной цикл (vTaskDelay 30000):
  - `client_registry_check_stale(360)`
  - `esp_task_wdt_feed()`
  - ESP_LOGI stats (uptime, heap, clients online)

### S8: Server Build & Test
- [x] Полная сборка: `cd apps/server && idf.py build`
- [x] Сервер запущен, WiFi AP `AirMon-Server` работает, HTTP API отвечает
- [x] Рестарт сервера: данные на месте, реестр восстановлен.
- [ ] Эмуляция client через `curl`: отправка JSON upload, проверка отображения в UI.

---

## Phase 4: Client Application

### C1: Partition Table (Client 4MB, no OTA)
- [x] Создать `apps/client/partitions_client.csv`:
  - nvs (0x4000), phy_init (0x1000)
  - factory (1MB), storage/SPIFFS (~2.9MB)
- [x] Обновить `apps/client/sdkconfig` → custom partition table.
- [x] Проверка: `cd apps/client && idf.py build` проходит.

### C2: Local Data Storage (`apps/client/main/data_storage.c/h`)
- [x] `client_record_t` (packed, 15 байт с полем `synced`): timestamp, uptime_sec, temperature, humidity, eco2, tvoc, aqi, synced.
- [x] Кольцевой буфер в SPIFFS (`/spiffs/client_data.dat`), max 5000 записей.
- [x] API: `data_storage_init()`, `append()`, `read_unsynced()`, `mark_synced()`, `get_count()`, `clear_all()`.

### C3: Sensor Reading Task
- [x] I2C init через `display_init()` (SDA=GPIO4, SCL=GPIO5).
- [x] Инициализация ENS160 + AHT21 при старте.
- [x] ENS160: исправлен OPMODE с `0x01` (IDLE) → `0x02` (STANDARD) — после исправления сенсор работает.
- [x] Периодическое чтение сенсоров (каждые 5 сек для отладки).
- [x] Сохранение readings в локальный `data_storage`.
- [x] Работает стабильно, без крашей (подтверждено >3 минут).
- [ ] Deep sleep (5 мин) — пока отключено для отладки.

### C4: WiFi & Sync Task
- [x] Подключение к серверу (AP `AirMon-Server`, пароль `12345678`).
- [x] POST JSON upload на `http://192.168.4.1/api/upload?id=test_client`.
- [x] Client ID: `test_client` (hardcoded для отладки).
- [x] Обработка ответа сервера, mark_synced при успехе.
- [x] JSON строится вручную через `snprintf` (без cJSON — не хватает heap на ESP8266).
- [x] **Важно**: ESP8266 newlib не поддерживает `%f` в printf/snprintf. Все float → int через `(int)(val*10)%10`.
- [x] Повтор каждые 30 сек.
- [x] Статус: `Upload OK, status=200` — данные успешно принимаются сервером.

### C5: Display & Button
- [x] Инициализация SSD1306 (I2C addr 0x3C, 128x32) при старте.
- [x] Показ "Client / Ready" при запуске.
- [x] Постоянное отображение данных: при каждом чтении сенсоров (каждые 5 сек) дисплей обновляется:
  - Строка 1: `T±±.±C HH% AQI±` (температура, влажность, AQI)
  - Строка 2: `eCO2±±±± TVOC±±±` (показания ENS160)
- [ ] Кнопка (GPIO12) для переключения страниц — отключено для отладки.

### C6: Client Build & Test
- [x] `cd apps/client && idf.py build` — успешно.
- [x] Прошивка на устройство (`/dev/ttyUSB0`) — успешно.
- [x] Инициализация всех компонентов — успешно.
- [x] AHT21: 26-29°C, 46-53% RH — реальные показания, сенсор работает.
- [x] ENS160: eCO2=400-600 ppm, TVOC=10-130 ppb, AQI=1-2 — реальные показания, сенсор работает.
- [x] Сохранение данных в SPIFFS — работает (1000+ записей сохранено).
- [x] WiFi подключение к серверу — работает (клиент получает IP 192.168.4.2).
- [x] HTTP upload — статус 200, `accepted 5 records`, данные на сервере.
- [x] Display — показывает текущие показатели, обновляется каждые 5 сек.
- [x] Стабильность: 3+ минут без крашей, heap=62K стабильно.
- [ ] Web UI: проверить отображение данных в браузере (требуется WiFi подключение к AP).

---

## Phase 5: Quality & Robustness

- [ ] I2C retry (3 попытки с 10 мс delay) во всех драйверах (ENS160 и AHT21).
- [ ] Sensor read timeout: если ENS160 status не ready за X сек — skip reading (уже добавлен 3× retry).
- [ ] Watchdog feed в каждом цикле (`esp_task_wdt_feed()`).
- [ ] Doxygen-комментарии к публичным API (`data_store.h`, `client_registry.h`, `http_server.h`).
- [ ] `.editorconfig` и `clang-format` (опционально).
- [ ] Удалить старый `main/` каталог в корне (если ещё есть).
- [ ] Client ID: заменить hardcoded `test_client` на Kconfig/NVS.
- [ ] Timestamp на сервере: `time(NULL)` = 0 (нет NTP в AP-only режиме). Заменить на uptime с момента запуска сервера.
- [ ] Web UI: проверить корректность отображения данных извне (через WiFi подключение к `AirMon-Server`).
- [ ] README.md для каждой компоненты и приложения (display, sensors, wifi, fonts, server, client).
- [ ] Процедура первоначальной настройки: запись STA credentials в NVS сервера (wifi:sta_ssid + sta_pass) для подключения к домашней WiFi.

---

## Phase 6: Roadmap (будущие фичи)

- [ ] Alert thresholds (eCO2 > 1000 ppm, AQI > 3) — server-side логирование warning.
- [ ] Data export CSV (`GET /api/clients/<id>/export.csv`).
- [ ] Telegram бот / Push-уведомления при алертах.
- [ ] Настройка имён клиентов через веб-UI.
- [ ] Поддержка нескольких сенсоров на одном client (опционально).
