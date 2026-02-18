# ESP-NOW 7-in-1 Interactive Demo

ESP8266 NodeMCU 2대가 ESP-NOW 프로토콜로 무선 통신하며 SSD1306 OLED에 7가지 인터랙티브 데모를 10초씩 순환 표시하는 프로젝트.

---

## 목차

1. [하드웨어 구성](#하드웨어-구성)
2. [ESP-NOW 프로토콜 상세](#esp-now-프로토콜-상세)
3. [프로젝트 아키텍처](#프로젝트-아키텍처)
4. [메시지 프로토콜](#메시지-프로토콜)
5. [자동 페어링 메커니즘](#자동-페어링-메커니즘)
6. [부팅 스플래시](#부팅-스플래시)
7. [7가지 데모 상세](#7가지-데모-상세)
8. [빌드 및 업로드](#빌드-및-업로드)

---

## 하드웨어 구성

| 항목 | 사양 |
|------|------|
| MCU | ESP8266 NodeMCU v2 (ESP-12E) × 2 |
| 디스플레이 | SSD1306 OLED 0.96" 128×64 I2C × 2 |
| 통신 | ESP-NOW (802.11 Wi-Fi 기반, 라우터 불필요) |
| 전원 | USB 5V 또는 3.3V 외부 전원 |

### 핀 연결 (양쪽 보드 동일)

```
SSD1306 OLED      NodeMCU v2
─────────────     ──────────
SDA          →    D5 (GPIO14)
SCL          →    D6 (GPIO12)
VCC          →    3.3V
GND          →    GND
```

> I2C 주소: `0x3C` (기본값)

---

## ESP-NOW 프로토콜 상세

### 개요

ESP-NOW는 Espressif가 개발한 경량 무선 통신 프로토콜로, Wi-Fi 물리 레이어(802.11)를 사용하지만 TCP/IP 스택이나 라우터 없이 장치 간 직접 통신한다.

### 핵심 특징

| 항목 | 내용 |
|------|------|
| 최대 페이로드 | 250 bytes |
| 최대 등록 피어 수 | 20개 (암호화 시 10개) |
| 전송 속도 | 최대 1 Mbps |
| 통신 거리 | 약 200m (개방 공간, 안테나 조건에 따라 다름) |
| 지연 시간 | 약 1ms 이하 (TCP/IP 대비 매우 낮음) |
| 전력 소비 | Wi-Fi 연결 대비 훨씬 낮음 |
| 보안 | CCMP 방식 128-bit AES 암호화 지원 (옵션) |

### 작동 방식

ESP-NOW는 Wi-Fi의 Action Frame을 재활용한다. 일반 Wi-Fi 패킷처럼 생겼지만 AP와 연결하지 않고 MAC 주소만 알면 직접 데이터를 보낼 수 있다.

```
송신 측                           수신 측
────────                          ────────
esp_now_send(peerMAC, data, len)
        │
        ▼
  802.11 Action Frame
  (Wi-Fi 채널 1)
        │
        └──────────────────────▶  onDataRecv(mac, data, len) 콜백 호출
```

### ESP8266에서의 제약

- **Wi-Fi 모드**: `WIFI_STA` 또는 `WIFI_AP` 모드에서만 동작. `WIFI_OFF`에서는 불가.
- **채널**: 기본적으로 채널 1 사용. AP 모드와 혼용 시 채널을 맞춰야 함.
- **역할(Role)**: `ESP_NOW_ROLE_COMBO`로 설정하면 송수신 모두 가능.
- **피어 등록**: 송신 전에 반드시 `esp_now_add_peer()`로 대상 MAC을 등록해야 함.
- **브로드캐스트**: `FF:FF:FF:FF:FF:FF`로 피어 등록 후 송신하면 근처 모든 ESP-NOW 장치에 전달됨.

### 콜백 구조

```cpp
// 초기화 순서
WiFi.mode(WIFI_STA);
WiFi.disconnect();
esp_now_init();
esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
esp_now_register_send_cb(onDataSent);   // 전송 완료 콜백
esp_now_register_recv_cb(onDataRecv);  // 수신 콜백

// 수신 콜백 시그니처 (ESP8266)
void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len)

// 송신 완료 콜백 시그니처 (ESP8266)
void onDataSent(uint8_t *mac, uint8_t status)
// status: 0 = 성공, 1 = 실패
```

> **주의**: ESP32와 ESP8266의 콜백 시그니처가 다름.
> ESP32는 `const uint8_t *mac`, `const uint8_t *data`, `int len`.

---

## 프로젝트 아키텍처

### 마스터/슬레이브 구조

```
ESP_A (Master)                    ESP_B (Slave)
─────────────                     ─────────────
데모 타이머 관리                   A의 MSG_DEMO_SYNC 수신 대기
10초마다 nextDemo 계산             수신 시 즉시 상태 리셋
MSG_DEMO_SYNC 브로드캐스트 ──────▶ currentDemo 갱신
자체 상태 리셋                     (+2초 안전 타임아웃 내장)
```

- **ESP_A**는 데모 전환의 유일한 타이머 권한을 갖는다.
- **ESP_B**는 A의 `MSG_DEMO_SYNC`를 받아 동기화하고, 12초(DEMO_DURATION + 2000ms) 이내에 신호가 없으면 자체 전환(안전 폴백).

### 파일 구조

```
ESP-NOW/
├── platformio.ini      # 빌드 환경 설정 (esp_a, esp_b 두 타겟)
├── CLAUDE.md           # Claude Code 가이드
├── README.md           # 이 파일
└── src/
    ├── esp_a.cpp       # Board A 펌웨어 (마스터)
    └── esp_b.cpp       # Board B 펌웨어 (슬레이브)
```

`platformio.ini`의 `build_src_filter`로 각 환경이 자신의 파일만 컴파일한다.

```ini
[env:esp_a]
build_src_filter = +<esp_a.cpp>   # esp_a.cpp만 컴파일

[env:esp_b]
build_src_filter = +<esp_b.cpp>   # esp_b.cpp만 컴파일
```

---

## 메시지 프로토콜

### 패킷 구조

```cpp
struct Message {
  uint8_t demoMode;   // 현재 데모 인덱스 (0~6)
  uint8_t msgType;    // MsgType enum 값
  int32_t value;      // 페이로드 (msgType에 따라 의미 다름)
} __attribute__((packed));
// 총 크기: 6 bytes (250 bytes 한계의 2.4%)
```

`__attribute__((packed))`로 구조체 패딩을 제거해 최소 크기를 보장한다.

### MsgType 정의

| 값 | 이름 | value 의미 |
|----|------|-----------|
| 0 | `MSG_PAIR_REQ` | 페어링 요청 (value 미사용) |
| 1 | `MSG_PAIR_ACK` | 페어링 응답 (value 미사용) |
| 2 | `MSG_BALL` | 공 전달: `y * 100 + (vy * 10 + 50)` |
| 3 | `MSG_ICON` | 아이콘 인덱스: 0=Heart, 1=Star, 2=Smile |
| 4 | `MSG_COUNTER` | 현재 카운터 값 |
| 5 | `MSG_DEMO_SYNC` | 다음 데모 인덱스 (0~6) |
| 6 | `MSG_SEESAW` | 시소 기울기 오프셋 (-12 ~ +12) |
| 7 | `MSG_MORSE` | 0=dot, 1=dash, 99=전송완료 |
| 8 | `MSG_SNAKE` | `y * 10 + (vy + 5)` |
| 9 | `MSG_EYE` | `phase*100000 + (ox+20)*1000 + (oy+20)*10 + blink` |

### 값 인코딩 방식

부동소수점을 피하고 int32_t 하나에 여러 값을 담는 방식을 사용한다.

**공/스네이크 위치+속도 인코딩:**
```
encoded = position * 100 + (velocity * 10 + 50)

복원:
  position = encoded / 100
  velocity = (encoded % 100 - 50) / 10.0
```

예: `y=32.0, vy=1.5` → `32*100 + (15+50)` = `3265`
복원: `3265/100` = `32`, `(3265%100 - 50)/10.0` = `1.5`

**눈동자 위치+페이즈 인코딩 (`MSG_EYE`):**
```
encoded = phase * 100000 + (ox + 20) * 1000 + (oy + 20) * 10 + blink

복원:
  phase     = encoded / 100000
  remainder = encoded % 100000
  ox        = remainder / 1000 - 20
  oy        = (remainder % 1000) / 10 - 20
  blink     = remainder % 10
```

`ox`, `oy` 범위 ±8에 +20 오프셋을 더해 항상 양수로 유지.
`phase` (0 또는 1)를 100000 단위로 분리해 단일 int32_t에 모두 수용.

---

## 자동 페어링 메커니즘

라우터나 사전 MAC 주소 설정 없이 전원을 켜면 자동으로 1:1 페어링된다.

```
ESP_A                              ESP_B
─────                              ─────
브로드캐스트로 MSG_PAIR_REQ 전송 ──▶ MAC 저장, 유니캐스트 피어 등록
(500ms 간격)                        MSG_PAIR_ACK 응답 전송
                              ◀──  (자신의 MAC이 포함된 프레임)
수신한 MAC으로 피어 등록
paired = true                      paired = true
demoStartTime = millis()           demoStartTime = millis()
                    데모 시작
```

**양방향 개시**: A와 B 모두 `MSG_PAIR_REQ`를 보내므로, 어느 쪽이 먼저 켜져도 나중에 켜진 쪽의 요청에 먼저 켜진 쪽이 응답하는 방식으로 페어링이 완성된다.

---

## 부팅 스플래시

페어링 화면 이전, 부팅 직후 3초간 ESP/NOW 텍스트를 흰 바탕에 크게 표시한다.

```
A 화면:  ESP → NOW → ESP   (1초씩)
B 화면:  NOW → ESP → NOW   (1초씩)
```

두 보드를 나란히 놓으면 항상 반대 텍스트가 표시되어 ESP / NOW 쌍을 이룬다.

---

## 7가지 데모 상세

### 데모 순서

| 인덱스 | 이름 | 설명 |
|--------|------|------|
| 0 | EYES | 싱크된 눈동자 (2단계 페이즈) |
| 1 | PING-PONG | 공이 두 화면 사이를 왕복 |
| 2 | EMOJI | 아이콘을 번갈아 전송 |
| 3 | COUNTER | 공유 카운터 번갈아 증가 |
| 4 | SEESAW | 실시간 시소 동기화 |
| 5 | MORSE | 모스부호 교환 |
| 6 | SNAKE | 뱀이 화면 사이를 이동 |

### 공통 화면 레이아웃

```
┌─────────────────────────────┐  y=0
│ ESP_A    EYES               │  헤더 바 (흰색 배경, 검은 글씨) y=0~9
├─────────────────────────────┤  y=10
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░│  타임바 (10초 카운트다운) y=10~12
├─────────────────────────────┤  y=13
│                             │
│        데모 콘텐츠 영역      │  y=13~63
│                             │
└─────────────────────────────┘  y=63
```

타임바는 `DEMO_DURATION(10s)` 동안 왼쪽에서 오른쪽으로 줄어든다.

---

### Demo 0 — EYES

**개념**: 두 보드의 눈동자가 같은 방향으로 싱크되어 움직이고 함께 깜빡인다. 10초 동안 2단계로 변화한다.

**2단계 페이즈:**

| 시간 | 페이즈 | A 화면 | B 화면 |
|------|--------|--------|--------|
| 0~3초 | phase 0 | 눈 2개 (ER=16) | 눈 2개 (ER=16) |
| 3~10초 | phase 1 | 눈 1개 크게 (ER=24) | 눈 1개 크게 (ER=24) |

두 보드를 나란히 두면 phase 1에서 하나의 커다란 얼굴처럼 보인다.

**눈 렌더링 (`drawEye`)**:
```cpp
void drawEye(int cx, int cy, int ox, int oy, bool blink, int ER, int PR);
// ER = 눈 반지름, PR = 눈동자 반지름
// phase 0: ER=16, PR=7  (작은 눈)
// phase 1: ER=24, PR=11 (큰 눈)
```

- **흰자 (sclera)**: 반지름 ER, ER-1의 이중 원 테두리
- **눈동자**: 반지름 PR의 채운 원, `(cx+ox, cy+oy)` 위치로 이동
- **깜빡임 (blink)**: 아몬드 모양 4선으로 감은 눈 표현

**움직임 계산 (A만, 50ms 전송)**:
```cpp
float t = millis() / 1500.0;
int8_t ox = (int8_t)(sin(t) * 8);       // 좌우 ±8px
int8_t oy = (int8_t)(cos(t * 1.3) * 4); // 상하 ±4px
```

**깜빡임 타이밍**: 2500ms마다 한 번, 180ms 동안 감음.

**동기화**: A가 ox, oy, blink, phase를 `MSG_EYE`로 50ms마다 B에 전송. B는 수신값을 그대로 렌더링.

---

### Demo 1 — PING-PONG

**개념**: 공이 두 화면 사이를 실시간으로 오가는 가상 핑퐁 게임.

**동작 흐름**:
```
ESP_A 화면                         ESP_B 화면
──────────                         ──────────
공이 오른쪽으로 이동
x > 128 → MSG_BALL 전송 ─────────▶ x=-5에서 공 등장
                                   공이 오른쪽으로 이동
                                   x > 128 → MSG_BALL 전송
◀──────────────────────────────── x=-5에서 공 등장
```

**물리 시뮬레이션**:
- `ballVX = 2.5 px/frame`, `ballVY = 1.5 px/frame` (20ms 간격)
- 상하 벽(`y=16`, `y=58`) 반사: `ballVY = -ballVY`
- 왼쪽 벽(`x=4`) 반사: `ballVX = abs(ballVX)`

**값 인코딩** (`MSG_BALL`):
```cpp
int encoded = (int)(ballY * 100) + (int)((ballVY * 10) + 50);
// 수신 측 복원
ballY  = encoded / 100.0;
ballVY = (encoded % 100 - 50) / 10.0;
```

**표시 정보**: 화면 우하단에 총 왕복 횟수(`#N`), 송수신 시 2초간 `SEND >>>` / `>>> RECV` 텍스트 표시.

---

### Demo 2 — EMOJI

**개념**: Heart / Star / Smile 아이콘을 두 보드가 번갈아 전송하고, 수신 시 팝업 애니메이션으로 표시.

**동작 흐름**:
- A가 먼저 전송 (`myTurnToSend = true`)
- 전송 후 `myTurnToSend = false`
- 수신 측에서 팝업 후 `myTurnToSend = true`로 전환

**아이콘 데이터**: 16×16 비트맵을 `PROGMEM`(플래시)에 저장해 RAM 절약.

```cpp
const uint8_t heartBmp[] PROGMEM = { /* 32 bytes */ };
```

**애니메이션**:
- 송신: 아이콘이 0.5초에 걸쳐 수축하며 사라짐 (`sz = 16 * (1.0 - t)`)
- 수신: 1.5초 팝업 — 0.5초 동안 확장 (`iconScale = t * 2.0`), 이후 고정 표시, 동심원 파동 효과

---

### Demo 3 — COUNTER

**개념**: 두 보드가 번갈아 +1씩 카운터를 올려 동기화된 숫자를 공유.

**동작 흐름**:
```
A가 syncCounter++ → MSG_COUNTER(값) 전송 → myTurnToCount = false
B 수신 → syncCounter = 값, myTurnToCount = true
B가 syncCounter++ → MSG_COUNTER(값) 전송 → myTurnToCount = false
A 수신 → myTurnToCount = true
... 반복 (800ms 간격)
```

**표시**: 텍스트 크기 3 (24px 높이)으로 숫자 중앙 표시, 하단에 `syncCounter % 50` 기반 프로그레스 바.

---

### Demo 4 — SEESAW

**개념**: A가 시소의 기울기를 계산하여 B에 실시간 전송. 양쪽 화면이 동일한 시소를 렌더링하되, 자기 보드의 블록은 채워진 사각형(■), 상대 보드는 빈 사각형(□)으로 구분.

**마스터/슬레이브 역할**:
- **A**: `sin(millis() / 1400.0) * 12`로 오프셋 계산 후 50ms마다 `MSG_SEESAW` 전송
- **B**: 수신한 `seesawOffset` 값을 그대로 사용

**렌더링**:
```
오프셋 +12 (A쪽 무거움):          오프셋 -12 (B쪽 무거움):
   ■                                              ■
    \                              /
     \       △               /
      ─── /\  ───          ─── /\  ───
                □                        □
```

**시소 좌표 계산**:
```cpp
// 기준점: 플랭크 중심 (x=64, y=46)
plankLeft  = (6,   46 + off)
plankRight = (122, 46 - off)
blockA_top = 38 + off   // A 블록 (왼쪽)
blockB_top = 38 - off   // B 블록 (오른쪽)
```

**상태 표시**: `off > 4` → "A is heavy!", `off < -4` → "B is heavy!", 그 외 → "Balanced~"

---

### Demo 5 — MORSE

**개념**: 두 보드가 모스부호를 번갈아 전송. 점(●)과 선(▬)을 화면에 그려나가며 실시간으로 표시.

| 보드 | 송신 내용 | 모스 시퀀스 |
|------|-----------|------------|
| ESP_A | SOS | `...---...` (9심볼) |
| ESP_B | OK  | `---.-.` (6심볼) |

**심볼 인코딩** (`MSG_MORSE`):
```
value = 0  → dot  (점, 반지름 4 원)
value = 1  → dash (선, 16×6 사각형)
value = 99 → 전송 완료, 상대방 턴 시작
```

**턴 전환 로직**:
```
A: morseSending=true → 시퀀스 전송 완료 → MSG_MORSE(99) → morseSending=false
B: 99 수신 → morseSending=true → 시퀀스 전송 완료 → MSG_MORSE(99)
A: 99 수신 → morseSending=true (반복)
```

**타이밍**: 심볼 1개당 300ms (`MORSE_TICK = 300`)

**렌더링**:
- 점: `display.fillCircle(x+4, y, 4, WHITE)` → 13px 간격
- 선: `display.fillRect(x, y-3, 16, 6, WHITE)` → 21px 간격
- 송신 중 커서: 400ms 주기 깜빡이는 3px 세로 막대

---

### Demo 6 — SNAKE

**개념**: 뱀이 A 화면 → B 화면 → A 화면 순으로 순환 이동. 한 화면에서 오른쪽 끝에 도달하면 상대 화면 왼쪽에서 등장.

**뱀 구조**:
```cpp
struct SnakeSeg { int16_t x, y; };
SnakeSeg snakeSegs[7];  // [0]=머리, [6]=꼬리
// 머리: 반지름 4, 몸통: 3,3,2,2,1,1 순으로 축소
int r = max(1, 4 - i / 2);
```

**이동 방식** (60ms 타이머):
```cpp
// 트레일 시프트
for (int i = SNAKE_LEN-1; i > 0; i--) snakeSegs[i] = snakeSegs[i-1];
snakeX += 3;           // 수평 이동 (3px/60ms = 50px/sec)
snakeY += snakeVY;     // 수직 이동 (-1, 0, +1)
// 상하 경계 반사 (y=15, y=60)
```

**화면 전환** (`MSG_SNAKE`):
```cpp
// 전송: x > 136 (화면 밖) 시 발송
sendMsg(MSG_SNAKE, (int32_t)snakeY * 10 + (snakeVY + 5));

// 수신: 상대 화면 왼쪽에서 등장
snakeX = -4;
snakeY = msg.value / 10;
snakeVY = msg.value % 10 - 5;
// 트레일 초기화: 모든 세그먼트를 (x-i*6, y)에 배치
```

**대기 화면**: 뱀이 없을 때 `<<< waiting...` 텍스트와 점 애니메이션 표시.

---

## 빌드 및 업로드

### 요구사항

- [PlatformIO](https://platformio.org/) (CLI 또는 VS Code 확장)
- Python 3.x

### 명령어

```bash
# 빌드만
pio run -e esp_a
pio run -e esp_b

# 빌드 + 업로드
pio run -e esp_a -t upload
pio run -e esp_b -t upload

# 시리얼 모니터 (115200 baud)
pio device monitor

# 업로드 직후 바로 모니터
pio run -e esp_a -t upload && pio device monitor

# 캐시 클리어 후 풀 빌드
pio run -t clean && pio run -e esp_a
```

### 메모리 사용량 (현재)

| 보드 | RAM | Flash |
|------|-----|-------|
| ESP_A | 37.7% (30,848 / 81,920 bytes) | 29.0% (303,071 / 1,044,464 bytes) |
| ESP_B | 35.9% (29,392 / 81,920 bytes) | 28.0% (292,327 / 1,044,464 bytes) |

### 의존 라이브러리

PlatformIO가 `platformio.ini`의 `lib_deps`를 보고 자동 설치한다.

```ini
lib_deps =
    adafruit/Adafruit SSD1306@^2.5.7
    adafruit/Adafruit GFX Library@^1.11.5
```

> ESP8266WiFi, espnow, Wire는 ESP8266 Arduino 코어에 내장.

---

## 개발 시 참고사항

### 새 데모 추가 방법

1. `MsgType` enum에 새 값 추가 (양쪽 파일 동일하게)
2. 전역 상태 변수 선언
3. `onDataRecv()` switch에 수신 처리 추가
4. loop()의 데모 전환 reset 블록에 초기화 추가 (`esp_a.cpp`: `% N` 변경, `esp_b.cpp`: 폴백 타임아웃 블록도 동일)
5. `MSG_DEMO_SYNC` 핸들러에 reset 추가
6. `drawDemoN()` 함수 구현
7. `demoNames[]` 배열에 이름 추가
8. switch(currentDemo)에 케이스 추가

### 디버깅 팁

- 시리얼 모니터로 페어링 성공 여부 확인: `[ESP_A] Starting...` 출력 후 데모가 시작되면 페어링 완료
- `msg.demoMode`와 수신 측 `currentDemo`가 다르면 동기화 문제 — `MSG_DEMO_SYNC` 전송 타이밍 확인
- 뱀이 화면을 넘어가지 않으면 `snakeVY` 인코딩 오프셋(`+5`) 확인
- EYES 데모에서 눈이 안 보이면 `MSG_DEMO_SYNC` 핸들러의 `% 7` 확인 (구버전 `% 6` 버그 주의)

### ESP8266 vs ESP32 이식 시 주의점

| 항목 | ESP8266 | ESP32 |
|------|---------|-------|
| 헤더 | `#include <espnow.h>` | `#include <esp_now.h>` |
| 초기화 | `esp_now_init() != 0` (실패 시 0이 아님) | `esp_now_init() != ESP_OK` |
| 역할 설정 | `esp_now_set_self_role()` | 없음 (역할 개념 폐지) |
| 콜백 파라미터 | `uint8_t *mac, uint8_t *data, uint8_t len` | `const uint8_t *mac, const uint8_t *data, int len` |
| 피어 추가 | `esp_now_add_peer(mac, role, ch, key, keyLen)` | `esp_now_add_peer(&peerInfo)` 구조체 방식 |
