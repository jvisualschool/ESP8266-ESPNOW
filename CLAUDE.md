# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP-NOW 7-in-1 Demo: two ESP8266 NodeMCU v2 boards communicate wirelessly via ESP-NOW (WiFi peer-to-peer, no router required), each driving a 128×64 SSD1306 OLED display. The boards cycle through 7 interactive demos every 10 seconds.

**Board MACs:**
- ESP_A (master): `8c:4f:00:f4:bc:3a`
- ESP_B (slave): `48:3f:da:86:b1:c7`

## Build & Flash Commands (PlatformIO)

```bash
# Build
pio run -e esp_a
pio run -e esp_b

# Upload (flash to connected board)
pio run -e esp_a -t upload
pio run -e esp_b -t upload

# Monitor serial output (115200 baud)
pio device monitor

# Upload + immediately monitor
pio run -e esp_a -t upload && pio device monitor

# Clean build artifacts
pio run -t clean
```

## Architecture

### Two Separate Firmware Targets
`platformio.ini` defines `[env:esp_a]` and `[env:esp_b]`, each compiling only its own source file via `build_src_filter`.

### Hardware Wiring
- OLED SSD1306 (I2C 0x3C): SDA → D5 (GPIO14), SCL → D6 (GPIO12)

### Boot Splash
On power-on, before pairing, each board shows ESP/NOW text (full screen, 1 second each, 3 total):
- ESP_A sequence: `ESP → NOW → ESP`
- ESP_B sequence: `NOW → ESP → NOW`

### Message Protocol
6-byte packed struct over ESP-NOW:
```cpp
struct Message {
  uint8_t demoMode;   // current demo index (0–6)
  uint8_t msgType;    // MsgType enum
  int32_t value;      // payload
} __attribute__((packed));
```

**MsgType enum (0–9):**
```
MSG_PAIR_REQ=0, MSG_PAIR_ACK=1, MSG_BALL=2, MSG_ICON=3,
MSG_COUNTER=4, MSG_DEMO_SYNC=5, MSG_SEESAW=6, MSG_MORSE=7,
MSG_SNAKE=8, MSG_EYE=9
```

**MSG_EYE encoding:** `phase*100000 + (ox+20)*1000 + (oy+20)*10 + blink`
- `phase`: 0 = two small eyes, 1 = one big eye
- `ox`, `oy`: pupil offset ±8, ±4 (shifted +20 to keep positive)
- `blink`: 0 or 1

### Pairing Flow
Both boards broadcast `MSG_PAIR_REQ` every 500ms. On receipt, save sender MAC, register unicast peer, reply `MSG_PAIR_ACK`. After pairing, all messages use stored peer MAC.

### Master/Slave Roles
- **ESP_A**: controls 10-second demo cycling, sends `MSG_DEMO_SYNC` on each transition
- **ESP_B**: follows A's sync. Safety fallback advances demo after 12 seconds without sync

### Demo Order & Logic

| Index | Name | A role | B role |
|-------|------|--------|--------|
| 0 | EYES | Computes ox/oy/blink/phase, sends MSG_EYE every 50ms | Renders received values |
| 1 | PING-PONG | Ball starts on screen | Ball starts off screen |
| 2 | EMOJI | `myTurnToSend=true` (sends first) | `myTurnToSend=false` (waits) |
| 3 | COUNTER | `myTurnToCount=true` (counts first) | `myTurnToCount=false` (waits) |
| 4 | SEESAW | Computes `sin(millis()/1400)*12`, sends MSG_SEESAW every 50ms | Renders received offset |
| 5 | MORSE | Sends SOS `...---...` first | Sends OK `---.-.` after receiving 99 |
| 6 | SNAKE | Snake starts on A | Waits; snake arrives via MSG_SNAKE |

**EYES phases:**
- 0–3s (phase 0): each board shows 2 small eyes (ER=16, PR=7) at x=32,96
- 3–10s (phase 1): each board shows 1 big eye (ER=24, PR=11) centered at x=64

**Ball/Snake encoding:** `position*100 + (velocity*10 + 50)` packs float position+velocity into int32.

**Morse encoding:** `MSG_MORSE` value: 0=dot, 1=dash, 99=done (triggers turn swap).

### Display Layout
- y=0–9: white header bar (board name + demo name, black text)
- y=10–12: countdown progress bar (full→empty over 10s)
- y=13–63: demo content area

### Adding a New Demo
1. Add `MsgType` enum value (both files, same value)
2. Declare global state variables
3. Add receive handler in `onDataRecv()` switch
4. Add state reset in demo cycling block — update `% N` modulo in both files
5. Add state reset in `MSG_DEMO_SYNC` handler
6. Implement `drawDemoN()` function
7. Add name to `demoNames[]` array
8. Add `case N:` to `switch(currentDemo)`

### Common Pitfalls
- **Wrong `%` modulo**: `MSG_DEMO_SYNC` handler in both files must use `% (total demos)`. Using wrong value causes demo index wrap to wrong demo (e.g., `% 6` when 7 demos exist maps demo 6 → 0).
- **IDE clang errors** (`'Arduino.h' not found`, `Unknown type name 'uint8_t'`): IDE-only false positives. PlatformIO builds always succeed.
- **Wrong board uploaded**: Always verify MAC in upload output matches expected board before testing.

### Dependencies
- `adafruit/Adafruit SSD1306@^2.5.7`
- `adafruit/Adafruit GFX Library@^1.11.5`
- ESP8266 Arduino core provides `ESP8266WiFi.h` and `espnow.h`
