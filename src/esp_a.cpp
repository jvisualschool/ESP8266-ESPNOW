// ESP_A - ESP-NOW 6-in-1 Demo (Board A)
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- OLED ---
#define SCREEN_W 128
#define SCREEN_H 64
#define SDA_PIN 14  // D5
#define SCL_PIN 12  // D6
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// --- ESP-NOW ---
#define BROADCAST_ADDR {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}
uint8_t peerMAC[6] = {0};
bool paired = false;

// --- Message Protocol ---
enum MsgType : uint8_t {
  MSG_PAIR_REQ = 0,
  MSG_PAIR_ACK,
  MSG_BALL,        // Demo1
  MSG_ICON,        // Demo2
  MSG_COUNTER,     // Demo3
  MSG_DEMO_SYNC,
  MSG_SEESAW,      // Demo4
  MSG_MORSE,       // Demo5
  MSG_SNAKE,       // Demo6
  MSG_EYE,         // Demo7
};

struct Message {
  uint8_t demoMode;
  uint8_t msgType;
  int32_t value;
} __attribute__((packed));

// --- Demo State ---
uint8_t currentDemo = 0;
unsigned long demoStartTime = 0;
const unsigned long DEMO_DURATION = 10000;
bool isMaster = true;

// Demo 1: Ping-Pong Ball
float ballX = 20, ballY = 32;
float ballVX = 2.5, ballVY = 1.5;
bool ballOnScreen = true;
int pingPongCount = 0;
bool showSend = false, showRecv = false;
unsigned long sendRecvTimer = 0;

// Demo 2: Emoji Transfer
const uint8_t ICON_HEART = 0, ICON_STAR = 1, ICON_SMILE = 2;
uint8_t currentIcon = ICON_HEART;
bool sendingIcon = false;
bool receivedIcon = false;
float iconScale = 0;
unsigned long iconTimer = 0;
unsigned long iconSendInterval = 2000;
bool myTurnToSend = true;

// Demo 3: Synced Counter
int32_t syncCounter = 0;
bool myTurnToCount = true;
unsigned long counterTimer = 0;
unsigned long counterInterval = 800;

// Demo 4: Seesaw (A is master, offset computed from millis())
// No persistent state needed

// Demo 5: Morse Code (A sends SOS)
const char MORSE_SEQ[] = "...---...";  // SOS
int morseIdx = 0;
bool morseSending = true;  // A sends first
unsigned long morseTimer = 0;
const int MORSE_TICK = 300;
char morseBuf[12];
int morseBufLen = 0;

// Demo 7: Eyes (A is master)
unsigned long eyeSendTimer = 0;
unsigned long eyeBlinkTimer = 0;
bool eyeBlinking = false;

// Demo 6: Snake
const int SNAKE_LEN = 7;
struct SnakeSeg { int16_t x, y; };
SnakeSeg snakeSegs[SNAKE_LEN];
bool snakeOnMe = true;  // A has snake initially
int16_t snakeX = 10, snakeY = 37;
int8_t snakeVY = 1;

// --- Forward Declarations ---
void onDataSent(uint8_t *mac, uint8_t status);
void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len);
void sendMsg(uint8_t msgType, int32_t value);
void drawDemo1();
void drawDemo2();
void drawDemo3();
void drawDemo4();
void drawDemo5();
void drawDemo6();
void drawDemo7();
void drawEye(int cx, int cy, int ox, int oy, bool blink, int ER=16, int PR=7);
void drawSplash(const char* text);
void drawPairingScreen();

// --- Icon Bitmaps (16x16) ---
const uint8_t heartBmp[] PROGMEM = {
  0x00,0x00, 0x36,0x6C, 0x7F,0xFE, 0x7F,0xFE,
  0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF, 0xFF,0xFF,
  0x7F,0xFE, 0x3F,0xFC, 0x1F,0xF8, 0x0F,0xF0,
  0x07,0xE0, 0x03,0xC0, 0x01,0x80, 0x00,0x00
};

const uint8_t starBmp[] PROGMEM = {
  0x00,0x00, 0x01,0x80, 0x01,0x80, 0x03,0xC0,
  0x03,0xC0, 0xFF,0xFF, 0x7F,0xFE, 0x1F,0xF8,
  0x0F,0xF0, 0x0F,0xF0, 0x1F,0xF8, 0x39,0x9C,
  0x71,0x8E, 0x61,0x86, 0x01,0x80, 0x00,0x00
};

const uint8_t smileBmp[] PROGMEM = {
  0x07,0xE0, 0x18,0x18, 0x20,0x04, 0x40,0x02,
  0x4C,0x32, 0x4C,0x32, 0x40,0x02, 0x40,0x02,
  0x40,0x02, 0x42,0x42, 0x41,0x82, 0x20,0x04,
  0x18,0x18, 0x07,0xE0, 0x00,0x00, 0x00,0x00
};

// --- Callbacks ---
void onDataSent(uint8_t *mac, uint8_t status) {}

void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len < sizeof(Message)) return;
  Message msg;
  memcpy(&msg, data, sizeof(Message));

  if (msg.msgType == MSG_PAIR_REQ && !paired) {
    memcpy(peerMAC, mac, 6);
    esp_now_add_peer(peerMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    paired = true;
    Message ack = {0, MSG_PAIR_ACK, 0};
    esp_now_send(peerMAC, (uint8_t*)&ack, sizeof(ack));
    demoStartTime = millis();
    return;
  }
  if (msg.msgType == MSG_PAIR_ACK && !paired) {
    memcpy(peerMAC, mac, 6);
    esp_now_add_peer(peerMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    paired = true;
    demoStartTime = millis();
    return;
  }

  if (!paired) return;

  switch (msg.msgType) {
    case MSG_DEMO_SYNC:
      currentDemo = msg.value % 7;
      demoStartTime = millis();
      if (currentDemo == 0) {
        eyeBlinking = false; eyeBlinkTimer = millis(); eyeSendTimer = millis();
      } else if (currentDemo == 1) {
        ballX = -5; ballY = 32; ballVX = 2.5; ballVY = 1.5;
        ballOnScreen = true; showRecv = true; showSend = false;
        sendRecvTimer = millis();
      } else if (currentDemo == 2) {
        myTurnToSend = false; receivedIcon = false; sendingIcon = false;
        iconScale = 0; iconTimer = millis();
      } else if (currentDemo == 3) {
        syncCounter = 0; myTurnToCount = false; counterTimer = millis();
      } else if (currentDemo == 5) {
        morseIdx = 0; morseSending = true; morseBufLen = 0; morseTimer = millis();
      } else if (currentDemo == 6) {
        snakeOnMe = true; snakeX = 10; snakeY = 37; snakeVY = 1;
        for (int i = 0; i < SNAKE_LEN; i++) snakeSegs[i] = {(int16_t)(snakeX - i*6), snakeY};
      }
      break;

    case MSG_BALL:
      ballX = -5;
      ballY = msg.value / 100.0;
      ballVX = abs(ballVX);
      ballVY = (msg.value % 100 - 50) / 10.0;
      ballOnScreen = true;
      pingPongCount++;
      showRecv = true; showSend = false;
      sendRecvTimer = millis();
      break;

    case MSG_ICON:
      currentIcon = msg.value;
      receivedIcon = true;
      iconScale = 0.3;
      iconTimer = millis();
      myTurnToSend = true;
      break;

    case MSG_COUNTER:
      syncCounter = msg.value;
      myTurnToCount = true;
      counterTimer = millis();
      break;

    case MSG_MORSE:
      if (msg.value == 99) {
        morseSending = true;
        morseIdx = 0;
        morseBufLen = 0;
        morseTimer = millis();
      } else if (!morseSending) {
        char sym = (msg.value == 0) ? '.' : '-';
        if (morseBufLen < 11) morseBuf[morseBufLen++] = sym;
      }
      break;

    case MSG_EYE:
      // A is master, no need to receive eye data
      break;

    case MSG_SNAKE:
      snakeOnMe = true;
      snakeX = -4;
      snakeY = (int16_t)(msg.value / 10);
      snakeVY = (int8_t)(msg.value % 10 - 5);
      for (int i = 0; i < SNAKE_LEN; i++) snakeSegs[i] = {(int16_t)(snakeX - i*6), snakeY};
      break;
  }
}

void sendMsg(uint8_t msgType, int32_t value) {
  if (!paired) return;
  Message msg = {currentDemo, msgType, value};
  esp_now_send(peerMAC, (uint8_t*)&msg, sizeof(msg));
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  Serial.println("\n[ESP_A] Starting...");

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 fail");
    while (1) delay(100);
  }
  // Boot splash: A starts "ESP", B starts "NOW"
  const char* splashSeq[] = {"ESP", "NOW", "ESP"};
  for (int i = 0; i < 3; i++) {
    drawSplash(splashSeq[i]);
    delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW init fail");
    while (1) delay(100);
  }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  uint8_t broadcastMAC[] = BROADCAST_ADDR;
  esp_now_add_peer(broadcastMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

  // Init snake trail
  for (int i = 0; i < SNAKE_LEN; i++) snakeSegs[i] = {(int16_t)(snakeX - i*6), snakeY};

  eyeBlinkTimer = millis();
  eyeSendTimer = millis();
  demoStartTime = millis();
}

// --- Loop ---
void loop() {
  if (!paired) {
    static unsigned long lastPairSend = 0;
    if (millis() - lastPairSend > 500) {
      uint8_t broadcastMAC[] = BROADCAST_ADDR;
      Message pairReq = {0, MSG_PAIR_REQ, 0};
      esp_now_send(broadcastMAC, (uint8_t*)&pairReq, sizeof(pairReq));
      lastPairSend = millis();
    }
    drawPairingScreen();
    return;
  }

  // Demo cycling (A is master)
  if (millis() - demoStartTime >= DEMO_DURATION) {
    uint8_t nextDemo = (currentDemo + 1) % 7;
    currentDemo = nextDemo;
    demoStartTime = millis();
    sendMsg(MSG_DEMO_SYNC, currentDemo);

    if (currentDemo == 0) {
      eyeBlinking = false; eyeBlinkTimer = millis(); eyeSendTimer = millis();
    } else if (currentDemo == 1) {
      ballX = 20; ballY = 32; ballVX = 2.5; ballVY = 1.5;
      ballOnScreen = true; pingPongCount = 0;
      showSend = false; showRecv = false;
    } else if (currentDemo == 2) {
      myTurnToSend = true; receivedIcon = false; sendingIcon = false;
      iconScale = 0; currentIcon = ICON_HEART; iconTimer = millis();
    } else if (currentDemo == 3) {
      syncCounter = 0; myTurnToCount = true; counterTimer = millis();
    } else if (currentDemo == 5) {
      morseIdx = 0; morseSending = true; morseBufLen = 0; morseTimer = millis();
    } else if (currentDemo == 6) {
      snakeOnMe = true; snakeX = 10; snakeY = 37; snakeVY = 1;
      for (int i = 0; i < SNAKE_LEN; i++) snakeSegs[i] = {(int16_t)(snakeX - i*6), snakeY};
    }
  }

  display.clearDisplay();

  // Header bar
  display.fillRect(0, 0, SCREEN_W, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 1);
  display.print("ESP_A");
  display.setCursor(44, 1);
  const char* demoNames[] = {"EYES", "PING-PONG", "EMOJI", "COUNTER", "SEESAW", "MORSE", "SNAKE"};
  display.print(demoNames[currentDemo]);

  // Time remaining bar
  unsigned long elapsed = millis() - demoStartTime;
  int barW = map(constrain(elapsed, 0, DEMO_DURATION), 0, DEMO_DURATION, SCREEN_W, 0);
  display.drawRect(0, 10, SCREEN_W, 3, SSD1306_WHITE);
  display.fillRect(0, 10, barW, 3, SSD1306_WHITE);

  display.setTextColor(SSD1306_WHITE);

  switch (currentDemo) {
    case 0: drawDemo7(); break;
    case 1: drawDemo1(); break;
    case 2: drawDemo2(); break;
    case 3: drawDemo3(); break;
    case 4: drawDemo4(); break;
    case 5: drawDemo5(); break;
    case 6: drawDemo6(); break;
  }

  display.display();
  delay(20);
}

// --- Demo 1: Ping-Pong Ball ---
void drawDemo1() {
  if (ballOnScreen) {
    ballX += ballVX;
    ballY += ballVY;

    if (ballY < 16) { ballY = 16; ballVY = abs(ballVY); }
    if (ballY > 58) { ballY = 58; ballVY = -abs(ballVY); }
    if (ballX < 4) { ballX = 4; ballVX = abs(ballVX); }

    if (ballX > SCREEN_W + 3) {
      ballOnScreen = false;
      int encoded = (int)(ballY * 100) + (int)((ballVY * 10) + 50);
      sendMsg(MSG_BALL, encoded);
      pingPongCount++;
      showSend = true; showRecv = false;
      sendRecvTimer = millis();
    }

    if (ballOnScreen) {
      display.fillCircle((int)ballX, (int)ballY, 4, SSD1306_WHITE);
    }
  }

  display.setTextSize(1);
  if (showSend && millis() - sendRecvTimer < 2000) {
    display.setCursor(30, 28); display.print("SEND >>>");
  }
  if (showRecv && millis() - sendRecvTimer < 2000) {
    display.setCursor(30, 28); display.print(">>> RECV");
  }

  display.setCursor(85, 55);
  display.print("#"); display.print(pingPongCount);
}

// --- Demo 2: Emoji/Icon Transfer ---
void drawDemo2() {
  if (myTurnToSend && !sendingIcon && millis() - iconTimer > iconSendInterval) {
    sendingIcon = true;
    currentIcon = (currentIcon + 1) % 3;
    sendMsg(MSG_ICON, currentIcon);
    myTurnToSend = false;
    iconTimer = millis();
    display.setTextSize(1);
    display.setCursor(10, 50);
    display.print("Sending...");
  }

  if (sendingIcon) {
    float t = (millis() - iconTimer) / 500.0;
    if (t > 1.0) { sendingIcon = false; }
    else {
      int sz = 16 * (1.0 - t);
      if (sz > 0) {
        const uint8_t *bmp = (currentIcon == ICON_HEART) ? heartBmp :
                              (currentIcon == ICON_STAR) ? starBmp : smileBmp;
        display.drawBitmap(64 - sz/2, 36 - sz/2, bmp, 16, 16, SSD1306_WHITE);
      }
    }
  }

  if (receivedIcon) {
    float t = (millis() - iconTimer) / 1000.0;
    if (t > 1.5) {
      receivedIcon = false;
    } else {
      iconScale = (t < 0.5) ? t * 2.0 : 1.0;
      const uint8_t *bmp = (currentIcon == ICON_HEART) ? heartBmp :
                            (currentIcon == ICON_STAR) ? starBmp : smileBmp;
      display.drawBitmap(64 - 8, 30 - 8, bmp, 16, 16, SSD1306_WHITE);
      if (iconScale < 1.0) {
        int r = (int)(20 * iconScale);
        display.drawCircle(64, 30, r, SSD1306_WHITE);
        display.drawCircle(64, 30, r + 2, SSD1306_WHITE);
      }
      display.setTextSize(1);
      display.setCursor(10, 50);
      display.print("Received!");
    }
  }

  display.setTextSize(1);
  display.setCursor(2, 16);
  const char* iconNames[] = {"Heart", "Star", "Smile"};
  display.print(iconNames[currentIcon]);
}

// --- Demo 3: Synced Counter ---
void drawDemo3() {
  if (myTurnToCount && millis() - counterTimer > counterInterval) {
    syncCounter++;
    sendMsg(MSG_COUNTER, syncCounter);
    myTurnToCount = false;
    counterTimer = millis();
  }

  display.setTextSize(3);
  String numStr = String(syncCounter);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(numStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_W - w) / 2, 20);
  display.print(syncCounter);

  int progress = (syncCounter % 50);
  int barWidth = map(progress, 0, 49, 0, 120);
  display.drawRect(4, 52, 120, 10, SSD1306_WHITE);
  display.fillRect(4, 52, barWidth, 10, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(2, 16);
  display.print(myTurnToCount ? "My turn" : "Waiting...");
}

// --- Demo 4: Seesaw ---
void drawDemo4() {
  int8_t off = (int8_t)(sin(millis() / 1400.0) * 12);

  static unsigned long lastSeesawSend = 0;
  if (millis() - lastSeesawSend > 50) {
    sendMsg(MSG_SEESAW, off);
    lastSeesawSend = millis();
  }

  // Fulcrum
  display.fillTriangle(62, 62, 64, 56, 66, 62, SSD1306_WHITE);
  display.drawLine(58, 63, 70, 63, SSD1306_WHITE);

  // Plank (pivot at x=64, y=46)
  display.drawLine(6, 46 + off, 122, 46 - off, SSD1306_WHITE);

  // A block (left end, filled = this board)
  int ay = 38 + off;
  display.fillRect(4, ay, 10, 8, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(6, ay + 1);
  display.print("A");
  display.setTextColor(SSD1306_WHITE);

  // B block (right end, outline)
  int by = 38 - off;
  display.drawRect(114, by, 10, 8, SSD1306_WHITE);
  display.setCursor(116, by + 1);
  display.print("B");

  display.setTextSize(1);
  display.setCursor(30, 16);
  if (off > 4)       display.print("A is heavy!");
  else if (off < -4) display.print("B is heavy!");
  else               display.print("Balanced~");
}

// --- Demo 5: Morse Code (A sends SOS) ---
void drawDemo5() {
  if (morseSending) {
    if (millis() - morseTimer > MORSE_TICK) {
      if (morseIdx < (int)strlen(MORSE_SEQ)) {
        char sym = MORSE_SEQ[morseIdx++];
        sendMsg(MSG_MORSE, sym == '.' ? 0 : 1);
        if (morseBufLen < 11) morseBuf[morseBufLen++] = sym;
        morseTimer = millis();
      } else {
        sendMsg(MSG_MORSE, 99);  // done
        morseSending = false;
        morseIdx = 0;
        morseBufLen = 0;
        morseTimer = millis();
      }
    }
    display.setTextSize(1);
    display.setCursor(2, 16);
    display.print("TX >> SOS");
  } else {
    display.setTextSize(1);
    display.setCursor(2, 16);
    display.print("RX << OK ");
  }

  // Draw dots and dashes
  int x = 4, y = 40;
  for (int i = 0; i < morseBufLen && x < 120; i++) {
    if (morseBuf[i] == '.') {
      display.fillCircle(x + 4, y, 4, SSD1306_WHITE);
      x += 13;
    } else {
      display.fillRect(x, y - 3, 16, 6, SSD1306_WHITE);
      x += 21;
    }
  }

  // Blinking cursor when sending
  if (morseSending && (millis() / 400) % 2 == 0 && x < 120) {
    display.fillRect(x, y - 3, 3, 6, SSD1306_WHITE);
  }

  // Dot/dash legend
  display.setTextSize(1);
  display.setCursor(2, 55);
  display.print(". = dot  - = dash");
}

// --- Demo 6: Snake ---
void drawDemo6() {
  if (snakeOnMe) {
    // Move snake on a timer
    static unsigned long snakeMoveTimer = 0;
    if (millis() - snakeMoveTimer > 60) {
      for (int i = SNAKE_LEN - 1; i > 0; i--) snakeSegs[i] = snakeSegs[i-1];
      snakeX += 3;
      snakeY += snakeVY;
      if (snakeY < 15) { snakeY = 15; snakeVY = 1; }
      if (snakeY > 60) { snakeY = 60; snakeVY = -1; }
      snakeSegs[0] = {snakeX, snakeY};
      snakeMoveTimer = millis();
    }

    // Check exit right edge
    if (snakeX > SCREEN_W + 8) {
      snakeOnMe = false;
      sendMsg(MSG_SNAKE, (int32_t)snakeY * 10 + (snakeVY + 5));
    }

    // Draw trail (tail first, head on top)
    for (int i = SNAKE_LEN - 1; i >= 0; i--) {
      if (snakeSegs[i].x < 0 || snakeSegs[i].x > SCREEN_W) continue;
      int r = max(1, 4 - i / 2);
      display.fillCircle(snakeSegs[i].x, snakeSegs[i].y, r, SSD1306_WHITE);
    }

    display.setTextSize(1);
    display.setCursor(2, 16);
    display.print("Here! >>>");
  } else {
    // Waiting for snake from B
    display.setTextSize(1);
    display.setCursor(16, 30);
    display.print("<<< waiting...");

    // Animated dots
    int dots = (millis() / 300) % 5;
    for (int i = 0; i < dots; i++) {
      display.fillCircle(20 + i * 10, 46, 2, SSD1306_WHITE);
    }

    display.setCursor(2, 16);
    display.print("B's turn");
  }
}

// --- Demo 7: Eyes ---
void drawEye(int cx, int cy, int ox, int oy, bool blink, int ER, int PR) {
  if (!blink) {
    display.drawCircle(cx, cy, ER,     SSD1306_WHITE);
    display.drawCircle(cx, cy, ER - 1, SSD1306_WHITE);
    display.fillCircle(cx + ox, cy + oy, PR, SSD1306_WHITE);
  } else {
    int ey = ER / 3;
    display.drawLine(cx - ER + 3, cy + 1, cx,          cy - ey, SSD1306_WHITE);
    display.drawLine(cx,          cy - ey, cx + ER - 3, cy + 1, SSD1306_WHITE);
    display.drawLine(cx - ER + 3, cy + 1, cx,          cy + ey, SSD1306_WHITE);
    display.drawLine(cx,          cy + ey, cx + ER - 3, cy + 1, SSD1306_WHITE);
  }
}

void drawDemo7() {
  float t = millis() / 1500.0;
  int8_t ox = (int8_t)(sin(t) * 8);
  int8_t oy = (int8_t)(cos(t * 1.3) * 4);

  if (!eyeBlinking && millis() - eyeBlinkTimer > 2500) {
    eyeBlinking = true;
    eyeBlinkTimer = millis();
  }
  if (eyeBlinking && millis() - eyeBlinkTimer > 180) {
    eyeBlinking = false;
    eyeBlinkTimer = millis();
  }

  int8_t phase = (millis() - demoStartTime < 3000) ? 0 : 1;

  if (millis() - eyeSendTimer > 50) {
    int32_t enc = (int32_t)phase * 100000
                + (int32_t)(ox + 20) * 1000
                + (int32_t)(oy + 20) * 10
                + (eyeBlinking ? 1 : 0);
    sendMsg(MSG_EYE, enc);
    eyeSendTimer = millis();
  }

  if (phase == 0) {
    drawEye(32, 38, ox, oy, eyeBlinking, 16, 7);
    drawEye(96, 38, ox, oy, eyeBlinking, 16, 7);
  } else {
    drawEye(64, 38, ox, oy, eyeBlinking, 24, 11);
  }
}

// --- Boot Splash ---
void drawSplash(const char* text) {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_W, SCREEN_H, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(6);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_W - w) / 2 - x1, (SCREEN_H - h) / 2 - y1);
  display.print(text);
  display.display();
}

// --- Pairing Screen ---
void drawPairingScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 10);
  display.print("ESP_A");
  display.setCursor(10, 30);
  display.print("Searching for");
  display.setCursor(10, 42);
  display.print("ESP_B...");

  int dots = (millis() / 500) % 4;
  for (int i = 0; i < dots; i++) {
    display.fillCircle(80 + i * 10, 45, 2, SSD1306_WHITE);
  }

  display.display();
}
