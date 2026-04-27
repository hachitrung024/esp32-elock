# Progress

> Current state of the codebase. Update as tasks land or hardware is verified. For stable project context (stack, build commands, environmental gotchas) see [CLAUDE.md](CLAUDE.md).

## TASK-001 — Smart Lock Core System

**Status:** implemented · **Last hardware check:** 2026-04-27

Spec: [tasks/task1.md](tasks/task1.md)

### Verified on hardware (2026-04-27)
Boot trace from a clean flash with no saved credentials:
```
*** Booting Zephyr OS build v4.3.0-9359-g56f8a30c7fd7 ***
<inf> main: SmartLock boot
<inf> fs_nvs: 8 Sectors of 4096 bytes
<inf> wifi_manager: No saved credentials, starting AP
<inf> wifi_manager: AP enabled
<inf> http_srv: HTTP server started on :80
```

### Acceptance criteria — status
- [ ] Boot with no saved config → AP `SmartLock-Setup` visible, `192.168.4.1` serves PIN page  *(AP up; HTTP page not yet hit from a client)*
- [ ] Submit valid WiFi credentials → device reboots → connects to STA → serves PIN page on local IP
- [ ] Submit wrong WiFi credentials 5 times → fallback to AP mode
- [ ] POST `/unlock` with `pin=123456` → GPIO48 HIGH for 5s → LOW
- [ ] POST `/unlock` with wrong PIN → 403, GPIO unchanged
- [ ] Press GPIO0 button → GPIO48 LOW immediately
- [x] No logic in `main.c` beyond thread spawning

## TASK-002 — Web UI + /status endpoint

**Status:** implemented, build clean · **Last hardware check:** not yet (flash blocked by USB I/O timeouts at byte ~32k — environmental, not a code issue)

Spec: [tasks/task2.md](tasks/task2.md)

### What changed
- New `/status` JSON route in [src/http_server.c](src/http_server.c) — returns `{locked, wifi, ip, temp, humidity}`. Reads `lock_ctrl_is_locked()` and `wifi_manager_get_state()` / `wifi_manager_get_ip()`; sensor fields are always `null` (placeholders for future sensor task).
- `GET /` now serves [src/web/index.html](src/web/index.html), embedded at build time via `generate_inc_file_for_target` → `${ZEPHYR_BINARY_DIR}/include/generated/index_html.inc`.
- [src/lock_ctrl.c](src/lock_ctrl.c) — added `is_unlocked` (atomic) + `lock_ctrl_is_locked()`. Set HIGH when unlock window opens, cleared on relock or `lock_ctrl_lock()`.
- [src/wifi_manager.c](src/wifi_manager.c) — added `WIFI_MGR_MODE_CONNECTING` state, `wifi_manager_get_state()` returning `"sta"`/`"ap"`/`"connecting"`/`"boot"`, and IP tracking via a `NET_EVENT_IPV4_ADDR_ADD` callback (AP mode sets `192.168.4.1` directly).

### UI ([src/web/index.html](src/web/index.html))
Single-file HTML/CSS/JS. Status overview + sensor row + 3×4 PIN keypad with 6 dots and feedback line. Polls `/status` every 1s; PIN submit POSTs `pin=...` to `/unlock` and shows OK/wrong/network feedback in Vietnamese.

### Acceptance criteria — status
- [ ] `GET /status` returns valid JSON with correct `locked` and `wifi` fields  *(handler builds the body; not yet hit from a client)*
- [ ] Status cards update every 1s without page reload
- [ ] WiFi card shows "Cấu hình WiFi →" link when `wifi == "ap"`
- [ ] PIN keypad input shows filled dots as digits entered
- [ ] POST `/unlock` with correct PIN → "Đã mở khóa" feedback
- [ ] POST `/unlock` with wrong PIN → "Sai mã PIN" feedback
- [ ] Layout renders correctly on 320px, 390px, and 1440px viewports
- [ ] Sensor placeholders show `--°C` and `--%`

### Build footprint
FLASH 663,216 B (7.91%) · DRAM 217,712 B (54.55%). +9 kB FLASH vs TASK-001.

## Module map

| File | Responsibility |
|---|---|
| [src/main.c](src/main.c) | Calls each module's `_init()` and returns. No business logic. |
| [src/config.h](src/config.h) | PIN, GPIO numbers, WiFi defaults, settings keys. |
| [src/lock_ctrl.c](src/lock_ctrl.c) | GPIO48. Owns `lock_thread` for the timed-unlock sequence. `lock_ctrl_unlock()` posts a request via sem; `lock_ctrl_lock()` drives GPIO low immediately and short-circuits the unlock window via `lock_now_sem`. Exposes `lock_ctrl_is_locked()` (atomic flag). |
| [src/button.c](src/button.c) | GPIO0 polling thread with 50 ms software debounce. Press → `lock_ctrl_lock()`. |
| [src/wifi_manager.c](src/wifi_manager.c) | Loads creds from NVS via `SETTINGS_STATIC_HANDLER_DEFINE`. STA with 5 × 3s retries (mode = `CONNECTING` during retries); AP fallback to `SmartLock-Setup` on `192.168.4.1` with DHCPv4 server. Reboot-after-config via `k_work_delayable`. Tracks IP from `NET_EVENT_IPV4_ADDR_ADD`. Exposes `wifi_manager_get_state()` (string) + `wifi_manager_get_ip()`. |
| [src/http_server.c](src/http_server.c) | Four routes registered statically (`HTTP_RESOURCE_DEFINE`): `/`, `/status`, `/unlock`, `/config`. `/config` enforces AP-mode at handler entry. `http_thread` waits for `wifi_manager_network_ready()` before calling `http_server_start()`. `index_html` embedded via `generate_inc_file_for_target`. |
| [src/web/index.html](src/web/index.html) | Single-file UI: status cards, sensor row, PIN keypad. Polls `/status` every 1s, POSTs PIN to `/unlock`. |

Build glue: [CMakeLists.txt](CMakeLists.txt), [prj.conf](prj.conf), [app.overlay](app.overlay), [sections-rom.ld](sections-rom.ld), [socs/esp32s3_procpu.overlay](socs/esp32s3_procpu.overlay).

## Threading model (current)

| Thread | Stack | Prio | Owner |
|---|---|---|---|
| `wifi_thread` | 4096 | 5 | wifi_manager |
| `http_thread` | 4096 | 7 | http_server |
| `button_thread` | 1024 | 3 | button |
| `lock_thread` | 1024 | 4 | lock_ctrl (internal) |

`main()` only spawns these and returns. Reboot-after-config uses the system work queue (delayable, non-blocking).

## Open follow-ups

- TASK-001 + TASK-002 acceptance pass on hardware once the USB flash issue clears (replug device / different host port / stable cable). Need to exercise: HTTP `/`, `/status`, `/unlock` valid + invalid, `/config` save+reboot, STA fallback after 5 failed retries, button press → lock, UI render at 320 / 390 / 1440 px.
