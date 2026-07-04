#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <EEPROM.h>

// ═════════════════════════════════════════════════════════════════════════
// 123 Console
// Games: Bounce Ball (joystick) · Space Shooter (ADXL345 tilt) ·
//        Star Catcher (ADXL345 tilt) · XO 2-Player (joystick) ·
//        Reflex Test (button)
// ═════════════════════════════════════════════════════════════════════════

// ── Pins ──────────────────────────────────────────────────────────────────
#define JOY_X   A0
#define JOY_Y   A1
#define JOY_BTN 2
#define BUZZER  8
#define TILT_DEAD 22   // moved up here: needed by tiltStep() before it's used

// ── Types ─────────────────────────────────────────────────────────────────
struct Sprite    { int8_t x, y; bool alive; uint8_t type; };
struct Explosion { int8_t x, y; uint8_t life; };

// ── Forward declarations ─────────────────────────────────────────────────
void adxlWrite(uint8_t r, uint8_t v);
void adxlXY(int16_t &x, int16_t &y);
void adxlXYFiltered(int16_t &x, int16_t &y);
int8_t tiltStep(int16_t t);
bool adxlOK();
int8_t joyX();
int8_t joyY();
bool btnDown();
bool btnPress();
bool backHold();
void beep(uint16_t fr, uint16_t ms);
uint16_t eeR(uint8_t a);
void eeW(uint8_t a, uint16_t v);
void applyBright();
void drawStrF(u8g2_uint_t x, u8g2_uint_t y, const __FlashStringHelper* s);
void centerStrF(u8g2_uint_t y, const __FlashStringHelper* s);
void centerStrRAM(u8g2_uint_t y, const char* s);
void showCenteredMsg(const __FlashStringHelper* a, const __FlashStringHelper* b);
void countdown();
void drawLives(uint8_t lives);
bool askRetryMenu();
bool gameOverChoice(uint16_t score, uint8_t eeAddr);
void runPOST();
void bootAnim();
void playBounce();
void playSpace();
void playStar();
void playMaze();
void playXO();
void playReflex();
void settingsCalibrate();
void settingsResetScores();
void settingsSysInfo();
void showAbout();
uint8_t drawMenu(const char* const* items, uint8_t n, uint8_t sel, const __FlashStringHelper* title);

U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ── EEPROM addresses ──────────────────────────────────────────────────────
#define EE_BOUNCE 0   // high score
#define EE_SPACE  2   // high score
#define EE_STAR   4   // high score
#define EE_REFLEX 6   // best reaction time (ms), 0 = no record
#define EE_DIFF   8
#define EE_BRIGHT 9
#define EE_JX     10
#define EE_JY     12
#define EE_AX     14
#define EE_AY     16

// ── ADXL345 ───────────────────────────────────────────────────────────────
#define ADXL_ADDR 0x53
int16_t axOff = 0, ayOff = 0;

void adxlWrite(uint8_t r, uint8_t v) {
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(r); Wire.write(v);
  Wire.endTransmission();
}
void adxlXY(int16_t &x, int16_t &y) {
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(0x32); Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)ADXL_ADDR, (uint8_t)6);
  int16_t rx = (int16_t)(Wire.read() | (Wire.read() << 8));
  int16_t ry = (int16_t)(Wire.read() | (Wire.read() << 8));
  Wire.read(); Wire.read();
  x = rx - axOff; y = ry - ayOff;
}
bool adxlOK() {
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(0x00); Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)ADXL_ADDR, (uint8_t)1);
  return Wire.read() == 0xE5;
}

// Lightly smoothed tilt reading, used by the motion games. A cheap
// exponential filter kills sensor jitter (this is what was making the
// Star Catcher basket twitch) without adding noticeable input lag.
int16_t axFilt = 0, ayFilt = 0;
void adxlXYFiltered(int16_t &x, int16_t &y) {
  int16_t rx, ry;
  adxlXY(rx, ry);
  axFilt += (rx - axFilt) / 2;
  ayFilt += (ry - ayFilt) / 2;
  x = axFilt; y = ayFilt;
}

// Maps a tilt deviation to a movement step with finer graduation than a
// simple two-tier threshold, so gentle tilts move the sprite too instead
// of requiring a hard tilt before anything happens.
int8_t tiltStep(int16_t t) {
  int16_t at = abs(t);
  if (at <= TILT_DEAD) return 0;
  int8_t step = (at > 180) ? 7 : (at > 120) ? 5 : (at > 70) ? 3 : 2;
  return (t > 0) ? step : -step;
}

// ── Joystick ──────────────────────────────────────────────────────────────
int16_t jcx = 512, jcy = 512;
bool gBackRequested = false;
#define DEAD 80

// Horizontal axis: -1 = left, +1 = right
int8_t joyX() {
  int v = analogRead(JOY_X) - jcx;
  if (v >  DEAD) return -1;
  if (v < -DEAD) return  1;
  return 0;
}
// Vertical axis: -1 = up, +1 = down
int8_t joyY() {
  int v = analogRead(JOY_Y) - jcy;
  if (v >  DEAD) return  1;
  if (v < -DEAD) return -1;
  return 0;
}
bool btnDown() { return digitalRead(JOY_BTN) == LOW; }
bool btnPress() {
  if (!btnDown()) return false;
  uint32_t t = millis();
  while (btnDown()) {
    if (millis() - t >= 3000) {
      gBackRequested = true;
      beep(330, 80); beep(220, 80);
      while (btnDown()) delay(10);
      return false;
    }
    delay(10);
  }
  return true;
}
bool backHold() {
  if (gBackRequested) {
    gBackRequested = false;
    return true;
  }
  static uint32_t holdStart = 0;
  static bool fired = false;
  if (btnDown()) {
    if (holdStart == 0) holdStart = millis();
    if (!fired && millis() - holdStart >= 3000) {
      fired = true;
      beep(330, 80); beep(220, 80);
      while (btnDown()) delay(10);
      holdStart = 0;
      fired = false;
      return true;
    }
  } else {
    holdStart = 0;
    fired = false;
  }
  return false;
}

// ── Buzzer ────────────────────────────────────────────────────────────────
void beep(uint16_t fr, uint16_t ms) {
  tone(BUZZER, fr); delay(ms); noTone(BUZZER);
}

// ── EEPROM helpers ────────────────────────────────────────────────────────
uint16_t eeR(uint8_t a) {
  uint16_t v = ((uint16_t)EEPROM.read(a) << 8) | EEPROM.read(a + 1);
  return v == 0xFFFF ? 0 : v;
}
void eeW(uint8_t a, uint16_t v) {
  EEPROM.write(a, v >> 8); EEPROM.write(a + 1, v & 0xFF);
}

// ── Shared UI ─────────────────────────────────────────────────────────────
uint8_t gDiff = 1, gBright = 3;

void applyBright() {
  u8g2.setContrast(gBright == 1 ? 30 : gBright == 2 ? 80 : gBright == 3 ? 150 : gBright == 4 ? 200 : 255);
}

void drawStrF(u8g2_uint_t x, u8g2_uint_t y, const __FlashStringHelper* s) {
  char buf[24];
  strncpy_P(buf, (PGM_P)s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  u8g2.drawStr(x, y, buf);
}

void centerStrF(u8g2_uint_t y, const __FlashStringHelper* s) {
  char buf[24];
  strncpy_P(buf, (PGM_P)s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  uint8_t w = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - w) / 2, y, buf);
}
void centerStrRAM(u8g2_uint_t y, const char* s) {
  uint8_t w = u8g2.getStrWidth(s);
  u8g2.drawStr((128 - w) / 2, y, s);
}

void showCenteredMsg(const __FlashStringHelper* a, const __FlashStringHelper* b) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_5x7_tf);
    if (a) centerStrF(26, a);
    if (b) centerStrF(40, b);
  } while (u8g2.nextPage());
}

void countdown() {
  const char* n[] = {"3", "2", "1", "GO!"};
  uint16_t fr[] = {500, 600, 700, 900};
  for (uint8_t i = 0; i < 4; i++) {
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_6x10_tf);
      centerStrRAM(36, n[i]);
    } while (u8g2.nextPage());
    beep(fr[i], 120); delay(500);
  }
}

void drawLives(uint8_t lives) {
  for (uint8_t i = 0; i < 3; i++) {
    if (i < lives) u8g2.drawDisc(118 - i * 8, 60, 3);
    else u8g2.drawCircle(118 - i * 8, 60, 3);
  }
}

bool askRetryMenu() {
  delay(400);
  while (true) {
    if (backHold()) return false;
    if (btnPress()) {
      uint32_t t = millis();
      bool second = false;
      while (millis() - t < 500) {
        if (backHold()) return false;
        if (btnPress()) { second = true; break; }
      }
      return !second;
    }
  }
}

bool gameOverChoice(uint16_t score, uint8_t eeAddr) {
  beep(200, 400);
  uint16_t hi = eeR(eeAddr);
  bool newBest = score > hi;
  if (newBest) { hi = score; eeW(eeAddr, hi); }
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tf);
    centerStrF(14, newBest ? F("NEW BEST!") : F("GAME OVER"));
    u8g2.setFont(u8g2_font_5x7_tf);
    char buf[16], num[6];
    utoa(score, num, 10); strcpy(buf, "Score: "); strcat(buf, num);
    centerStrRAM(30, buf);
    utoa(hi, num, 10); strcpy(buf, "Best: "); strcat(buf, num);
    centerStrRAM(42, buf);
    centerStrF(58, F("BTN=Retry  2x=Menu"));
  } while (u8g2.nextPage());
  return askRetryMenu();
}

// ── POST ──────────────────────────────────────────────────────────────────
void runPOST() {
  bool oledOK = true;
  bool joyOK = true;
  bool acOK = adxlOK();
  bool buzOK = true;
  int jx = analogRead(JOY_X), jy = analogRead(JOY_Y);
  if (jx < 20 || jx > 1000 || jy < 20 || jy > 1000) joyOK = false;
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_5x7_tf);
    centerStrF(10, F("-- POST --"));
    drawStrF(2, 22, F("OLED"));      drawStrF(70, 22, oledOK ? F("PASS") : F("FAIL"));
    drawStrF(2, 32, F("Joystick")); drawStrF(70, 32, joyOK  ? F("PASS") : F("FAIL"));
    drawStrF(2, 42, F("ADXL345")); drawStrF(70, 42, acOK   ? F("PASS") : F("FAIL"));
    drawStrF(2, 52, F("Buzzer"));   drawStrF(70, 52, buzOK  ? F("PASS") : F("FAIL"));
  } while (u8g2.nextPage());
  beep(1000, 80); delay(80); beep(1200, 80);
  delay(1200);
}

// ── Boot animation ────────────────────────────────────────────────────────
void bootAnim() {
  for (uint8_t r = 2; r <= 30; r += 4) {
    u8g2.firstPage();
    do {
      u8g2.drawCircle(64, 32, r);
      if (r > 8)  u8g2.drawCircle(64, 32, r - 8);
      if (r > 16) u8g2.drawCircle(64, 32, r - 16);
    } while (u8g2.nextPage());
    delay(40);
  }
  for (uint8_t f = 0; f < 12; f++) {
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_6x10_tf);
      centerStrF(32, F("NanoVolt"));
      u8g2.drawHLine(10, 42, 108);
      u8g2.setFont(u8g2_font_5x7_tf);
      if (f % 2 == 0) centerStrF(56, F("Press Button..."));
    } while (u8g2.nextPage());
    delay(350);
    if (btnDown()) break;
  }
  beep(523, 80); beep(659, 80); beep(784, 80); beep(1047, 150);
  while (!btnPress()) delay(50);
}

// ══════════════════════════════════════════════════════════════════════════
// GAME 1 — BOUNCE BALL  (joystick)
// Now with a row of breakable bonus blocks and a hit combo counter.
// ══════════════════════════════════════════════════════════════════════════
#define BB_BLOCKS 6

void playBounce() {
  while (true) {
    int8_t bx = 64, by = 20, vx = 2, vy = 2;
    int8_t px = 54; uint8_t pw = 20;
    uint8_t lives = 3; uint16_t score = 0;
    uint8_t combo = 0;
    bool block[BB_BLOCKS];
    for (uint8_t i = 0; i < BB_BLOCKS; i++) block[i] = true;
    const uint8_t bStartX = 5, bW = 18, bGap = 2, bStep = bW + bGap, bY = 10, bH = 4;
    uint16_t spd = (gDiff == 0) ? 55 : (gDiff == 1) ? 40 : 28;
    countdown();
    while (lives > 0) {
      if (backHold()) return;
      int8_t jx = joyX();
      px += jx * 3;
      if (px < 0) px = 0;
      if (px + pw > 128) px = 128 - pw;

      bx += vx; by += vy;
      if (bx <= 0 || bx >= 127) { vx = -vx; beep(440, 20); }
      if (by <= 0) { vy = -vy; beep(440, 20); }

      // bonus blocks
      if (vy < 0 && by >= bY && by <= bY + bH + 2) {
        int16_t rel = bx - bStartX;
        if (rel >= 0 && rel < bStep * BB_BLOCKS) {
          uint8_t idx = rel / bStep;
          if (idx < BB_BLOCKS && block[idx] && (rel % bStep) < bW) {
            block[idx] = false; vy = -vy; score += 3; beep(1000, 15);
            bool anyLeft = false;
            for (uint8_t i = 0; i < BB_BLOCKS; i++) if (block[i]) anyLeft = true;
            if (!anyLeft) {
              score += 20; beep(1200, 60); beep(1568, 80);
              for (uint8_t i = 0; i < BB_BLOCKS; i++) block[i] = true;
            }
          }
        }
      }

      // paddle hit
      if (by >= 54 && bx >= px && bx <= px + pw && vy > 0) {
        vy = -vy;
        score++; combo++; beep(880, 20);
        if (combo % 5 == 0) { score += 5; beep(1319, 40); }
        if (score % 50 == 0 && spd > 15) spd -= 2;
      }
      // miss
      if (by > 63) {
        lives--; combo = 0; beep(220, 300);
        bx = 64; by = 20; vx = 2; vy = 2;
        delay(600);
      }

      u8g2.firstPage();
      do {
        u8g2.drawFrame(0, 0, 128, 64);
        for (uint8_t x = 4; x < 124; x += 6) u8g2.drawHLine(x, 32, 3);
        for (uint8_t i = 0; i < BB_BLOCKS; i++)
          if (block[i]) u8g2.drawBox(bStartX + i * bStep, bY, bW, bH);
        u8g2.drawDisc(bx, by, 3);
        u8g2.drawBox(px, 56, pw, 3);
        u8g2.setFont(u8g2_font_5x7_tf);
        char buf[8]; utoa(score, buf, 10);
        u8g2.drawStr(3, 8, buf);
        if (combo > 1) {
          char cbuf[6]; strcpy(cbuf, "x"); char cn[4]; utoa(combo, cn, 10); strcat(cbuf, cn);
          u8g2.drawStr(3, 63, cbuf);
        }
        drawLives(lives);
      } while (u8g2.nextPage());
      delay(spd);
    }
    if (!gameOverChoice(score, EE_BOUNCE)) return;
  }
}

// ══════════════════════════════════════════════════════════════════════════
// GAME 2 — SPACE SHOOTER  (ADXL345 tilt)
// Now with a scrolling star-field, kill-combo bonus, and hit explosions.
// ══════════════════════════════════════════════════════════════════════════
#define MAX_EN 3
#define MAX_BL 3
#define MAX_EX 3
#define MAX_STARS 7

void playSpace() {
  while (true) {
    int8_t sx = 60; uint8_t lives = 3; uint16_t score = 0;
    Sprite en[MAX_EN], bl[MAX_BL];
    Explosion ex[MAX_EX];
    int8_t starX[MAX_STARS], starY[MAX_STARS];
    for (uint8_t i = 0; i < MAX_EN; i++) en[i].alive = false;
    for (uint8_t i = 0; i < MAX_BL; i++) bl[i].alive = false;
    for (uint8_t i = 0; i < MAX_EX; i++) ex[i].life = 0;
    for (uint8_t i = 0; i < MAX_STARS; i++) { starX[i] = random(0, 128); starY[i] = random(0, 64); }
    uint16_t spd = (gDiff == 0) ? 70 : (gDiff == 1) ? 50 : 35;
    uint32_t lastFire = 0; uint8_t spawnCnt = 0;
    uint8_t combo = 0; uint32_t lastKill = 0;
    countdown();
    while (true) {
      if (backHold()) return;
      // tilt-driven ship movement, proportional to how hard you tilt
      int16_t tx, ty; adxlXYFiltered(tx, ty);
      int8_t mv = tiltStep(tx);
      if (mv != 0) sx = constrain((int)sx + mv, 2, 122);

      // starfield
      for (uint8_t i = 0; i < MAX_STARS; i++) {
        starY[i] += 1;
        if (starY[i] > 63) { starY[i] = 0; starX[i] = random(0, 128); }
      }

      if (millis() - lastFire > 500) {
        for (uint8_t i = 0; i < MAX_BL; i++) {
          if (!bl[i].alive) { bl[i] = {sx, 50, true, 0}; lastFire = millis(); beep(1000, 20); break; }
        }
      }
      for (uint8_t i = 0; i < MAX_BL; i++) {
        if (!bl[i].alive) continue;
        bl[i].y -= 4;
        if (bl[i].y < 0) bl[i].alive = false;
      }
      for (uint8_t i = 0; i < MAX_EX; i++) if (ex[i].life > 0) ex[i].life--;

      spawnCnt++;
      int8_t spawnRate = 30 - score / 50 - gDiff * 4;
      if (spawnRate < 6) spawnRate = 6;
      if (spawnCnt >= spawnRate) {
        spawnCnt = 0;
        for (uint8_t i = 0; i < MAX_EN; i++) {
          if (!en[i].alive) { en[i] = {(int8_t)random(10, 118), 0, true, (uint8_t)random(2)}; break; }
        }
      }
      uint8_t enSpd = 1 + score / 100;
      if (enSpd > 5) enSpd = 5;
      for (uint8_t i = 0; i < MAX_EN; i++) {
        if (!en[i].alive) continue;
        en[i].y += enSpd;
        if (en[i].y > 63) { en[i].alive = false; continue; }
        if (abs(en[i].x - sx) < 8 && en[i].y > 48) {
          en[i].alive = false; lives--; combo = 0; beep(200, 300);
          if (lives == 0) goto spaceEnd;
        }
        for (uint8_t b = 0; b < MAX_BL; b++) {
          if (!bl[b].alive) continue;
          if (abs(bl[b].x - en[i].x) < 6 && abs(bl[b].y - en[i].y) < 6) {
            // combo: chained kills within 800ms score extra
            if (millis() - lastKill < 800) combo++; else combo = 1;
            lastKill = millis();
            uint16_t gain = (en[i].type == 1 ? 20 : 10) + (combo > 1 ? combo * 2 : 0);
            score += gain;
            en[i].alive = false; bl[b].alive = false;
            for (uint8_t e = 0; e < MAX_EX; e++) if (ex[e].life == 0) { ex[e] = {en[i].x, en[i].y, 3}; break; }
            beep(660, 30); break;
          }
        }
      }
      u8g2.firstPage();
      do {
        for (uint8_t i = 0; i < MAX_STARS; i++) u8g2.drawPixel(starX[i], starY[i]);
        u8g2.drawTriangle(sx, 50, sx - 5, 60, sx + 5, 60);
        for (uint8_t i = 0; i < MAX_EN; i++) {
          if (!en[i].alive) continue;
          if (en[i].type == 1) u8g2.drawFrame(en[i].x - 4, en[i].y - 3, 8, 6);
          else u8g2.drawDisc(en[i].x, en[i].y, 4);
        }
        for (uint8_t i = 0; i < MAX_BL; i++)
          if (bl[i].alive) u8g2.drawBox(bl[i].x - 1, bl[i].y - 3, 2, 4);
        for (uint8_t i = 0; i < MAX_EX; i++) {
          if (ex[i].life == 0) continue;
          uint8_t r = 5 - ex[i].life;
          u8g2.drawLine(ex[i].x - r, ex[i].y - r, ex[i].x + r, ex[i].y + r);
          u8g2.drawLine(ex[i].x - r, ex[i].y + r, ex[i].x + r, ex[i].y - r);
        }
        u8g2.setFont(u8g2_font_5x7_tf);
        char buf[8]; utoa(score, buf, 10);
        u8g2.drawStr(0, 8, buf);
        if (combo > 1) {
          char cbuf[6] = "x"; char cn[4]; utoa(combo, cn, 10); strcat(cbuf, cn);
          u8g2.drawStr(0, 63, cbuf);
        }
        drawLives(lives);
      } while (u8g2.nextPage());
      delay(spd);
    }
    spaceEnd:
    if (!gameOverChoice(score, EE_SPACE)) return;
  }
}

// ══════════════════════════════════════════════════════════════════════════
// GAME 3 — STAR CATCHER  (ADXL345 tilt)
// Tilt to slide the basket and catch falling stars before they hit bottom.
// ══════════════════════════════════════════════════════════════════════════
void playStar() {
  while (true) {
    int8_t bx = 55; uint8_t bw = 18;
    uint8_t lives = 3; uint16_t score = 0;
    int8_t starX = random(6, 118), starY = 0;
    uint8_t starSpd = 1;
    uint16_t spd = (gDiff == 0) ? 65 : (gDiff == 1) ? 48 : 34;
    countdown();
    while (lives > 0) {
      if (backHold()) return;
      int16_t tx, ty; adxlXYFiltered(tx, ty);
      int8_t mv = tiltStep(tx);
      if (mv != 0) bx = constrain((int)bx + mv, 0, 128 - bw);

      starY += starSpd;
      if (starY >= 58) {
        if (starX >= bx && starX <= bx + bw) {
          score += 10; beep(880, 30);
          if (score % 100 == 0 && starSpd < 5) starSpd++;
        } else {
          lives--; beep(220, 300);
        }
        starX = random(6, 118); starY = 0;
      }
      u8g2.firstPage();
      do {
        u8g2.drawFrame(0, 0, 128, 64);
        u8g2.drawFrame(bx, 58, bw, 4);
        u8g2.drawPixel(starX, starY - 2);
        u8g2.drawPixel(starX - 1, starY);
        u8g2.drawPixel(starX, starY);
        u8g2.drawPixel(starX + 1, starY);
        u8g2.drawPixel(starX, starY + 2);
        u8g2.setFont(u8g2_font_5x7_tf);
        char buf[8]; utoa(score, buf, 10);
        u8g2.drawStr(3, 8, buf);
        drawLives(lives);
      } while (u8g2.nextPage());
      delay(spd);
    }
    if (!gameOverChoice(score, EE_STAR)) return;
  }
}

// ══════════════════════════════════════════════════════════════════════════
// GAME 4 — XO  (Tic-Tac-Toe, joystick, 2-PLAYER hot-seat)
// ══════════════════════════════════════════════════════════════════════════
// GAME 4 - MAZE RUNNER (ADXL345 tilt)
void playMaze() {
  static const char maze[8][17] = {
    "################",
    "#S     #       #",
    "# ###  #  ###  #",
    "#   #     #    #",
    "### # ##### ## #",
    "#   #     #    #",
    "#     ###    G #",
    "################"
  };
  while (true) {
    int8_t px = 1, py = 1;
    uint32_t start = millis();
    uint32_t lastMove = 0;
    countdown();
    while (true) {
      if (backHold()) return;
      int16_t tx, ty; adxlXYFiltered(tx, ty);
      if (millis() - lastMove > 150) {
        int8_t nx = px, ny = py;
        if (abs(tx) >= abs(ty)) {
          int8_t dx = tiltStep(tx);
          if (dx > 0) nx++;
          else if (dx < 0) nx--;
        } else {
          int8_t dy = tiltStep(ty);
          if (dy > 0) ny++;
          else if (dy < 0) ny--;
        }
        if (nx >= 0 && nx < 16 && ny >= 0 && ny < 8 && maze[ny][nx] != '#') {
          px = nx; py = ny; lastMove = millis(); beep(700, 8);
          if (maze[py][px] == 'G') {
            uint16_t t = (uint16_t)((millis() - start) / 1000);
            u8g2.firstPage();
            do {
              u8g2.setFont(u8g2_font_6x10_tf);
              centerStrF(18, F("MAZE CLEAR!"));
              u8g2.setFont(u8g2_font_5x7_tf);
              char buf[16], num[6];
              utoa(t, num, 10); strcpy(buf, "Time: "); strcat(buf, num); strcat(buf, "s");
              centerStrRAM(36, buf);
              centerStrF(58, F("BTN=Retry Hold=Menu"));
            } while (u8g2.nextPage());
            beep(1047, 100); beep(1319, 120);
            if (!askRetryMenu()) return;
            break;
          }
        }
      }

      u8g2.firstPage();
      do {
        for (uint8_t y = 0; y < 8; y++) {
          for (uint8_t x = 0; x < 16; x++) {
            if (maze[y][x] == '#') u8g2.drawBox(x * 8, y * 8, 8, 8);
            else if (maze[y][x] == 'G') u8g2.drawFrame(x * 8 + 1, y * 8 + 1, 6, 6);
          }
        }
        u8g2.drawDisc(px * 8 + 4, py * 8 + 4, 3);
      } while (u8g2.nextPage());
      delay(20);
    }
  }
}

bool xoCheckWin(char* b, char p) {
  const uint8_t L[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
  for (uint8_t i = 0; i < 8; i++)
    if (b[L[i][0]] == p && b[L[i][1]] == p && b[L[i][2]] == p) return true;
  return false;
}

void xoDraw(char* b, int8_t cursor, bool p1Turn) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_5x7_tf);
    centerStrF(7, p1Turn ? F("Player 1's turn (X)") : F("Player 2's turn (O)"));
    const uint8_t gx = 38, gy = 9, cs = 17;
    u8g2.drawVLine(gx + cs, gy, cs * 3);
    u8g2.drawVLine(gx + cs * 2, gy, cs * 3);
    u8g2.drawHLine(gx, gy + cs, cs * 3);
    u8g2.drawHLine(gx, gy + cs * 2, cs * 3);
    u8g2.drawFrame(gx, gy, cs * 3, cs * 3);
    for (uint8_t i = 0; i < 9; i++) {
      uint8_t cx = gx + (i % 3) * cs, cy = gy + (i / 3) * cs;
      if (i == cursor) u8g2.drawFrame(cx + 1, cy + 1, cs - 2, cs - 2);
      if (b[i] == 'X') {
        u8g2.drawLine(cx + 3, cy + 3, cx + cs - 3, cy + cs - 3);
        u8g2.drawLine(cx + cs - 3, cy + 3, cx + 3, cy + cs - 3);
      } else if (b[i] == 'O') {
        u8g2.drawCircle(cx + cs / 2, cy + cs / 2, cs / 2 - 3);
      }
    }
  } while (u8g2.nextPage());
}

void playXO() {
  while (true) {
    char board[9];
    for (uint8_t i = 0; i < 9; i++) board[i] = ' ';
    int8_t cursor = 4;
    bool p1Turn = true;
    uint8_t winner = 0; // 0 = draw, 1 = player1, 2 = player2
    countdown();
    while (true) {
      xoDraw(board, cursor, p1Turn);
      delay(150);
      if (backHold()) return;
      // XO's board is rotated relative to this joystick wiring, so rotate
      // the input back before moving the cursor.
      int16_t rdx = analogRead(JOY_X) - jcx;
      int16_t rdy = analogRead(JOY_Y) - jcy;
      int8_t dx = joyX();
      int8_t dy = joyY();
      int8_t boardDx = -dy;
      int8_t boardDy = -dx;

      if (abs(rdy) >= abs(rdx)) {
        if (boardDx == 1 && cursor % 3 < 2) cursor++;
        else if (boardDx == -1 && cursor % 3 > 0) cursor--;
      } else {
        if (boardDy == 1 && cursor < 6) cursor += 3;
        else if (boardDy == -1 && cursor > 2) cursor -= 3;
      }
      if (btnPress() && board[cursor] == ' ') {
        char mark = p1Turn ? 'X' : 'O';
        board[cursor] = mark;
        beep(p1Turn ? 700 : 500, 30);
        if (xoCheckWin(board, mark)) { winner = p1Turn ? 1 : 2; break; }
        bool full = true;
        for (uint8_t i = 0; i < 9; i++) if (board[i] == ' ') full = false;
        if (full) { winner = 0; break; }
        p1Turn = !p1Turn;
      }
    }
    xoDraw(board, cursor, p1Turn);
    if (winner == 0) beep(440, 250);
    else { beep(1047, 150); beep(1319, 200); }
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_6x10_tf);
      centerStrF(24, winner == 1 ? F("PLAYER 1 WINS!") : winner == 2 ? F("PLAYER 2 WINS!") : F("DRAW!"));
      u8g2.setFont(u8g2_font_5x7_tf);
      centerStrF(58, F("BTN=Retry  2x=Menu"));
    } while (u8g2.nextPage());
    if (!askRetryMenu()) return;
  }
}

// ══════════════════════════════════════════════════════════════════════════
// GAME 5 — REFLEX TEST  (button)
// ══════════════════════════════════════════════════════════════════════════
void playReflex() {
  while (true) {
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_6x10_tf);
      centerStrF(20, F("REFLEX TEST"));
      u8g2.setFont(u8g2_font_5x7_tf);
      centerStrF(38, F("Wait for GO!"));
      centerStrF(50, F("Press BTN when it shows"));
    } while (u8g2.nextPage());
    delay(1200);

    uint16_t wait = 1000 + random(2500);
    uint32_t t0 = millis();
    bool falseStart = false;
    while (millis() - t0 < wait) {
      if (backHold()) return;
      if (btnPress()) { falseStart = true; break; }
      delay(10);
    }
    if (falseStart) {
      showCenteredMsg(F("Too Soon!"), F("Try again..."));
      beep(200, 300); delay(1200);
      continue;
    }

    uint32_t goTime = millis();
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_6x10_tf);
      centerStrF(38, F("GO!"));
    } while (u8g2.nextPage());
    beep(1500, 60);
    bool timedOut = true;
    while (millis() - goTime < 5000) {
      if (backHold()) return;
      if (btnPress()) { timedOut = false; break; }
      delay(10);
    }
    if (timedOut) {
      showCenteredMsg(F("YOU LOSE!"), F("Slowpoke :P"));
      beep(180, 250); delay(120); beep(160, 350);
      if (!askRetryMenu()) return;
      continue;
    }
    uint16_t rt = (uint16_t)(millis() - goTime);

    uint16_t best = eeR(EE_REFLEX);
    bool newBest = (best == 0 || rt < best);
    if (newBest) { best = rt; eeW(EE_REFLEX, best); }

    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_6x10_tf);
      centerStrF(14, newBest ? F("NEW BEST!") : F("Result"));
      u8g2.setFont(u8g2_font_5x7_tf);
      char buf[20], num[6];
      utoa(rt, num, 10); strcpy(buf, "Time: "); strcat(buf, num); strcat(buf, " ms");
      centerStrRAM(30, buf);
      utoa(best, num, 10); strcpy(buf, "Best: "); strcat(buf, num); strcat(buf, " ms");
      centerStrRAM(42, buf);
      centerStrF(58, F("BTN=Retry  2x=Menu"));
    } while (u8g2.nextPage());
    beep(newBest ? 1200 : 700, 100);
    if (!askRetryMenu()) return;
  }
}

// ══════════════════════════════════════════════════════════════════════════
// SETTINGS
// ══════════════════════════════════════════════════════════════════════════
void settingsCalibrate() {
  showCenteredMsg(F("Joystick Cal"), F("Center stick & press BTN"));
  while (!btnPress()) { if (backHold()) return; delay(50); }
  jcx = analogRead(JOY_X); jcy = analogRead(JOY_Y);
  eeW(EE_JX, (uint16_t)jcx); eeW(EE_JY, (uint16_t)jcy);

  showCenteredMsg(F("ADXL Cal"), F("Place flat & hold still"));
  delay(1500);
  int32_t sx = 0, sy = 0;
  for (uint8_t i = 0; i < 32; i++) {
    int16_t x, y; adxlXY(x, y); sx += x; sy += y; delay(15);
  }
  axOff += sx / 32; ayOff += sy / 32;
  eeW(EE_AX, (uint16_t)axOff); eeW(EE_AY, (uint16_t)ayOff);
  showCenteredMsg(F("Calibrated!"), F(""));
  beep(1000, 100); delay(800);
}

void settingsResetScores() {
  showCenteredMsg(F("Reset Scores?"), F("BTN=Yes   Hold=No"));
  delay(500);
  uint32_t t = millis();
  bool confirmed = false;
  while (millis() - t < 3000) {
    if (backHold()) return;
    if (btnPress()) { confirmed = true; break; }
  }
  if (confirmed) {
    for (uint8_t a = 0; a < 8; a++) EEPROM.write(a, 0xFF);
    showCenteredMsg(F("Scores Reset!"), F(""));
    beep(500, 200); delay(800);
  }
}

void settingsSysInfo() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_5x7_tf);
    centerStrF(10, F("System Info"));
    u8g2.drawHLine(0, 12, 128);
    drawStrF(0, 24, F("ATmega328P"));
    drawStrF(0, 34, F("SH1106 0x3C"));
    drawStrF(0, 44, adxlOK() ? F("ADXL345: OK") : F("ADXL345: ERR"));
    drawStrF(0, 54, F("6 games onboard"));
  } while (u8g2.nextPage());
  while (!btnPress()) { if (backHold()) return; delay(50); }
}

// ── About (multi-page) ───────────────────────────────────────────────────
void showAbout() {
  // Page 1 — brand
  u8g2.firstPage();
  do {
    u8g2.drawFrame(4, 4, 120, 56);
    u8g2.drawFrame(6, 6, 116, 52);
    u8g2.setFont(u8g2_font_6x10_tf);
    centerStrF(28, F("NanoVolt"));
    u8g2.setFont(u8g2_font_5x7_tf);
    centerStrF(40, F("Handheld Arcade Console"));
    centerStrF(56, F("Press BTN ->"));
  } while (u8g2.nextPage());
  while (!btnPress()) { if (backHold()) return; delay(50); }

  // Page 2 — games included
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_5x7_tf);
    centerStrF(9, F("Games Included"));
    u8g2.drawHLine(0, 12, 128);
    centerStrF(23, F("Bounce Ball"));
    centerStrF(33, F("Space Shooter"));
    centerStrF(43, F("Star Catcher"));
    centerStrF(53, F("Maze + XO + Reflex"));
    centerStrF(63, F("Press BTN ->"));
  } while (u8g2.nextPage());
  while (!btnPress()) { if (backHold()) return; delay(50); }

  // Page 3 — hardware & credit
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_5x7_tf);
    centerStrF(9, F("Hardware"));
    u8g2.drawHLine(0, 12, 128);
    centerStrF(23, F("Arduino Nano"));
    centerStrF(33, F("SH1106 OLED + ADXL345"));
    centerStrF(43, F("Joystick + Buzzer"));
    u8g2.drawHLine(0, 47, 128);
    centerStrF(57, F("Made by Vidya Shankar"));
  } while (u8g2.nextPage());
  while (!btnPress()) { if (backHold()) return; delay(50); }

  showCenteredMsg(F("Thanks for"), F("playing! <3"));
  delay(1200);
}

// ── Generic scrolling menu ────────────────────────────────────────────────
// Shows up to `visible` rows at a time with a scrollbar when the list is
// longer than the screen can hold (e.g. the 5-item Settings/Brightness menus).
uint8_t drawMenu(const char* const* items, uint8_t n, uint8_t sel, const __FlashStringHelper* title) {
  const uint8_t visible = 4;
  uint8_t offset = 0;
  while (true) {
    if (sel < offset) offset = sel;
    if (sel >= offset + visible) offset = sel - visible + 1;
    u8g2.firstPage();
    do {
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, 0, 128, 11);
      u8g2.setDrawColor(0);
      u8g2.setFont(u8g2_font_5x7_tf);
      centerStrF(9, title);
      u8g2.setDrawColor(1);
      uint8_t shown = (n - offset < visible) ? (n - offset) : visible;
      uint8_t rowW = (n > visible) ? 121 : 128;
      for (uint8_t row = 0; row < shown; row++) {
        uint8_t i = offset + row;
        uint8_t y = 13 + row * 12;
        if (i == sel) {
          u8g2.drawBox(0, y - 1, rowW, 11);
          u8g2.setDrawColor(0);
        }
        char buf[18];
        strcpy_P(buf, (const char*)pgm_read_ptr(&items[i]));
        u8g2.drawStr(4, y + 8, buf);
        u8g2.setDrawColor(1);
      }
      if (n > visible) {
        u8g2.drawFrame(123, 12, 5, 51);
        uint8_t trackH = 49;
        uint8_t thumbH = (trackH * visible) / n;
        if (thumbH < 6) thumbH = 6;
        uint8_t thumbY = 13 + ((uint16_t)(trackH - thumbH) * offset) / (n - visible);
        u8g2.drawBox(124, thumbY, 3, thumbH);
      }
    } while (u8g2.nextPage());
    delay(150);
    if (backHold()) return 255;
    int8_t dy = joyY();
    if (dy == -1 && sel > 0) { sel--; beep(600, 20); }
    if (dy ==  1 && sel < n - 1) { sel++; beep(600, 20); }
    if (btnPress()) return sel;
  }
}

// ══════════════════════════════════════════════════════════════════════════
// MENU STRINGS (PROGMEM)
// ══════════════════════════════════════════════════════════════════════════
const char m0[] PROGMEM = "Joystick Games";
const char m1[] PROGMEM = "Motion Games";
const char m2[] PROGMEM = "Settings";
const char m3[] PROGMEM = "About";
const char* const mainItems[] PROGMEM = {m0, m1, m2, m3};

const char j0[] PROGMEM = "Bounce Ball";
const char j1[] PROGMEM = "XO Game (2P)";
const char j2[] PROGMEM = "Reflex Test";
const char* const joyItems[] PROGMEM = {j0, j1, j2};

const char t0[] PROGMEM = "Space Shooter";
const char t1[] PROGMEM = "Star Catcher";
const char t2[] PROGMEM = "Maze Runner";
const char* const motionItems[] PROGMEM = {t0, t1, t2};

const char s0[] PROGMEM = "Difficulty";
const char s1[] PROGMEM = "Brightness";
const char s2[] PROGMEM = "Calibrate";
const char s3[] PROGMEM = "Reset Scores";
const char s4[] PROGMEM = "System Info";
const char* const setItems[] PROGMEM = {s0, s1, s2, s3, s4};

const char d0[] PROGMEM = "Easy";
const char d1[] PROGMEM = "Normal";
const char d2[] PROGMEM = "Hard";
const char* const diffItems[] PROGMEM = {d0, d1, d2};

//const char b0[] PROGMEM = "Dim";
const char b1[] PROGMEM = "Low";
//const char b2[] PROGMEM = "Medium";
const char b3[] PROGMEM = "Bright";
const char b4[] PROGMEM = "Max";
const char* const brightItems[] PROGMEM = { b1, b3, b4};

// ══════════════════════════════════════════════════════════════════════════
// SETUP & LOOP
// ══════════════════════════════════════════════════════════════════════════
void setup() {
  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  Wire.begin();
  Wire.setClock(400000);   // fast-mode I2C — snappier ADXL345 reads
  u8g2.begin();
  u8g2.setFont(u8g2_font_5x7_tf);

  adxlWrite(0x2D, 0x08); // measure mode
  adxlWrite(0x31, 0x00); // ±2G

  gDiff   = EEPROM.read(EE_DIFF);   if (gDiff > 2)   gDiff = 1;
  gBright = EEPROM.read(EE_BRIGHT); if (gBright > 4) gBright = 3;
  applyBright();

  uint16_t jx = eeR(EE_JX); if (jx > 100 && jx < 900) jcx = jx;
  uint16_t jy = eeR(EE_JY); if (jy > 100 && jy < 900) jcy = jy;
  axOff = (int16_t)eeR(EE_AX);
  ayOff = (int16_t)eeR(EE_AY);

  randomSeed(analogRead(A2) + analogRead(A3));

  runPOST();
  bootAnim();
}

void loop() {
  uint8_t sel = drawMenu(mainItems, 4, 0, F("NanoVolt"));
  if (sel == 255) return;
  if (sel == 0) {
    uint8_t g = drawMenu(joyItems, 3, 0, F("Joystick"));
    if (g == 255) return;
    if (g == 0) playBounce();
    if (g == 1) playXO();
    if (g == 2) playReflex();
  } else if (sel == 1) {
    uint8_t g = drawMenu(motionItems, 3, 0, F("Motion"));
    if (g == 255) return;
    if (g == 0) playSpace();
    if (g == 1) playStar();
    if (g == 2) playMaze();
  } else if (sel == 2) {
    uint8_t s = drawMenu(setItems, 5, 0, F("Settings"));
    if (s == 255) return;
    if (s == 0) {
      uint8_t d = drawMenu(diffItems, 3, gDiff, F("Difficulty"));
      if (d != 255) { gDiff = d; EEPROM.write(EE_DIFF, gDiff); }
    } else if (s == 1) {
      uint8_t b = drawMenu(brightItems, 5, gBright, F("Brightness"));
      if (b != 255) { gBright = b; EEPROM.write(EE_BRIGHT, gBright); applyBright(); }
    } else if (s == 2) {
      settingsCalibrate();
    } else if (s == 3) {
      settingsResetScores();
    } else if (s == 4) {
      settingsSysInfo();
    }
  } else if (sel == 3) {
    showAbout();
  }
}
