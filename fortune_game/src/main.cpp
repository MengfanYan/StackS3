#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>

struct Fortune {
  const char* title;
  int rarity;   // 0 = low, 1 = normal, 2 = good
};

Fortune fortunes[] = {
  {"Fortune", 2},
  {"Success", 2},
  {"Hope", 2},
  {"Victory", 2},
  {"Balance", 1},
  {"Patience", 1},
  {"Change", 1},
  {"Growth", 1},
  {"Wisdom", 1},
  {"Caution", 0},
  {"Silence", 0},
  {"Stillness", 0}
};

const int FORTUNE_COUNT = sizeof(fortunes) / sizeof(fortunes[0]);

enum AppState {
  STATE_IDLE,
  STATE_ANIMATING,
  STATE_RESULT
};

AppState appState = STATE_IDLE;

float lastAx = 0.0f;
float lastAy = 0.0f;
float lastAz = 0.0f;
bool hasLastAccel = false;

const float SHAKE_THRESHOLD = 1.65f;
const uint32_t SHAKE_COOLDOWN = 1500;
uint32_t lastShakeTime = 0;

uint32_t animStartTime = 0;
const uint32_t ANIM_DURATION = 1600;
int selectedIndex = -1;

uint32_t lastShakeSfxTime = 0;

uint32_t resultLockUntil = 0;

// Forward declarations
void resetShakeBaseline();

// Draw centered text
void drawCenterText(const String& text, int y, int size = 1) {
  M5.Display.setTextSize(size);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(text, M5.Display.width() / 2, y);
}

// Weighted random draw
int drawFortuneWeighted() {
  int roll = random(100);
  int targetRarity = 1;

  if (roll < 10) {
    targetRarity = 2;
  } else if (roll < 80) {
    targetRarity = 1;
  } else {
    targetRarity = 0;
  }

  int candidates[FORTUNE_COUNT];
  int count = 0;

  for (int i = 0; i < FORTUNE_COUNT; i++) {
    if (fortunes[i].rarity == targetRarity) {
      candidates[count++] = i;
    }
  }

  return candidates[random(count)];
}

// Play short wooden shaking sound
void playShakeSfx() {
  uint32_t now = millis();
  if (now - lastShakeSfxTime > 70) {
    M5.Speaker.tone(random(1200, 2000), 30);
    lastShakeSfxTime = now;
  }
}

// Play result sound based on rarity
void playResultSfx(int rarity) {
  if (rarity == 2) {
    M5.Speaker.tone(2000, 80);
    delay(80);
    M5.Speaker.tone(2600, 80);
    delay(80);
    M5.Speaker.tone(3400, 180);
  } else if (rarity == 1) {
    M5.Speaker.tone(2000, 120);
  } else {
    M5.Speaker.tone(1000, 180);
    delay(100);
    M5.Speaker.tone(800, 220);
  }
}

// Draw a tube with sticks, moving vertically
void drawTubeVertical(int offsetY, int step) {
  int cx = M5.Display.width() / 2;
  int cy = M5.Display.height() / 2 + 6 + offsetY;

  M5.Display.drawRoundRect(cx - 18, cy - 30, 36, 60, 8, WHITE);

  M5.Display.drawLine(cx - 12, cy - 16, cx + 12, cy - 16, WHITE);
  M5.Display.drawLine(cx - 10, cy - 6,  cx + 10, cy - 6,  WHITE);
  M5.Display.drawLine(cx - 8,  cy + 4,  cx + 8,  cy + 4,  WHITE);

  int phase = step % 3;

  if (phase == 0) {
    M5.Display.drawLine(cx - 10, cy - 44, cx - 4, cy - 30, WHITE);
    M5.Display.drawLine(cx,      cy - 48, cx,      cy - 30, WHITE);
    M5.Display.drawLine(cx + 10, cy - 44, cx + 4, cy - 30, WHITE);
  } else if (phase == 1) {
    M5.Display.drawLine(cx - 11, cy - 46, cx - 5, cy - 30, WHITE);
    M5.Display.drawLine(cx,      cy - 43, cx,      cy - 30, WHITE);
    M5.Display.drawLine(cx + 11, cy - 46, cx + 5, cy - 30, WHITE);
  } else {
    M5.Display.drawLine(cx - 9,  cy - 42, cx - 4, cy - 30, WHITE);
    M5.Display.drawLine(cx,      cy - 50, cx,      cy - 30, WHITE);
    M5.Display.drawLine(cx + 9,  cy - 42, cx + 4, cy - 30, WHITE);
  }

  M5.Display.drawLine(cx - 28, cy - 34, cx - 22, cy - 40, WHITE);
  M5.Display.drawLine(cx + 28, cy - 34, cx + 22, cy - 40, WHITE);
  M5.Display.drawLine(cx - 28, cy + 34, cx - 22, cy + 40, WHITE);
  M5.Display.drawLine(cx + 28, cy + 34, cx + 22, cy + 40, WHITE);
}

// Show centered idle screen
void showIdle() {
  appState = STATE_IDLE;
  M5.Display.clear(BLACK);
  M5.Display.setTextColor(WHITE, BLACK);

  int centerY = M5.Display.height() / 2;

  drawCenterText("Fortune Draw", centerY - 20, 2);
  drawCenterText("Shake or Press A", centerY + 10, 1);
  drawCenterText("Press B for Home", centerY + 28, 1);

  resultLockUntil = millis() + 300;
  resetShakeBaseline();
}

// Reset accelerometer baseline to avoid immediate re-trigger
void resetShakeBaseline() {
  if (M5.Imu.update()) {
    auto d = M5.Imu.getImuData();
    lastAx = d.accel.x;
    lastAy = d.accel.y;
    lastAz = d.accel.z;
    hasLastAccel = true;
  }
}

// Show result screen
void showResult(int idx) {
  appState = STATE_RESULT;
  M5.Display.clear(BLACK);

  int rarity = fortunes[idx].rarity;
  if (rarity == 2) {
    M5.Display.setTextColor(GREEN, BLACK);
  } else if (rarity == 1) {
    M5.Display.setTextColor(YELLOW, BLACK);
  } else {
    M5.Display.setTextColor(RED, BLACK);
  }

  int centerY = M5.Display.height() / 2;

  drawCenterText(fortunes[idx].title, centerY - 4, 2);
  drawCenterText("Shake or A Again", centerY + 30, 1);
  drawCenterText("B Home", centerY + 46, 1);

  playResultSfx(rarity);

  // Prevent immediate second trigger after animation ends
  resultLockUntil = millis() + 1500;
  lastShakeTime = millis();
  resetShakeBaseline();
}

// Start animation
void startAnim() {
  appState = STATE_ANIMATING;
  animStartTime = millis();
  selectedIndex = drawFortuneWeighted();
  lastShakeSfxTime = 0;
}

// Update vertical shaking animation
void updateAnim() {
  uint32_t elapsed = millis() - animStartTime;

  if (elapsed >= ANIM_DURATION) {
    showResult(selectedIndex);
    return;
  }

  playShakeSfx();

  int amplitude = 12;
  if (elapsed > ANIM_DURATION * 2 / 3) amplitude = 7;
  if (elapsed > ANIM_DURATION * 5 / 6) amplitude = 4;

  int step = elapsed / 80;
  int offsetY = 0;

  if (step % 2 == 0) {
    offsetY = -amplitude;
  } else {
    offsetY = amplitude;
  }

  M5.Display.clear(BLACK);
  M5.Display.setTextColor(WHITE, BLACK);

  drawTubeVertical(offsetY, step);

  delay(45);
}

// Detect shake
bool detectShake() {
  if (millis() < resultLockUntil) return false;

  if (!M5.Imu.update()) return false;

  auto d = M5.Imu.getImuData();

  if (!hasLastAccel) {
    lastAx = d.accel.x;
    lastAy = d.accel.y;
    lastAz = d.accel.z;
    hasLastAccel = true;
    return false;
  }

  float dx = d.accel.x - lastAx;
  float dy = d.accel.y - lastAy;
  float dz = d.accel.z - lastAz;

  lastAx = d.accel.x;
  lastAy = d.accel.y;
  lastAz = d.accel.z;

  float delta = sqrtf(dx * dx + dy * dy + dz * dz);

  uint32_t now = millis();
  if (delta > SHAKE_THRESHOLD && (now - lastShakeTime) > SHAKE_COOLDOWN) {
    lastShakeTime = now;
    return true;
  }

  return false;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextColor(WHITE, BLACK);

  M5.Speaker.setVolume(80);

  randomSeed(micros());

  showIdle();
}

void loop() {
  M5.update();

  if (M5.BtnB.wasPressed()) {
    showIdle();
    return;
  }

  if (appState == STATE_ANIMATING) {
    updateAnim();
    return;
  }

  if (M5.BtnA.wasPressed()) {
    startAnim();
    return;
  }

  if (detectShake()) {
    startAnim();
    return;
  }

  delay(20);
}