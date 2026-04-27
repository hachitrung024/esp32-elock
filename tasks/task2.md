# TASK-002: Web UI — index.html + /status endpoint

## Objective

Replace the placeholder HTTP response for `GET /` with a proper single-page UI.  
Add `GET /status` JSON endpoint.  
No changes to WiFi logic, lock logic, or other routes.

---

## Deliverables

- `src/http_server.c` — add `/status` route, replace `/` handler with static HTML
- `src/web/index.html` — single-file UI (HTML + CSS + JS, no build step)
- `CMakeLists.txt` — embed `index.html` as a C string or incbin

---

## GET /status

**Response:** `200 OK`, `Content-Type: application/json`

```json
{
  "locked": true,
  "wifi": "sta",
  "ip": "192.168.1.42",
  "temp": null,
  "humidity": null
}
```

Field definitions:

| Field | Type | Values |
|---|---|---|
| `locked` | bool | `true` = locked, `false` = unlocked |
| `wifi` | string | `"sta"` = connected, `"ap"` = AP mode, `"connecting"` = retrying |
| `ip` | string \| null | local IP if STA, `"192.168.4.1"` if AP, `null` if unknown |
| `temp` | number \| null | °C, always `null` until sensor task is implemented |
| `humidity` | number \| null | %, always `null` until sensor task is implemented |

---

## UI — index.html

### Layout

Single centered column, `max-width: 360px`, `margin: 0 auto`.  
Responsive: works on 320px phones and 1440px desktops — no horizontal scroll, no stretching.  
No external dependencies (no CDN, no framework). Vanilla HTML/CSS/JS only — must work without internet.

### Sections (top to bottom)

**1. Status row — 2 cards side by side**

- Left card: lock state
  - Label: "Khóa"
  - Value: colored dot + text
    - `locked: true` → green dot + "Đang khóa"
    - `locked: false` → red dot + "Đã mở"

- Right card: WiFi state
  - Label: "WiFi"
  - Value: colored dot + text
    - `sta` → green dot + "Đã kết nối"
    - `ap` → amber dot + "Chưa kết nối" + link "Cấu hình WiFi →" pointing to `/config`
    - `connecting` → amber dot + "Đang kết nối..."

**2. Sensor row — 2 cards side by side**

- Left: nhiệt độ — icon + value (`--°C` when `null`)
- Right: độ ẩm — icon + value (`--%` when `null`)

**3. PIN keypad card**

- Title: "Nhập mã PIN"
- 6 dot indicators (filled/empty)
- 3×4 grid:
  ```
  1  2  3
  4  5  6
  7  8  9
  ⌫  0  Mở
  ```
- Feedback line below keypad (one line height, always reserved):
  - Success: "Đã mở khóa" (green)
  - Wrong PIN: "Sai mã PIN" (red)
  - Incomplete: "Cần đủ 6 số" (red)

### PIN submission

On "Mở" press:
1. If `pin.length < 6` → show "Cần đủ 6 số", do not submit
2. `POST /unlock` with body `pin=XXXXXX`, `Content-Type: application/x-www-form-urlencoded`
3. `200` → show "Đã mở khóa" (green), clear PIN
4. `403` → show "Sai mã PIN" (red), clear PIN
5. Network error → show "Lỗi kết nối" (red), clear PIN

### Status polling

- On page load: fetch `/status` immediately, update UI
- Repeat every 1000ms via `setInterval`
- On fetch error: do not crash, retain last known state, next interval retries

### Color spec

| State | Dot color |
|---|---|
| Locked / connected | `#1D9E75` (teal-400) |
| Unlocked / AP / connecting | `#EF9F27` (amber-400) |
| Error / wrong PIN | `#E24B4A` (red-400) |

PIN keypad submit button background: `#534AB7` (purple-600), text `#EEEDFE`.  
Everything else: system CSS variables for text/bg — no hardcoded light/dark colors.

---

## Embedding index.html in firmware

Two options — Claude Code picks the simpler one for Zephyr:

**Option A — incbin (preferred):**
```cmake
generate_inc_file_for_target(app src/web/index.html
    ${ZEPHYR_BINARY_DIR}/include/generated/index_html.inc)
```
Then in C:
```c
static const char index_html[] = {
    #include <generated/index_html.inc>
    , 0
};
```

**Option B — xxd / objcopy at build time** if incbin is unavailable.

---

## Constraints

- `index.html` must be a single file — no separate CSS or JS files
- No `<form>` tags — use `fetch()` for PIN submission
- No external resources — no Google Fonts, no CDN
- Must render correctly with JS disabled for status display (JS required for PIN input — acceptable)
- `/status` handler reads from `lock_ctrl_is_locked()` and `wifi_manager_get_state()` — do not inline state logic in `http_server.c`

---

## Acceptance Criteria

- [ ] `GET /status` returns valid JSON with correct `locked` and `wifi` fields
- [ ] Status cards update every 1s without page reload
- [ ] WiFi card shows "Cấu hình WiFi →" link when `wifi == "ap"`
- [ ] PIN keypad input shows filled dots as digits entered
- [ ] POST `/unlock` with correct PIN → "Đã mở khóa" feedback
- [ ] POST `/unlock` with wrong PIN → "Sai mã PIN" feedback
- [ ] Layout renders correctly on 320px, 390px, and 1440px viewports
- [ ] Sensor placeholders show `--°C` and `--%`

---

## Out of Scope

- Sensor driver (TASK-00x)
- `/config` page UI (separate task)
- Authentication / session
- HTTPS