# TASK-001: Smart Lock — Core System

## Objective

Implement the foundational smart lock firmware on ESP32-S3 with Zephyr RTOS.  
Covers: WiFi lifecycle, HTTP PIN entry, GPIO lock/unlock, button lock.

---

## Deliverables

- Working firmware that boots, manages WiFi state, serves web UI, controls lock GPIO
- Source split into modules (no monolithic main.c)
- All threads explicitly defined — no ad-hoc work queue usage for blocking ops

---

## Module Structure

```
src/
├── main.c              — entry point, thread spawn only, no logic
├── wifi_manager.c/h    — WiFi state machine (AP / STA / fallback)
├── http_server.c/h     — HTTP server, route handlers
├── lock_ctrl.c/h       — GPIO lock control (lock/unlock)
├── button.c/h          — GPIO0 button handler
└── config.h            — compile-time constants (PIN, timeouts, GPIO defs)
```

---

## Thread Model

| Thread | Stack | Priority | Responsibility |
|---|---|---|---|
| `wifi_thread` | 4096 | 5 | WiFi state machine, connection retries |
| `http_thread` | 4096 | 7 | HTTP server lifecycle |
| `button_thread` | 1024 | 3 | Poll GPIO0, trigger lock |

- `main()` spawns all threads then returns (or idles)
- No business logic in `main.c`
- System work queue: ISR-safe signaling only — no blocking ops

---

## WiFi State Machine

```
BOOT
  └─ Load config from Settings (NVS)
       ├─ No config → AP_MODE
       └─ Config exists → STA_CONNECT
            ├─ Success → STA_MODE
            └─ Fail (5 retries, 3s between) → AP_MODE
```

**AP_MODE:**
- SSID: `SmartLock-Setup`, no password
- IP: `192.168.4.1`
- Hosts: `/` (PIN entry) + `/config` (WiFi credentials form)
- On valid WiFi credentials submitted → save to Settings → reboot

**STA_MODE:**
- Connect to configured SSID
- Obtain IP via DHCP
- Hosts: `/` (PIN entry only)
- No AP interface active

---

## HTTP Routes

### `GET /`
Returns HTML form: single PIN input (6 digits), submit button.

### `POST /unlock`
Body: `pin=XXXXXX` (application/x-www-form-urlencoded)  
- PIN correct (`123456`) → call `lock_ctrl_unlock()` → respond 200  
- PIN wrong → respond 403  
- No URL decoding required (PIN is digits only)

### `GET /config` — AP mode only
Returns HTML form: SSID input, password input, submit button.

### `POST /config` — AP mode only
Body: `ssid=...&password=...`  
- Save to Zephyr Settings  
- Respond 200 with "Rebooting..." message  
- Schedule `sys_reboot(SYS_REBOOT_COLD)` after 1s via `k_work_delayable`

---

## Lock Control (lock_ctrl)

**GPIO:** GPIO48 (`gpio1`, pin 16 on ESP32-S3)  
**Active HIGH:** pin HIGH = unlocked, pin LOW = locked

```
lock_ctrl_unlock():
  set GPIO48 HIGH
  wait 5 seconds (dedicated thread sleep, not work queue)
  set GPIO48 LOW

lock_ctrl_lock():
  set GPIO48 LOW immediately
```

`lock_ctrl` owns a dedicated internal thread (`lock_thread`, stack 1024, priority 4) for the timed unlock sequence. Caller is non-blocking.

---

## Button (button)

**GPIO:** GPIO0  
**Active LOW** (internal pull-up enabled)  
**Debounce:** 50ms software debounce in `button_thread`

Behavior: press → `lock_ctrl_lock()`

> Note: GPIO0 is a strapping pin on ESP32-S3. Acceptable for academic use.  
> Risk: holding during power-on may trigger download mode.

---

## Configuration (config.h)

```c
#define LOCK_PIN_CODE        "123456"
#define LOCK_UNLOCK_DURATION_S  5
#define WIFI_RETRY_COUNT     5
#define WIFI_RETRY_DELAY_MS  3000
#define WIFI_AP_SSID         "SmartLock-Setup"
#define WIFI_AP_CHANNEL      6
#define GPIO_LOCK            48
#define GPIO_BUTTON          0
```

---

## prj.conf (required Kconfig)

```
CONFIG_WIFI=y
CONFIG_WIFI_ESP32=y

CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_SOCKETS_POSIX_NAMES=y
CONFIG_NET_DHCPV4=y
CONFIG_NET_DHCPV4_SERVER=y

CONFIG_HTTP_SERVER=y
CONFIG_HTTP_SERVER_MAX_CLIENTS=3
CONFIG_HTTP_SERVER_MAX_URL_LENGTH=256

CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_NVS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y

CONFIG_GPIO=y
CONFIG_LOG=y
```

---

## app.overlay (additions)

```dts
/ {
    aliases {
        lock-out = &lock_gpio_node;
        btn-lock = &btn_gpio_node;
    };

    lock_gpio_node: lock-output {
        compatible = "gpio-leds";
        lock_pin { gpios = <&gpio1 16 GPIO_ACTIVE_HIGH>; };
    };

    btn_gpio_node: lock-button {
        compatible = "gpio-keys";
        btn_pin { gpios = <&gpio0 0 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>; };
    };
};
```

---

## Acceptance Criteria

- [ ] Boot with no saved config → AP `SmartLock-Setup` visible, `192.168.4.1` serves PIN page
- [ ] Submit valid WiFi credentials → device reboots → connects to STA → serves PIN page on local IP
- [ ] Submit wrong WiFi credentials 5 times → fallback to AP mode
- [ ] POST `/unlock` with `pin=123456` → GPIO48 HIGH for 5s → LOW
- [ ] POST `/unlock` with wrong PIN → 403, GPIO unchanged
- [ ] Press GPIO0 button → GPIO48 LOW immediately
- [ ] No logic in `main.c` beyond thread spawning

---

## Out of Scope

- HTTPS / TLS
- PIN change via web UI
- Multiple PINs / users
- Attempt rate limiting
- OTA