#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <Preferences.h>
#include <math.h>

// ============================================================
// Chrome Dino Style Game for M5StickS3
// ------------------------------------------------------------
// Controls:
// - BtnA: Start / Jump / Restart / Confirm
// - BtnB: Return to Home (available in GAME OVER / NEW RECORD)
//
// Features:
// - Off-screen sprite rendering to reduce flicker
// - Start sound / running sound / jump sound / game over sound
// - Persistent high score
// - New record celebration page and sound
// ============================================================

// ------------------------------------------------------------
// Screen settings
// ------------------------------------------------------------
static const int SCREEN_W = 240;
static const int SCREEN_H = 135;

// ------------------------------------------------------------
// World settings
// ------------------------------------------------------------
static const int GROUND_Y = 112;
static const int GROUND_SEGMENT_SPACING = 18;

// ------------------------------------------------------------
// Physics
// ------------------------------------------------------------
static const float GRAVITY = 0.72f;
static const float JUMP_VELOCITY = -8.8f;

// ------------------------------------------------------------
// Timing
// ------------------------------------------------------------
static const uint32_t FRAME_INTERVAL = 16;
static const uint32_t SCORE_INTERVAL = 100;
static const uint32_t LEG_ANIM_INTERVAL = 120;
static const uint32_t FLASH_INTERVAL = 220;
static const uint32_t RUN_SOUND_INTERVAL = 900;
static const uint32_t NEW_RECORD_SCREEN_TIME = 2200;

// ------------------------------------------------------------
// Persistent storage
// ------------------------------------------------------------
Preferences prefs;

// ------------------------------------------------------------
// Game state
// ------------------------------------------------------------
enum GameState {
  STATE_HOME,
  STATE_PLAYING,
  STATE_NEW_RECORD,
  STATE_GAME_OVER
};

GameState gameState = STATE_HOME;

// ------------------------------------------------------------
// Off-screen canvas
// ------------------------------------------------------------
M5Canvas canvas(&M5.Display);

// ------------------------------------------------------------
// Dino
// ------------------------------------------------------------
float dinoX = 28;
float dinoY = 0;
float dinoVy = 0.0f;
const int dinoW = 18;
const int dinoH = 20;
bool dinoJumping = false;
bool dinoLegFrame = false;

// ------------------------------------------------------------
// Obstacle
// ------------------------------------------------------------
struct Obstacle {
  int x;
  int y;
  int w;
  int h;
  int type;  // 0 = cactus, 1 = bird
};

Obstacle obstacle;

// ------------------------------------------------------------
// Clouds
// ------------------------------------------------------------
struct Cloud {
  int x;
  int y;
  int w;
};

Cloud cloud1;
Cloud cloud2;

// ------------------------------------------------------------
// Sound sequence system
// ------------------------------------------------------------
struct ToneStep {
  uint16_t freq;
  uint16_t dur;
  uint16_t gap;
};

static const ToneStep SND_START[] = {
  {1200, 70, 20},
  {1600, 70, 20},
  {2200, 90, 30}
};

static const ToneStep SND_GAME_OVER[] = {
  {1200, 90, 20},
  {900, 100, 20},
  {650, 160, 30}
};

static const ToneStep SND_NEW_RECORD[] = {
  {1400, 80, 20},
  {1800, 80, 20},
  {2200, 90, 20},
  {2600, 120, 40},
  {3200, 160, 50}
};

const ToneStep* activeMelody = nullptr;
size_t activeMelodyLength = 0;
size_t activeMelodyIndex = 0;
uint32_t nextToneTime = 0;
bool melodyPlaying = false;

// ------------------------------------------------------------
// Game variables
// ------------------------------------------------------------
uint32_t score = 0;
uint32_t highScore = 0;
int gameSpeed = 4;
int groundOffset = 0;

uint32_t lastFrameTime = 0;
uint32_t lastScoreTime = 0;
uint32_t lastLegToggleTime = 0;
uint32_t lastFlashTime = 0;
uint32_t lastRunSoundTime = 0;
uint32_t newRecordEnterTime = 0;

bool flashVisible = true;
bool isNewHighScoreThisRound = false;

// ============================================================
// Button helpers
// ============================================================
// If BtnB does not respond on your device, replace BtnB with BtnPWR.

bool btnAPressed() {
  return M5.BtnA.wasPressed();
}

bool btnBPressed() {
  return M5.BtnB.wasPressed();
}

// ============================================================
// Storage helpers
// ============================================================
void loadHighScore() {
  prefs.begin("dino_game", true);
  highScore = prefs.getUInt("highscore", 0);
  prefs.end();
}

void saveHighScore() {
  prefs.begin("dino_game", false);
  prefs.putUInt("highscore", highScore);
  prefs.end();
}

// ============================================================
// Sound helpers
// ============================================================
void startMelody(const ToneStep* melody, size_t len) {
  activeMelody = melody;
  activeMelodyLength = len;
  activeMelodyIndex = 0;
  nextToneTime = 0;
  melodyPlaying = true;
}

void stopMelody() {
  melodyPlaying = false;
  activeMelody = nullptr;
  activeMelodyLength = 0;
  activeMelodyIndex = 0;
  nextToneTime = 0;
}

void updateMelody() {
  if (!melodyPlaying || activeMelody == nullptr) return;

  uint32_t now = millis();
  if (now < nextToneTime) return;

  if (activeMelodyIndex >= activeMelodyLength) {
    stopMelody();
    return;
  }

  const ToneStep &step = activeMelody[activeMelodyIndex];
  if (step.freq > 0 && step.dur > 0) {
    M5.Speaker.tone(step.freq, step.dur);
  }

  nextToneTime = now + step.dur + step.gap;
  activeMelodyIndex++;
}

void playStartSound() {
  startMelody(SND_START, sizeof(SND_START) / sizeof(SND_START[0]));
}

void playGameOverSound() {
  startMelody(SND_GAME_OVER, sizeof(SND_GAME_OVER) / sizeof(SND_GAME_OVER[0]));
}

void playNewRecordSound() {
  startMelody(SND_NEW_RECORD, sizeof(SND_NEW_RECORD) / sizeof(SND_NEW_RECORD[0]));
}

void playJumpSound() {
  M5.Speaker.tone(2500, 40);
}

void playRunSound() {
  // A subtle short tick during gameplay
  M5.Speaker.tone(900, 18);
}

// ============================================================
// Helper functions
// ============================================================
int getGroundTop() {
  return GROUND_Y - dinoH;
}

void initCloud(Cloud &c, int startX, int minY, int maxY, int width) {
  c.x = startX;
  c.y = random(minY, maxY);
  c.w = width;
}

void updateCloud(Cloud &c, int minGap, int maxGap, int minY, int maxY) {
  c.x -= 1;
  if (c.x + c.w < 0) {
    c.x = SCREEN_W + random(minGap, maxGap);
    c.y = random(minY, maxY);
  }
}

void randomizeObstacle() {
  int typeRoll = random(0, 100);

  if (typeRoll < 75) {
    obstacle.type = 0;
    obstacle.w = random(10, 16);
    obstacle.h = random(18, 28);
    obstacle.x = SCREEN_W + random(95, 145);
    obstacle.y = GROUND_Y - obstacle.h;
  } else {
    obstacle.type = 1;
    obstacle.w = 16;
    obstacle.h = 12;
    obstacle.x = SCREEN_W + random(105, 160);

    int birdLevel = random(0, 3);
    if (birdLevel == 0) {
      obstacle.y = GROUND_Y - 18;
    } else if (birdLevel == 1) {
      obstacle.y = GROUND_Y - 34;
    } else {
      obstacle.y = GROUND_Y - 48;
    }
  }
}

void resetWorld() {
  dinoY = getGroundTop();
  dinoVy = 0.0f;
  dinoJumping = false;
  dinoLegFrame = false;

  score = 0;
  gameSpeed = 4;
  groundOffset = 0;

  initCloud(cloud1, 180, 18, 36, 26);
  initCloud(cloud2, 85, 28, 52, 20);

  randomizeObstacle();

  lastScoreTime = millis();
  lastLegToggleTime = millis();
  lastFlashTime = millis();
  lastRunSoundTime = millis();

  flashVisible = true;
  isNewHighScoreThisRound = false;
}

void goHome() {
  resetWorld();
  stopMelody();
  gameState = STATE_HOME;
}

void startGame() {
  resetWorld();
  stopMelody();
  playStartSound();
  gameState = STATE_PLAYING;
}

void enterGameOverOrCelebrate() {
  if (score > highScore) {
    highScore = score;
    saveHighScore();
    isNewHighScoreThisRound = true;
    newRecordEnterTime = millis();
    stopMelody();
    playNewRecordSound();
    gameState = STATE_NEW_RECORD;
  } else {
    stopMelody();
    playGameOverSound();
    gameState = STATE_GAME_OVER;
  }

  lastFlashTime = millis();
  flashVisible = true;
}

// ============================================================
// Drawing functions
// ============================================================
void drawCloud(const Cloud &c) {
  canvas.fillRoundRect(c.x + 4, c.y + 4, c.w - 8, 8, 4, WHITE);
  canvas.fillCircle(c.x + 8, c.y + 8, 6, WHITE);
  canvas.fillCircle(c.x + c.w / 2, c.y + 4, 7, WHITE);
  canvas.fillCircle(c.x + c.w - 8, c.y + 8, 6, WHITE);
}

void drawGround() {
  canvas.drawFastHLine(0, GROUND_Y, SCREEN_W, WHITE);
  canvas.drawFastHLine(0, GROUND_Y + 1, SCREEN_W, WHITE);
  canvas.drawFastHLine(0, GROUND_Y + 2, SCREEN_W, WHITE);

  for (int i = -groundOffset; i < SCREEN_W; i += GROUND_SEGMENT_SPACING) {
    canvas.drawFastHLine(i, GROUND_Y - 4, 8, WHITE);
  }
}

void drawDino(int x, int y) {
  canvas.fillRect(x + 10, y, 8, 8, WHITE);
  canvas.drawPixel(x + 15, y + 2, BLACK);
  canvas.fillRect(x + 13, y + 6, 5, 2, WHITE);
  canvas.fillRect(x + 9, y + 5, 3, 4, WHITE);
  canvas.fillRect(x + 5, y + 7, 10, 9, WHITE);
  canvas.fillRect(x + 1, y + 10, 4, 3, WHITE);
  canvas.fillRect(x + 13, y + 10, 3, 4, WHITE);

  if (dinoJumping) {
    canvas.fillRect(x + 7, y + 15, 6, 3, WHITE);
  } else {
    if (dinoLegFrame) {
      canvas.fillRect(x + 6, y + 16, 3, 4, WHITE);
      canvas.fillRect(x + 11, y + 15, 3, 5, WHITE);
    } else {
      canvas.fillRect(x + 6, y + 15, 3, 5, WHITE);
      canvas.fillRect(x + 11, y + 16, 3, 4, WHITE);
    }
  }
}

void drawCactus(const Obstacle &o) {
  canvas.fillRect(o.x, o.y, o.w, o.h, WHITE);
  if (o.h > 20) {
    canvas.fillRect(o.x - 3, o.y + 8, 3, 6, WHITE);
    canvas.fillRect(o.x + o.w, o.y + 11, 3, 6, WHITE);
  }
}

void drawBird(const Obstacle &o) {
  canvas.fillRect(o.x + 3, o.y + 4, 8, 4, WHITE);
  canvas.fillRect(o.x + 11, o.y + 3, 3, 3, WHITE);
  canvas.drawPixel(o.x + 14, o.y + 4, WHITE);

  bool wingUp = ((score / 4) % 2 == 0);
  if (wingUp) {
    canvas.drawLine(o.x + 5, o.y + 4, o.x + 1, o.y + 1, WHITE);
    canvas.drawLine(o.x + 6, o.y + 4, o.x + 2, o.y + 1, WHITE);
  } else {
    canvas.drawLine(o.x + 5, o.y + 7, o.x + 1, o.y + 10, WHITE);
    canvas.drawLine(o.x + 6, o.y + 7, o.x + 2, o.y + 10, WHITE);
  }
}

void drawObstacle(const Obstacle &o) {
  if (o.type == 0) {
    drawCactus(o);
  } else {
    drawBird(o);
  }
}

void drawHUD() {
  canvas.setTextColor(WHITE, BLACK);
  canvas.setTextSize(1);

  canvas.setCursor(150, 8);
  canvas.printf("HI %05lu", (unsigned long)highScore);

  canvas.setCursor(150, 20);
  canvas.printf("%05lu", (unsigned long)score);
}

void drawHomeScreen() {
  canvas.setTextColor(WHITE, BLACK);
  canvas.setTextSize(2);
  canvas.setCursor(58, 18);
  canvas.print("DINO");

  canvas.setTextSize(1);
  canvas.setCursor(68, 58);
  canvas.print("Press A to Start");

  canvas.setCursor(68, 74);
  canvas.printf("HI %05lu", (unsigned long)highScore);

  canvas.setCursor(68, 90);
  canvas.print("A: Start");
}

void drawGameOverScreen() {
  canvas.setTextColor(WHITE, BLACK);
  canvas.setTextSize(2);
  canvas.setCursor(42, 26);
  canvas.print("GAME OVER");

  canvas.setTextSize(1);
  canvas.setCursor(72, 54);
  canvas.printf("SCORE %05lu", (unsigned long)score);

  canvas.setCursor(72, 66);
  canvas.printf("HI    %05lu", (unsigned long)highScore);

  if (flashVisible) {
    canvas.setCursor(50, 90);
    canvas.print("A: Restart   B: Home");
  }
}

void drawNewRecordScreen() {
  canvas.setTextColor(WHITE, BLACK);

  canvas.setTextSize(2);
  canvas.setCursor(34, 18);
  canvas.print("NEW RECORD!");

  canvas.setTextSize(1);
  canvas.setCursor(72, 50);
  canvas.printf("BEST %05lu", (unsigned long)highScore);

  canvas.setCursor(72, 64);
  canvas.printf("SCORE %05lu", (unsigned long)score);

  // Simple celebration sparkles
  canvas.fillCircle(30, 30, 2, WHITE);
  canvas.drawFastHLine(22, 30, 16, WHITE);
  canvas.drawFastVLine(30, 22, 16, WHITE);

  canvas.fillCircle(205, 28, 2, WHITE);
  canvas.drawFastHLine(197, 28, 16, WHITE);
  canvas.drawFastVLine(205, 20, 16, WHITE);

  canvas.fillCircle(36, 88, 2, WHITE);
  canvas.drawFastHLine(28, 88, 16, WHITE);
  canvas.drawFastVLine(36, 80, 16, WHITE);

  canvas.fillCircle(208, 90, 2, WHITE);
  canvas.drawFastHLine(200, 90, 16, WHITE);
  canvas.drawFastVLine(208, 82, 16, WHITE);

  if (flashVisible) {
    canvas.setCursor(42, 104);
    canvas.print("A: Restart   B: Home");
  }
}

void renderScene() {
  canvas.fillScreen(BLACK);

  drawCloud(cloud1);
  drawCloud(cloud2);
  drawGround();
  drawDino((int)dinoX, (int)dinoY);

  if (gameState == STATE_PLAYING || gameState == STATE_GAME_OVER || gameState == STATE_NEW_RECORD) {
    drawObstacle(obstacle);
    drawHUD();
  }

  if (gameState == STATE_HOME) {
    drawHomeScreen();
  } else if (gameState == STATE_GAME_OVER) {
    drawGameOverScreen();
  } else if (gameState == STATE_NEW_RECORD) {
    drawNewRecordScreen();
  }

  canvas.pushSprite(0, 0);
}

// ============================================================
// Logic
// ============================================================
void handleHomeInput() {
  if (btnAPressed()) {
    startGame();
  }
}

void handlePlayingInput() {
  if (btnAPressed() && !dinoJumping) {
    dinoJumping = true;
    dinoVy = JUMP_VELOCITY;
    playJumpSound();
  }
}

void handleGameOverInput() {
  if (btnAPressed()) {
    startGame();
    return;
  }

  if (btnBPressed()) {
    goHome();
    return;
  }
}

void handleNewRecordInput() {
  if (btnAPressed()) {
    startGame();
    return;
  }

  if (btnBPressed()) {
    goHome();
    return;
  }
}

void updateDino() {
  if (dinoJumping) {
    dinoVy += GRAVITY;
    dinoY += dinoVy;

    if (dinoY >= getGroundTop()) {
      dinoY = getGroundTop();
      dinoVy = 0.0f;
      dinoJumping = false;
    }
  } else {
    uint32_t now = millis();
    if (now - lastLegToggleTime >= LEG_ANIM_INTERVAL) {
      dinoLegFrame = !dinoLegFrame;
      lastLegToggleTime = now;
    }
  }
}

void updateGround() {
  groundOffset += gameSpeed;
  groundOffset %= GROUND_SEGMENT_SPACING;
}

void updateClouds() {
  updateCloud(cloud1, 10, 40, 18, 36);
  updateCloud(cloud2, 50, 90, 28, 52);
}

void updateObstacle() {
  obstacle.x -= gameSpeed;

  if (obstacle.x + obstacle.w < 0) {
    randomizeObstacle();
  }
}

void updateScoreAndSpeed() {
  uint32_t now = millis();

  if (now - lastScoreTime >= SCORE_INTERVAL) {
    lastScoreTime = now;
    score++;

    if (score % 80 == 0 && gameSpeed < 10) {
      gameSpeed++;
    }
  }
}

void updateRunSound() {
  if (gameState != STATE_PLAYING) return;
  if (dinoJumping) return;

  uint32_t now = millis();
  if (now - lastRunSoundTime >= RUN_SOUND_INTERVAL) {
    lastRunSoundTime = now;
    playRunSound();
  }
}

void updateFlash() {
  uint32_t now = millis();
  if (now - lastFlashTime >= FLASH_INTERVAL) {
    lastFlashTime = now;
    flashVisible = !flashVisible;
  }
}

bool checkCollision() {
  int dx = (int)dinoX + 3;
  int dy = (int)dinoY + 2;
  int dw = dinoW - 5;
  int dh = dinoH - 2;

  int ox = obstacle.x;
  int oy = obstacle.y;
  int ow = obstacle.w;
  int oh = obstacle.h;

  if (obstacle.type == 1) {
    ox += 1;
    oy += 2;
    ow -= 2;
    oh -= 3;
  }

  bool noOverlap =
      (dx + dw <= ox) ||
      (dx >= ox + ow) ||
      (dy + dh <= oy) ||
      (dy >= oy + oh);

  return !noOverlap;
}

void updatePlaying() {
  handlePlayingInput();
  updateDino();
  updateGround();
  updateClouds();
  updateObstacle();
  updateScoreAndSpeed();
  updateRunSound();

  if (checkCollision()) {
    enterGameOverOrCelebrate();
  }
}

void updateGameOver() {
  handleGameOverInput();
  updateFlash();
}

void updateNewRecord() {
  handleNewRecordInput();
  updateFlash();

  uint32_t now = millis();
  if (now - newRecordEnterTime >= NEW_RECORD_SCREEN_TIME) {
    stopMelody();
    playGameOverSound();
    gameState = STATE_GAME_OVER;
    lastFlashTime = millis();
    flashVisible = true;
  }
}

// ============================================================
// Arduino
// ============================================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.fillScreen(BLACK);

  canvas.setColorDepth(16);
  canvas.createSprite(SCREEN_W, SCREEN_H);
  canvas.setTextColor(WHITE, BLACK);
  canvas.setTextSize(1);

  randomSeed(micros());

  loadHighScore();
  goHome();

  lastFrameTime = millis();
}

void loop() {
  M5.update();
  updateMelody();

  uint32_t now = millis();
  if (now - lastFrameTime < FRAME_INTERVAL) {
    return;
  }
  lastFrameTime = now;

  switch (gameState) {
    case STATE_HOME:
      handleHomeInput();
      break;

    case STATE_PLAYING:
      updatePlaying();
      break;

    case STATE_NEW_RECORD:
      updateNewRecord();
      break;

    case STATE_GAME_OVER:
      updateGameOver();
      break;
  }

  renderScene();
}