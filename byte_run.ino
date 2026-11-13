/**
 * ============================================================================
 * BYTE RUN — Endless Cyber-Runner for ESP32-C3 SuperMini & 128x64 OLED
 * ============================================================================
 *
 * MIT License
 *
 * Copyright (c) 2026 Frank Jennings
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ============================================================================
 * ABOUT THE GAME & ARCHITECTURE
 * ============================================================================
 * BYTE RUN is a fast-paced, 60 FPS arcade endless runner engineered specifically
 * for the ESP32-C3 SuperMini RISC-V microcontroller and a monochrome 128x64
 * SSD1306 I2C OLED display. You play as BYTE, a rogue data bot escaping a
 * corrupted mainframe across three distinct environments.
 *
 * KEY GAMEPLAY & ENGINE FEATURES:
 * 1. Three Environments & Difficulty Tiers:
 *    - Maps: NEON CITY (standard), DATA VOID (aerial drones & double mines),
 *      and CORE LAB (low-gravity physics).
 *    - Modes: CHILL (1.0x), ARCADE (1.5x), and HYPER (2.0x speed & score).
 * 2. BYTE Garage, Skins & Economy:
 *    - Collect Orbs during runs to bank currency and brew Phase Potions
 *      (20 Orbs each, max 3 held; auto-triggers 2.5s immunity on fatal hit).
 *    - Six unlockable chassis skins with unique passive perks:
 *      * CLASSIC  : Standard frame (Free)
 *      * VISOR    : Starts each run with an active Energy Shield (Free)
 *      * JET-WING : Reduced gravity for extended mid-air glide (Free)
 *      * NINJA    : 35% passive dodge/evade chance (Unlock: 15 total slides)
 *      * CROWN    : Starting Shield + 2x Orb yield (Unlock: 500 pts in HYPER)
 *      * PHANTOM  : 50% passive evade + ghost trail (Unlock: 50 lifetime orbs)
 * 3. Escalating Multi-Wave "Firewall Warden" Bosses:
 *    - Every 500 points, normal obstacles pause for a 14-second boss duel.
 *    - Wave 1 (500 pts)  : Single telegraphed low/high bolts (Jump vs. Slide).
 *    - Wave 2 (1000 pts) : Adds mid-aim lane feints & opposite-lane double-taps.
 *    - Wave 3+ (1500+)   : Adds vertically oscillating sine-wave plasma bolts.
 * 4. Responsive Platformer Physics:
 *    - Implements 85ms Coyote Time, 95ms Jump Buffering, variable jump height,
 *      double-jumping, ground sliding, and mid-air fast-dropping.
 * 5. Non-Blocking Chiptune Sequencer:
 *    - Driven by a millis()-based ring buffer supporting smooth frequency sweeps,
 *      arpeggios, and pseudo-polyphonic basslines without halting game frames.
 * 6. Hardware, Flash & Power Protections:
 *    - I2C overclocked to 600 kHz for tear-free ~60 FPS rendering.
 *    - Batch NVS flash commits prevent EEPROM/flash sector wear during runs.
 *    - On-demand Wi-Fi Access Point ("BYTE-RUN-ARCADE" at 192.168.4.1) serves a
 *      mobile telemetry dashboard; radio remains completely off during play.
 *    - Auto Deep-Sleep after 2 minutes of menu inactivity (~45 uA draw) with
 *      SSD1306 panel power-down to prevent OLED burn-in (wake via Right button).
 *
 * HARDWARE WIRING (ESP32-C3 SuperMini):
 * - OLED SDA         -> GPIO 8
 * - OLED SCL         -> GPIO 9
 * - Right Button [R] -> GPIO 3 (Active LOW, INPUT_PULLUP, RTC Wakeup)
 * - Left Button  [L] -> GPIO 5 (Active LOW, INPUT_PULLUP)
 * - Top Button   [T] -> GPIO 7 (Active LOW, INPUT_PULLUP)
 * - Passive Buzzer   -> GPIO 4 (Recommended: 100 Ohm series resistor to pin)
 * ============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_sleep.h"

#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/Org_01.h>

// ================= DISPLAY & HARDWARE PIN DEFINITIONS =================
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C

// ESP32-C3 SuperMini GPIO assignments
// Note: GPIO 3 & 5 belong to the RTC power domain and support deep-sleep wakeup
#define PIN_SDA       8
#define PIN_SCL       9
#define PIN_BTN_RIGHT 3  // Right: Jump / Double-Jump / Confirm / Brew / Scroll Down
#define PIN_BTN_LEFT  5  // Left: Slide / Fast-Drop / Cycle / Scroll Up
#define PIN_BTN_TOP   7  // Top: Pause / Back / Mute Toggle
#define PIN_BUZZER    4  // Passive Piezo Buzzer (+)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences prefs;
WebServer server(80);

// ================= GAME STATE MACHINE & NAVIGATION =================
enum GameState { TITLE, MENU, GARAGE, RECORDS, STORY, WIFI_SYNC, PLAYING, PAUSED, GAMEOVER };
GameState state = TITLE;

int menuIndex   = 0; // 0=PLAY, 1=MAP, 2=MODE, 3=GARAGE, 4=TOP SCORES, 5=STORY, 6=WIFI STATS
int storyLine   = 0; // Top visible line index in the scrollable story viewer
int recordsPage = 0; // 0 = High Scores by Difficulty, 1 = Lifetime Career Stats

// ================= ENVIRONMENTS & DIFFICULTY SCALING =================
int currentMap = 0;
const char* mapNames[] = {"NEON CITY", "DATA VOID", "CORE LAB"};

enum Difficulty { DIFF_CHILL = 0, DIFF_ARCADE = 1, DIFF_HYPER = 2 };
int currentDiff = DIFF_ARCADE;
const char* diffNames[]     = {"CHILL", "ARCADE", "HYPER"};
const float diffBaseSpeed[] = {2.7, 3.3, 4.2};
const float diffAccel[]     = {0.005, 0.008, 0.012};
const float diffScoreMult[] = {1.0, 1.5, 2.0};
int highScores[3]           = {0, 0, 0};

// ================= GARAGE, SKINS & ECONOMY =================
int currentSkin  = 0; // Currently previewed skin in Garage
int equippedSkin = 0; // Active skin used during runs
const char* skinNames[] = {"CLASSIC", "VISOR", "JET-WING", "NINJA", "CROWN", "PHANTOM"};
const char* skinPerks[] = {"NO ARMOR", "START SHIELD", "GLIDE FRAME", "35% EVADE", "SHIELD+ORBS", "50% EVADE"};
const char* skinReqs[]  = {"FREE", "FREE", "FREE", "15 SLIDES", "500 HYPER", "50 ORBS"};
bool skinUnlocked[6]    = {true, true, true, false, false, false};

// Lifetime player telemetry & consumable inventory
int totalSlides    = 0;
int totalOrbs      = 0;
int bossesDefeated = 0;
int orbBank        = 0;  // Spendable currency
int phasePotions   = 0;  // Auto-activates 2.5s immunity on lethal impact (Max 3)
const int POTION_COST = 20;
bool statsDirty    = false; // Tracks unsaved RAM changes to minimize NVS flash writes

bool soundEnabled = true;

// ================= IN-GAME LORE & INSTRUCTIONS =================
// Formatted strictly <= 21 characters per line to fit the 128px OLED width cleanly
const int TOTAL_STORY_LINES = 22;
const char* storyLines[TOTAL_STORY_LINES] = {
  "-- MISSION BRIEF --",
  "YOU ARE BYTE, A ROGUE",
  "DATA BOT ESCAPING THE",
  "CORRUPTED MAINFRAME.",
  "",
  "-- ARMOR & POTIONS --",
  "1. EACH SKIN HAS A",
  "   BUILT-IN PERK.",
  "2. SPEND 20 ORBS IN",
  "   GARAGE FOR POTIONS.",
  "3. POTIONS GRANT 2.5S",
  "   IMMUNITY ON HIT!",
  "",
  "-- PRO CONTROLS --",
  "JUMP BUFFER & COYOTE",
  "TIMING ARE ACTIVE.",
  "",
  "-- WARDEN BOSSES --",
  "ATTACKS EVOLVE EVERY",
  "500 PTS: FEINTS,",
  "DOUBLE-TAPS & WAVES!",
  "GOOD LUCK, BYTE!"
};

// ================= PLAYER PHYSICS & HITBOX CONSTANTS =================
const int GROUND_Y = 55;
const int PLAYER_X = 18;
const int PLAYER_W = 10;
const int NORMAL_H = 10;
const int SLIDE_H  = 5;

int playerH   = NORMAL_H;
float playerY = GROUND_Y - NORMAL_H;
float velocityY = 0;
const float JUMP_FORCE        = -7.2;
const float DOUBLE_JUMP_FORCE = -6.4;
int jumpsRemaining = 2;
bool isSliding     = false;
bool hasShield     = false;
unsigned long immuneUntil = 0;

// Forgiving platformer feel: Coyote Time (late jump) & Jump Buffering (early jump)
unsigned long lastGroundedTime = 0;
unsigned long jumpBufferTime   = 0;
const unsigned long COYOTE_MS      = 85;
const unsigned long JUMP_BUFFER_MS = 95;

// Circular buffer storing recent player coordinates for motion afterimages
struct TrailPoint {
  int x, y;
  bool sliding;
  bool active;
};
TrailPoint trail[4];
unsigned long lastTrailSample = 0;

// ================= INPUT DEBOUNCE, AUTO-REPEAT & IDLE SLEEP =================
bool prevRightState = false;
bool prevLeftState  = false;
bool prevTopState   = false;
unsigned long rightPressStart = 0;
unsigned long leftPressStart  = 0;
unsigned long lastRepeatTick  = 0;
bool rightHandledLong = false;
unsigned long lastUserActivity = 0;
const unsigned long IDLE_SLEEP_MS = 120000; // 2 minutes idle triggers Deep Sleep

// ================= PARTICLES, POPUPS, OBSTACLES & PICKUPS =================
struct Particle {
  float x, y, vx, vy;
  int life;
};
Particle particles[14];

struct Popup {
  float x, y;
  char text[14];
  int life;
};
Popup popup = {0, 0, "", 0};

float obsX      = 140;
int obsType     = 0;
float gameSpeed = 3.3;

struct Pickup {
  float x;
  int y;
  int type; // 0=Inactive, 1=Orb, 2=Energy Shield
  int noteIdx;
};
Pickup pickups[3];
int combo = 1;
unsigned long comboTimer = 0;

// ================= ESCALATING BOSS BATTLE ("FIREWALL WARDEN") =================
struct BossBolt {
  float x;
  int baseY;
  int y;
  int pattern; // 0=Straight horizontal, 1=Vertical sine-wave oscillation
  bool active;
};

bool bossActive = false;
int bossWave = 1;
int nextBossScore = 500;
unsigned long bossStartTime = 0;
const unsigned long BOSS_DURATION = 14000;
float bossY = 20;
int bossState = 0;       // 0=Aiming (laser telegraph), 1=Firing
int bossPattern = 0;     // 0=Single, 1=Feint Switch, 2=Double-Tap, 3=Sine-Wave
int bossLane = 0;        // 0=Low Bolt (requires Jump), 1=High Bolt (requires Slide)
bool bossFeinted = false;
unsigned long bossActionTimer = 0;
BossBolt bolts[2];

// ================= SCORING & SCREEN JUICE =================
float rawScore = 0;
int score = 0;
unsigned long lastScoreTick   = 0;
unsigned long stateChangeTime = 0;
int shakeFrames = 0;
int flashFrames = 0; // Non-blocking single-frame display inversion on impact

// ================= NON-BLOCKING CHIPTUNE AUDIO ENGINE =================
struct NoteStep {
  int freq;
  int endFreq; // When endFreq != freq, interpolates a smooth pitch slide
  int duration;
};

#define MAX_SFX_QUEUE 8
NoteStep sfxQueue[MAX_SFX_QUEUE];
int sfxHead = 0;
int sfxTail = 0;
NoteStep activeNote = {0, 0, 0};
unsigned long activeNoteStart = 0;
bool sfxPlaying = false;

int bassStep = 0;
unsigned long lastBassTick = 0;
bool harmonyToggle = false; // Alternates octaves on off-beats for pseudo-polyphony
const int bassNormal[]  = {110, 110, 146, 110, 164, 146, 130, 98};
const int bassShield[]  = {220, 277, 330, 440, 330, 277, 330, 440};
const int bassBoss[]    = {98, 98, 104, 98, 130, 98, 104, 87};
const int orbArpeggio[] = {523, 659, 784, 1046};

// ================= 1-BIT PIXEL ART SPRITE BITMAPS (PROGMEM) =================
const unsigned char PROGMEM byteRun1[] = {
  0b00111100, 0b00000000,
  0b01111110, 0b00000000,
  0b01011010, 0b00000000,
  0b01111110, 0b00000000,
  0b00111100, 0b00000000,
  0b01111110, 0b00000000,
  0b10111101, 0b00000000,
  0b00111100, 0b00000000,
  0b01000010, 0b00000000,
  0b11000011, 0b00000000
};

const unsigned char PROGMEM byteRun2[] = {
  0b00111100, 0b00000000,
  0b01111110, 0b00000000,
  0b01011010, 0b00000000,
  0b01111110, 0b00000000,
  0b00111100, 0b00000000,
  0b01111110, 0b00000000,
  0b00111100, 0b00000000,
  0b00111100, 0b00000000,
  0b00100100, 0b00000000,
  0b00110110, 0b00000000
};

const unsigned char PROGMEM byteJump[] = {
  0b00111100, 0b00000000,
  0b01111110, 0b00000000,
  0b01011010, 0b00000000,
  0b01111110, 0b00000000,
  0b10111101, 0b00000000,
  0b01111110, 0b00000000,
  0b00111100, 0b00000000,
  0b01100110, 0b00000000,
  0b01000010, 0b00000000,
  0b00000000, 0b00000000
};

const unsigned char PROGMEM byteSlide[] = {
  0b00001111, 0b00000000,
  0b00111101, 0b00000000,
  0b11111111, 0b11000000,
  0b01111111, 0b11100000,
  0b00111001, 0b11000000
};

const unsigned char PROGMEM mineBmp[] = {
  0b00001000, 0b00000000,
  0b00011100, 0b00000000,
  0b00011100, 0b00000000,
  0b00110110, 0b00000000,
  0b01101011, 0b00000000,
  0b01111111, 0b00000000,
  0b11011101, 0b10000000,
  0b11111111, 0b10000000,
  0b01111111, 0b00000000,
  0b11111111, 0b10000000
};

const unsigned char PROGMEM droneBmpA[] = {
  0b11111000, 0b01111100,
  0b00010000, 0b00100000,
  0b00111111, 0b11110000,
  0b01101101, 0b10111000,
  0b11111111, 0b11111100,
  0b00111000, 0b01110000,
  0b00010000, 0b00100000
};

const unsigned char PROGMEM droneBmpB[] = {
  0b00111000, 0b01110000,
  0b00010000, 0b00100000,
  0b00111111, 0b11110000,
  0b01111011, 0b01111000,
  0b11111111, 0b11111100,
  0b00111000, 0b01110000,
  0b00010000, 0b00100000
};

const unsigned char PROGMEM wardenBmp[] = {
  0b00111111, 0b11111100,
  0b01111111, 0b11111110,
  0b11001111, 0b11110011,
  0b11011011, 0b11011011,
  0b11111111, 0b11111111,
  0b01111000, 0b00011110,
  0b00111111, 0b11111100,
  0b01101111, 0b11110110,
  0b11001100, 0b00110011,
  0b11001111, 0b11110011,
  0b11111111, 0b11111111,
  0b01111100, 0b00111110,
  0b00111000, 0b00011100,
  0b01101100, 0b00110110,
  0b11000110, 0b01100011,
  0b10000011, 0b11000001,
  0b00000001, 0b10000000,
  0b00000000, 0b00000000
};

// ================= NON-BLOCKING AUDIO SEQUENCER =================
void clearSfxQueue() {
  sfxHead = sfxTail = 0;
  sfxPlaying = false;
  noTone(PIN_BUZZER);
}

// Enqueues a frequency sweep (f1 -> f2 over dur ms) without blocking loop()
void enqueueSweep(int f1, int f2, int dur) {
  if (!soundEnabled) return;
  int next = (sfxTail + 1) % MAX_SFX_QUEUE;
  if (next != sfxHead) {
    sfxQueue[sfxTail] = {f1, f2, dur};
    sfxTail = next;
  }
}

void playTone(int freq, int duration) {
  enqueueSweep(freq, freq, duration);
}

// Polled every frame in loop() to advance queued sound effects and pitch sweeps
void updateAudioEngine() {
  if (!soundEnabled) {
    if (sfxPlaying) clearSfxQueue();
    return;
  }

  unsigned long now = millis();

  if (sfxPlaying) {
    unsigned long elapsed = now - activeNoteStart;
    if (elapsed >= (unsigned long)activeNote.duration) {
      sfxPlaying = false;
      noTone(PIN_BUZZER);
    } else if (activeNote.freq != activeNote.endFreq) {
      int curFreq = map(elapsed, 0, activeNote.duration, activeNote.freq, activeNote.endFreq);
      if (curFreq > 20) tone(PIN_BUZZER, curFreq);
    }
  }

  if (!sfxPlaying && sfxHead != sfxTail) {
    activeNote = sfxQueue[sfxHead];
    sfxHead = (sfxHead + 1) % MAX_SFX_QUEUE;
    activeNoteStart = now;
    sfxPlaying = true;
    if (activeNote.freq > 20) tone(PIN_BUZZER, activeNote.freq);
    else noTone(PIN_BUZZER);
  }
}

void playPauseSound(bool pausing) {
  clearSfxQueue();
  if (pausing) { enqueueSweep(660, 660, 45); enqueueSweep(440, 440, 60); }
  else         { enqueueSweep(440, 440, 45); enqueueSweep(660, 660, 60); }
}

void playArpeggioNote(int idx) {
  int base = orbArpeggio[idx % 4];
  enqueueSweep(base, (int)(base * 1.12), 55);
}

void playBossFanfare(bool entering) {
  clearSfxQueue();
  if (entering) {
    enqueueSweep(220, 185, 90);
    enqueueSweep(196, 164, 110);
    enqueueSweep(174, 130, 140);
  } else {
    enqueueSweep(523, 523, 55);
    enqueueSweep(659, 659, 55);
    enqueueSweep(784, 784, 55);
    enqueueSweep(1046, 1318, 120);
  }
}

void playShieldUpSound() {
  enqueueSweep(523, 1046, 130);
}

void playPotionSound() {
  clearSfxQueue();
  enqueueSweep(440, 880, 85);
  enqueueSweep(659, 1318, 110);
}

void playShieldBreakSound() {
  clearSfxQueue();
  enqueueSweep(640, 190, 140);
}

void playCrashSound() {
  clearSfxQueue();
  enqueueSweep(320, 75, 280);
}

void toggleSound() {
  soundEnabled = !soundEnabled;
  prefs.putBool("sound", soundEnabled);
  if (soundEnabled) enqueueSweep(660, 990, 70);
  else clearSfxQueue();
}

// ================= NVS FLASH WEAR PROTECTION =================
// Commits accumulated run statistics to flash only when dirty and outside active frames
void commitStatsToFlash() {
  if (!statsDirty) return;
  prefs.putInt("slides", totalSlides);
  prefs.putInt("orbs", totalOrbs);
  prefs.putInt("bank", orbBank);
  prefs.putInt("bosses", bossesDefeated);
  prefs.putBool("sk3", skinUnlocked[3]);
  prefs.putBool("sk4", skinUnlocked[4]);
  prefs.putBool("sk5", skinUnlocked[5]);
  statsDirty = false;
}

// ================= ON-DEMAND WI-FI TELEMETRY PORTAL =================
void handleWebRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{background:#0b0e14;color:#00ffcc;font-family:monospace;padding:18px;max-width:420px;margin:auto;}";
  html += ".card{border:1px solid #00ffcc;padding:12px;margin:10px 0;border-radius:6px;background:#111622;}";
  html += "h1{margin:0 0 8px 0;font-size:22px;} .row{display:flex;justify-content:space-between;padding:4px 0;}";
  html += "</style></head><body><h1>// BYTE RUN TELEMETRY</h1>";
  html += "<div class='card'><b>HIGH SCORES</b>";
  html += "<div class='row'><span>CHILL:</span><span>" + String(highScores[0]) + "</span></div>";
  html += "<div class='row'><span>ARCADE:</span><span>" + String(highScores[1]) + "</span></div>";
  html += "<div class='row'><span>HYPER:</span><span>" + String(highScores[2]) + "</span></div></div>";
  html += "<div class='card'><b>LIFETIME STATS</b>";
  html += "<div class='row'><span>ORB BANK:</span><span>" + String(orbBank) + "</span></div>";
  html += "<div class='row'><span>TOTAL ORBS:</span><span>" + String(totalOrbs) + "</span></div>";
  html += "<div class='row'><span>TOTAL SLIDES:</span><span>" + String(totalSlides) + "</span></div>";
  html += "<div class='row'><span>WARDENS BEATEN:</span><span>" + String(bossesDefeated) + "</span></div>";
  html += "<div class='row'><span>EQUIPPED SKIN:</span><span>" + String(skinNames[equippedSkin]) + "</span></div></div>";
  html += "<p style='font-size:12px;color:#7a8b99;'>Press [T] on your ESP32-C3 console to shut down Wi-Fi and save battery.</p></body></html>";
  server.send(200, "text/html", html);
}

void startWifiCompanion() {
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_11dBm); // Capped TX power avoids USB brownouts
  WiFi.softAP("BYTE-RUN-ARCADE");
  server.on("/", handleWebRoot);
  server.begin();
}

void stopWifiCompanion() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

// ================= DEEP SLEEP POWER & OLED BURN-IN PROTECTION =================
void enterDeepSleep() {
  clearSfxQueue();
  commitStatsToFlash();
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF); // Shut down OLED charge pump

  // Configure Right Button (GPIO 3) as active-LOW RTC wakeup source
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BTN_RIGHT, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

// ================= UI, PARTICLES & SKIN PROGRESSION HELPERS =================
void drawCenteredText(const char* text, int y, int offsetX) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  display.setCursor(((SCREEN_WIDTH - w) / 2) - x1 + offsetX, y);
  display.print(text);
}

void drawCenteredText(const char* text, int y) {
  drawCenteredText(text, y, 0);
}

void spawnParticles(float x, float y, int count) {
  for (int i = 0; i < 14 && count > 0; i++) {
    if (particles[i].life <= 0) {
      particles[i].x = x;
      particles[i].y = y;
      particles[i].vx = random(-28, 14) / 10.0;
      particles[i].vy = random(-22, 18) / 10.0;
      particles[i].life = random(6, 13);
      count--;
    }
  }
}

void updateAndDrawParticles(int ox, int oy) {
  for (int i = 0; i < 14; i++) {
    if (particles[i].life > 0) {
      particles[i].x += particles[i].vx;
      particles[i].y += particles[i].vy;
      particles[i].life--;
      display.drawPixel((int)particles[i].x + ox, (int)particles[i].y + oy, SSD1306_WHITE);
    }
  }
}

void triggerPopup(float x, float y, const char* msg) {
  popup.x = constrain((int)x, 4, 82);
  popup.y = constrain((int)y, 14, 48);
  strncpy(popup.text, msg, 13);
  popup.text[13] = '\0';
  popup.life = 20;
}

void checkSkinUnlocks() {
  if (!skinUnlocked[3] && totalSlides >= 15) {
    skinUnlocked[3] = true;
    statsDirty = true;
    triggerPopup(24, 22, "UNLOCK:NINJA");
    playShieldUpSound();
  }
  if (!skinUnlocked[4] && currentDiff == DIFF_HYPER && score >= 500) {
    skinUnlocked[4] = true;
    statsDirty = true;
    triggerPopup(24, 22, "UNLOCK:CROWN");
    playShieldUpSound();
  }
  if (!skinUnlocked[5] && totalOrbs >= 50) {
    skinUnlocked[5] = true;
    statsDirty = true;
    triggerPopup(20, 22, "UNLOCK:PHANTM");
    playShieldUpSound();
  }
}

// Evaluates defensive layers on collision: Immunity -> Shield -> Skin Evade -> Phase Potion
bool absorbOrSurviveHit(float hitX, float hitY) {
  if (millis() < immuneUntil) {
    spawnParticles(hitX, hitY, 6);
    return true;
  }

  if (hasShield) {
    hasShield = false;
    playShieldBreakSound();
    spawnParticles(hitX, hitY, 12);
    triggerPopup(PLAYER_X, playerY - 6, "BLOCKED!");
    shakeFrames = 5;
    flashFrames = 1;
    return true;
  }

  int evadeChance = 0;
  if (equippedSkin == 3) evadeChance = 35;      // NINJA perk
  else if (equippedSkin == 5) evadeChance = 50; // PHANTOM perk

  if (evadeChance > 0 && random(0, 100) < evadeChance) {
    immuneUntil = millis() + 900;
    enqueueSweep(780, 1180, 65);
    spawnParticles(PLAYER_X + 5, playerY + 5, 10);
    triggerPopup(PLAYER_X, playerY - 6, "EVADED!");
    return true;
  }

  if (phasePotions > 0) {
    phasePotions--;
    prefs.putInt("potions", phasePotions);
    immuneUntil = millis() + 2500;
    playPotionSound();
    spawnParticles(PLAYER_X + 5, playerY + 5, 14);
    triggerPopup(PLAYER_X, playerY - 6, "IMMUNE 2.5S!");
    shakeFrames = 4;
    flashFrames = 1;
    return true;
  }

  return false;
}

void executeJump(bool isFirstJump) {
  if (isSliding) {
    isSliding = false;
    playerH = NORMAL_H;
  }
  if (isFirstJump) {
    velocityY = JUMP_FORCE;
    enqueueSweep(360, 620, 50);
    spawnParticles(PLAYER_X + 2, GROUND_Y - 1, 4);
    jumpsRemaining = 1;
  } else {
    velocityY = DOUBLE_JUMP_FORCE;
    enqueueSweep(540, 880, 60);
    spawnParticles(PLAYER_X + 4, playerY + playerH, 6);
    jumpsRemaining = 0;
  }
  jumpBufferTime = 0;
  lastGroundedTime = 0;
}

// ================= MULTI-WAVE BOSS ATTACK GENERATOR =================
void setupNextBossSalvo() {
  bossState = 0;
  bossFeinted = false;
  bossLane = random(0, 2);
  bossActionTimer = millis();

  // Escalate attack complexity based on current Boss Wave
  if (bossWave == 1) {
    bossPattern = 0; // Wave 1: Straight single bolts
  } else if (bossWave == 2) {
    bossPattern = random(1, 3); // Wave 2: Feint lane switch or Double-Tap combo
  } else {
    bossPattern = random(1, 4); // Wave 3+: Adds Sine-Wave plasma bolts
  }
}

void fireBossSalvo() {
  bossState = 1;
  int lane0Y = GROUND_Y - 5;  // Low bolt -> requires Jump
  int lane1Y = GROUND_Y - 10; // High bolt -> requires Slide

  bolts[0].active = true;
  bolts[0].x = 102;
  bolts[0].baseY = (bossLane == 0) ? lane0Y : lane1Y;
  bolts[0].y = bolts[0].baseY;
  bolts[0].pattern = (bossPattern == 3) ? 1 : 0;

  if (bossPattern == 2) {
    // Double-Tap: Second bolt trails 48px behind in the opposite lane
    bolts[1].active = true;
    bolts[1].x = 150;
    bolts[1].baseY = (bossLane == 0) ? lane1Y : lane0Y;
    bolts[1].y = bolts[1].baseY;
    bolts[1].pattern = 0;
    enqueueSweep(460, 240, 80);
  } else {
    bolts[1].active = false;
    enqueueSweep(420, 260, 55);
  }

  // Spawn a risk/reward Orb in the safe dodge lane
  if (pickups[0].type == 0) {
    pickups[0].type = 1;
    pickups[0].x = 106;
    pickups[0].noteIdx = random(0, 4);
    pickups[0].y = (bossLane == 0) ? 26 : (GROUND_Y - 4);
  }
}

void resetGame() {
  playerH = NORMAL_H;
  playerY = GROUND_Y - playerH;
  velocityY = 0;
  jumpsRemaining = 2;
  isSliding = false;
  immuneUntil = 0;
  lastGroundedTime = millis();
  jumpBufferTime = 0;

  // VISOR (1) & CROWN (4) skins grant a starting Shield
  hasShield = (equippedSkin == 1 || equippedSkin == 4);

  for (int i = 0; i < 4; i++) trail[i].active = false;
  obsX = 140;
  obsType = 0;
  for (int i = 0; i < 3; i++) pickups[i].type = 0;
  bossActive = false;
  bossWave = 1;
  nextBossScore = 500;
  bolts[0].active = false;
  bolts[1].active = false;
  gameSpeed = diffBaseSpeed[currentDiff];
  rawScore = 0;
  score = 0;
  combo = 1;
  shakeFrames = 0;
  flashFrames = 0;
  popup.life = 0;
  lastScoreTick = millis();
  for (int i = 0; i < 14; i++) particles[i].life = 0;
}

void spawnObstacleAndPickups() {
  obsX = SCREEN_WIDTH + random(14, 34);

  // Select obstacle pools tailored to map theme and difficulty progression
  if (currentMap == 1) {
    int voidPool[] = {2, 3, 4, 2, 0};
    obsType = (score < 80) ? random(0, 3) : voidPool[random(0, 5)];
  } else if (currentMap == 2) {
    int labPool[] = {1, 2, 4, 1, 0};
    obsType = (score < 80) ? random(0, 2) : labPool[random(0, 5)];
  } else {
    if (currentDiff == DIFF_CHILL) obsType = (score < 120) ? 0 : random(0, 3);
    else if (currentDiff == DIFF_ARCADE) {
      if (score < 100) obsType = random(0, 2);
      else if (score < 260) obsType = random(0, 3);
      else obsType = random(0, 5);
    } else obsType = random(0, 5);
  }

  bool anyActive = false;
  for (int i = 0; i < 3; i++) if (pickups[i].type > 0) anyActive = true;

  if (!anyActive) {
    int roll = random(0, 100);
    if (!hasShield && roll < 14 && score > 70) {
      pickups[0].type = 2; // Energy Shield pickup
      pickups[0].x = obsX + random(14, 28);
      pickups[0].y = random(20, 36);
    } else if (roll < 72) {
      // Spawn 3-Orb rhythm arc guiding player over/under the obstacle
      int baseX = obsX + 10;
      for (int i = 0; i < 3; i++) {
        pickups[i].type = 1;
        pickups[i].x = baseX + (i * 12);
        pickups[i].noteIdx = i;
        if (obsType == 2) pickups[i].y = GROUND_Y - 4;
        else pickups[i].y = (i == 1) ? 22 : 31;
      }
    }
  }
}

// ================= RENDERING PIPELINE =================
void drawMotionTrails(int ox, int oy) {
  if (state != PLAYING) return;
  bool showTrail = (equippedSkin == 5) || (jumpsRemaining < 2) || (gameSpeed > 5.5);
  if (!showTrail) return;

  for (int i = 0; i < 4; i++) {
    if (trail[i].active) {
      int tx = trail[i].x - ((4 - i) * 3) + ox;
      int ty = trail[i].y + oy;
      display.drawPixel(tx + 2, ty + 2, SSD1306_WHITE);
      display.drawPixel(tx + 6, ty + 3, SSD1306_WHITE);
      if (!trail[i].sliding) {
        display.drawPixel(tx + 4, ty + 6, SSD1306_WHITE);
      }
    }
  }
}

void drawByteWithSkin(int x, int y, int ox, int oy) {
  int px = x + ox;
  int py = y + oy;
  int skinToDraw = (state == GARAGE) ? currentSkin : equippedSkin;

  // Blinking aura ring while Phase Potion or Evade immunity is active
  if (state == PLAYING && millis() < immuneUntil && ((millis() / 60) % 2 == 0)) {
    display.drawCircle(px + 5, py + 5, 9, SSD1306_WHITE);
    return;
  }

  if (skinToDraw == 5) {
    // PHANTOM hollow wireframe chassis
    if (isSliding) {
      display.drawRoundRect(px, py, 12, 5, 2, SSD1306_WHITE);
      display.drawPixel(px + 9, py + 2, SSD1306_WHITE);
    } else {
      display.drawRect(px + 1, py, 8, 9, SSD1306_WHITE);
      display.drawPixel(px + 3, py + 2, SSD1306_WHITE);
      display.drawPixel(px + 6, py + 2, SSD1306_WHITE);
    }
  } else {
    if (isSliding) {
      display.drawBitmap(px, py, byteSlide, 12, 5, SSD1306_WHITE);
    } else if (jumpsRemaining < 2) {
      display.drawBitmap(px, py, byteJump, 10, 10, SSD1306_WHITE);
    } else {
      const unsigned char* runBmp = ((millis() / 90) % 2 == 0) ? byteRun1 : byteRun2;
      display.drawBitmap(px, py, runBmp, 10, 10, SSD1306_WHITE);
    }

    // Cosmetic skin overlays
    if (skinToDraw == 1 && !isSliding) {        // VISOR antenna & optical strip
      display.drawLine(px + 2, py + 2, px + 8, py + 2, SSD1306_WHITE);
      display.drawLine(px + 5, py - 1, px + 5, py - 3, SSD1306_WHITE);
    } else if (skinToDraw == 2 && !isSliding) { // JET-WING thrusters
      display.drawLine(px - 1, py + 4, px - 3, py + 2, SSD1306_WHITE);
      display.drawLine(px - 1, py + 6, px - 3, py + 7, SSD1306_WHITE);
    } else if (skinToDraw == 3) {               // NINJA animated scarf
      int wave = ((millis() / 80) % 2 == 0) ? 0 : 1;
      display.drawLine(px - 1, py + 3, px - 5, py + 2 + wave, SSD1306_WHITE);
      display.drawLine(px - 1, py + 4, px - 4, py + 4 - wave, SSD1306_WHITE);
    } else if (skinToDraw == 4) {               // CROWN floating royal diadem
      int bob = ((millis() / 150) % 2 == 0) ? 0 : 1;
      int cy = py - 4 - bob;
      display.drawLine(px + 2, cy + 2, px + 7, cy + 2, SSD1306_WHITE);
      display.drawPixel(px + 2, cy, SSD1306_WHITE);
      display.drawPixel(px + 4, cy, SSD1306_WHITE);
      display.drawPixel(px + 5, cy, SSD1306_WHITE);
      display.drawPixel(px + 7, cy, SSD1306_WHITE);
      display.drawPixel(px + 3, cy + 1, SSD1306_WHITE);
      display.drawPixel(px + 6, cy + 1, SSD1306_WHITE);
    }
  }

  if (hasShield) {
    if (isSliding) display.drawRoundRect(px - 2, py - 2, 16, 9, 3, SSD1306_WHITE);
    else display.drawCircle(px + 5, py + 5, 8, SSD1306_WHITE);
  }
}

void renderMapBackground(int ox, int oy) {
  if (currentMap == 0) {
    // NEON CITY: Twinkling stars, skyline silhouettes, and high-speed data road
    for (int s = 0; s < 8; s++) {
      int sx = ((s * 37) - ((int)(score * 0.1) % 37)) % SCREEN_WIDTH;
      if (sx < 0) sx += SCREEN_WIDTH;
      int sy = 11 + ((s * 13) % 18);
      if ((millis() / 300 + s) % 3 != 0) display.drawPixel(sx, sy, SSD1306_WHITE);
    }
    for (int b = 0; b < 6; b++) {
      int bx = ((b * 28) - ((int)(score * 0.25) % 28));
      int bh = 10 + ((b * 7) % 16);
      display.drawRect(bx + ox, GROUND_Y - bh + oy, 14, bh, SSD1306_WHITE);
    }
    display.fillRect(0, GROUND_Y - 9, SCREEN_WIDTH, 9, SSD1306_BLACK);

    display.drawLine(0, GROUND_Y + oy, SCREEN_WIDTH, GROUND_Y + oy, SSD1306_WHITE);
    for (int i = 0; i < SCREEN_WIDTH + 16; i += 16) {
      int dashX = (i - ((int)(score * 3.5) % 16));
      display.drawLine(dashX + ox, GROUND_Y + 3 + oy, dashX + 4 + ox, GROUND_Y + 3 + oy, SSD1306_WHITE);
      display.drawPixel(dashX + 9 + ox, GROUND_Y + 6 + oy, SSD1306_WHITE);
    }
  } else if (currentMap == 1) {
    // DATA VOID: Cyberspace perspective grid & segmented hover-track
    for (int gx = 0; gx < SCREEN_WIDTH + 24; gx += 24) {
      int lineX = gx - ((int)(score * 0.6) % 24);
      display.drawLine(lineX + ox, 12 + oy, lineX + ox, GROUND_Y - 6 + oy, SSD1306_WHITE);
    }
    display.drawLine(0, 26 + oy, SCREEN_WIDTH, 26 + oy, SSD1306_WHITE);
    display.drawLine(0, 40 + oy, SCREEN_WIDTH, 40 + oy, SSD1306_WHITE);
    for (int i = 0; i < SCREEN_WIDTH + 16; i += 16) {
      int segX = i - ((int)(score * 2.5) % 16);
      display.fillRect(segX + ox, GROUND_Y + oy, 12, 2, SSD1306_WHITE);
      display.drawPixel(segX + 5 + ox, GROUND_Y + 5 + oy, SSD1306_WHITE);
    }
  } else {
    // CORE LAB: Ceiling reactor conduits, support pillars & hazard catwalk
    display.drawLine(0, 11 + oy, SCREEN_WIDTH, 11 + oy, SSD1306_WHITE);
    for (int p = 0; p < SCREEN_WIDTH + 32; p += 32) {
      int px = p - ((int)(score * 0.5) % 32);
      display.drawRect(px + ox, 11 + oy, 6, 6, SSD1306_WHITE);
      display.drawLine(px + 3 + ox, 17 + oy, px + 3 + ox, GROUND_Y + oy, SSD1306_WHITE);
    }
    display.drawLine(0, GROUND_Y + oy, SCREEN_WIDTH, GROUND_Y + oy, SSD1306_WHITE);
    display.drawLine(0, GROUND_Y + 3 + oy, SCREEN_WIDTH, GROUND_Y + 3 + oy, SSD1306_WHITE);
    for (int i = 0; i < SCREEN_WIDTH + 12; i += 12) {
      int rx = i - ((int)(score * 2.5) % 12);
      display.drawLine(rx + ox, GROUND_Y + oy, rx - 3 + ox, GROUND_Y + 3 + oy, SSD1306_WHITE);
    }
  }
}

void renderGameplayScene(int ox, int oy) {
  display.clearDisplay();
  renderMapBackground(ox, oy);

  // Top Status HUD
  display.setFont(&Org_01);
  display.setTextSize(1);
  display.setCursor(2, 7);
  display.print(diffNames[currentDiff][0]);
  display.print(":");
  display.print(highScores[currentDiff]);

  if (bossActive) {
    unsigned long elapsed = millis() - bossStartTime;
    int remW = map(constrain(BOSS_DURATION - elapsed, 0, BOSS_DURATION), 0, BOSS_DURATION, 0, 22);
    display.setCursor(38, 7);
    display.print("W");
    display.print(bossWave);
    display.drawRect(49, 2, 24, 5, SSD1306_WHITE);
    display.fillRect(50, 3, remW, 3, SSD1306_WHITE);
  } else {
    if (combo > 1) {
      display.setCursor(46, 7);
      display.print(combo);
      display.print("X");
    }
    display.setCursor(60, 7);
    if (hasShield) display.print("S");
    if (phasePotions > 0) {
      display.print("P");
      display.print(phasePotions);
    }
  }

  display.setCursor(86, 7);
  display.print("SC:");
  display.print(score);

  drawMotionTrails(ox, oy);
  drawByteWithSkin(PLAYER_X, (int)playerY, ox, oy);
  updateAndDrawParticles(ox, oy);

  if (popup.life > 0) {
    display.setCursor((int)popup.x, (int)popup.y);
    display.print(popup.text);
    if (state == PLAYING) {
      popup.y -= 0.4;
      popup.life--;
    }
  }

  // Render Orbs & Shield Pickups
  for (int i = 0; i < 3; i++) {
    if (pickups[i].type == 1) {
      int r = ((millis() / 120 + i) % 2 == 0) ? 3 : 2;
      display.drawCircle((int)pickups[i].x + ox, pickups[i].y + oy, r, SSD1306_WHITE);
      display.drawPixel((int)pickups[i].x + ox, pickups[i].y + oy, SSD1306_WHITE);
    } else if (pickups[i].type == 2) {
      int px = (int)pickups[i].x + ox, py = pickups[i].y + oy;
      display.drawLine(px, py - 4, px + 4, py, SSD1306_WHITE);
      display.drawLine(px + 4, py, px, py + 4, SSD1306_WHITE);
      display.drawLine(px, py + 4, px - 4, py, SSD1306_WHITE);
      display.drawLine(px - 4, py, px, py - 4, SSD1306_WHITE);
      display.drawPixel(px, py, SSD1306_WHITE);
    }
  }

  // Render Boss or Standard Obstacles
  if (bossActive) {
    int bx = 106 + ox;
    int by = (int)bossY + oy;
    display.drawBitmap(bx, by, wardenBmp, 16, 18, SSD1306_WHITE);

    if (bossState == 0) {
      int aimY = (bossLane == 0) ? (GROUND_Y - 3) : (GROUND_Y - 8);
      if ((millis() / 65) % 2 == 0) {
        for (int lx = 20; lx < bx; lx += 8) display.drawPixel(lx, aimY, SSD1306_WHITE);
      }
      display.setCursor(bx - 10, aimY + 2);
      if (bossPattern == 2) display.print("!!");      // Double-Tap indicator
      else if (bossPattern == 3) display.print("~!"); // Sine-Wave indicator
      else display.print("!");
    }

    for (int b = 0; b < 2; b++) {
      if (bolts[b].active) {
        int bpx = (int)bolts[b].x + ox;
        int bpy = bolts[b].y + oy;
        display.fillRoundRect(bpx, bpy, 11, 4, 2, SSD1306_WHITE);
        display.drawLine(bpx + 11, bpy + 2, bpx + 16, bpy + 2, SSD1306_WHITE);
      }
    }
  } else {
    int obsW = 7, obsH = 18, obsY = GROUND_Y - 18;
    if (obsType == 0) {
      display.drawBitmap((int)obsX + ox, GROUND_Y - 10 + oy, mineBmp, 9, 10, SSD1306_WHITE);
    } else if (obsType == 1) {
      display.drawRect((int)obsX + ox, obsY + oy, obsW, obsH, SSD1306_WHITE);
      if ((millis() / 80) % 2 == 0) {
        display.drawLine((int)obsX + 3 + ox, obsY + 2 + oy, (int)obsX + 3 + ox, obsY + obsH - 2 + oy, SSD1306_WHITE);
      }
    } else if (obsType == 2 || obsType == 4) {
      int dY = (obsType == 2) ? (GROUND_Y - 13) : (GROUND_Y - 23);
      const unsigned char* dBmp = ((millis() / 60) % 2 == 0) ? droneBmpA : droneBmpB;
      display.drawBitmap((int)obsX + ox, dY + oy, dBmp, 14, 7, SSD1306_WHITE);
    } else {
      display.drawBitmap((int)obsX + ox, GROUND_Y - 10 + oy, mineBmp, 9, 10, SSD1306_WHITE);
      display.drawBitmap((int)obsX + 9 + ox, GROUND_Y - 10 + oy, mineBmp, 9, 10, SSD1306_WHITE);
    }
  }
}

// ================= INITIALIZATION =================
void setup() {
  // Keep Wi-Fi & BLE radios powered down during normal gameplay
  WiFi.mode(WIFI_OFF);

  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT,  INPUT_PULLUP);
  pinMode(PIN_BTN_TOP,   INPUT_PULLUP);
  pinMode(PIN_BUZZER,    OUTPUT);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(600000); // 600 kHz Fast-Mode Plus I2C for smooth ~60 FPS updates

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    while (true);
  }

  // Load saved records, inventory, and settings from NVS flash
  prefs.begin("byterun11", false);
  highScores[0]   = prefs.getInt("hi0", 0);
  highScores[1]   = prefs.getInt("hi1", 0);
  highScores[2]   = prefs.getInt("hi2", 0);
  currentDiff     = prefs.getInt("diff", DIFF_ARCADE);
  currentMap      = prefs.getInt("map", 0);
  equippedSkin    = prefs.getInt("skin", 0);
  currentSkin     = equippedSkin;
  totalSlides     = prefs.getInt("slides", 0);
  totalOrbs       = prefs.getInt("orbs", 0);
  bossesDefeated  = prefs.getInt("bosses", 0);
  orbBank         = prefs.getInt("bank", 20);
  phasePotions    = prefs.getInt("potions", 0);
  skinUnlocked[3] = prefs.getBool("sk3", false);
  skinUnlocked[4] = prefs.getBool("sk4", false);
  skinUnlocked[5] = prefs.getBool("sk5", false);
  soundEnabled    = prefs.getBool("sound", true);

  randomSeed(esp_random());
  lastUserActivity = millis();
}

// ================= MAIN GAME LOOP =================
void loop() {
  updateAudioEngine();

  // Sample tactile switches (Active LOW with internal pull-ups)
  bool rightPressed = (digitalRead(PIN_BTN_RIGHT) == LOW);
  bool leftPressed  = (digitalRead(PIN_BTN_LEFT)  == LOW);
  bool topPressed   = (digitalRead(PIN_BTN_TOP)   == LOW);

  bool rightJustPressed  = (rightPressed && !prevRightState);
  bool rightJustReleased = (!rightPressed && prevRightState);
  bool leftJustPressed   = (leftPressed  && !prevLeftState);
  bool topJustPressed    = (topPressed   && !prevTopState);

  if (rightPressed || leftPressed || topPressed) {
    lastUserActivity = millis();
  }

  prevRightState = rightPressed;
  prevLeftState  = leftPressed;
  prevTopState   = topPressed;

  // Enter Deep Sleep if left idle on Title or Main Menu
  if ((state == TITLE || state == MENU) && (millis() - lastUserActivity > IDLE_SLEEP_MS)) {
    enterDeepSleep();
  }

  // ================= 1. TITLE SCREEN =================
  if (state == TITLE) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    display.fillRect(2, 2, 124, 20, SSD1306_WHITE);
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(SSD1306_BLACK);
    drawCenteredText("BYTE RUN", 17);

    display.setTextColor(SSD1306_WHITE);
    drawByteWithSkin(59, 27, 0, 0);

    display.setFont(&Org_01);
    char subLine[26];
    snprintf(subLine, sizeof(subLine), "BEST:%d   ORBS:%d", highScores[currentDiff], orbBank);
    drawCenteredText(subLine, 44);

    display.drawLine(2, 49, 125, 49, SSD1306_WHITE);
    drawCenteredText("[T]SFX      [R]MENU", 57);
    display.display();

    if (topJustPressed) toggleSound();
    if (rightJustPressed && (millis() - stateChangeTime > 200)) {
      playTone(620, 45);
      state = MENU;
      stateChangeTime = millis();
    }
  }

  // ================= 2. MAIN MENU =================
  else if (state == MENU) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    display.fillRect(2, 2, 124, 10, SSD1306_WHITE);
    display.setFont(&Org_01);
    display.setTextColor(SSD1306_BLACK);
    drawCenteredText("MAIN MENU", 9);

    display.setTextColor(SSD1306_WHITE);

    char items[7][22];
    snprintf(items[0], sizeof(items[0]), "%s PLAY RUN",      (menuIndex == 0) ? ">" : " ");
    snprintf(items[1], sizeof(items[1]), "%s MAP: %s",      (menuIndex == 1) ? ">" : " ", mapNames[currentMap]);
    snprintf(items[2], sizeof(items[2]), "%s MODE: %s",     (menuIndex == 2) ? ">" : " ", diffNames[currentDiff]);
    snprintf(items[3], sizeof(items[3]), "%s GARAGE (%s)",  (menuIndex == 3) ? ">" : " ", skinNames[equippedSkin]);
    snprintf(items[4], sizeof(items[4]), "%s TOP SCORES",    (menuIndex == 4) ? ">" : " ");
    snprintf(items[5], sizeof(items[5]), "%s STORY & GOALS", (menuIndex == 5) ? ">" : " ");
    snprintf(items[6], sizeof(items[6]), "%s WIFI STATS",    (menuIndex == 6) ? ">" : " ");

    // Render 4-row sliding window over the 7 menu entries
    int topItem = constrain(menuIndex - 2, 0, 3);
    for (int i = 0; i < 4; i++) {
      display.setCursor(8, 20 + (i * 7));
      display.print(items[topItem + i]);
    }

    int barY = 14 + ((menuIndex * 24) / 6);
    display.fillRect(123, barY, 2, 10, SSD1306_WHITE);

    display.drawLine(2, 50, 125, 50, SSD1306_WHITE);
    drawCenteredText("[T]BACK [L]NEXT [R]OK", 58);
    display.display();

    // Hold-to-scroll auto-repeat on Left button
    bool repeatLeft = false;
    if (leftJustPressed) {
      leftPressStart = millis();
    } else if (leftPressed && (millis() - leftPressStart > 350) && (millis() - lastRepeatTick > 140)) {
      repeatLeft = true;
      lastRepeatTick = millis();
    }

    if (topJustPressed) {
      playTone(400, 40);
      state = TITLE;
      stateChangeTime = millis();
    } else if (leftJustPressed || repeatLeft) {
      menuIndex = (menuIndex + 1) % 7;
      playTone(520, 30);
    } else if (rightJustPressed && (millis() - stateChangeTime > 200)) {
      if (menuIndex == 0) {
        resetGame();
        playTone(680, 55);
        state = PLAYING;
      } else if (menuIndex == 1) {
        currentMap = (currentMap + 1) % 3;
        prefs.putInt("map", currentMap);
        playTone(580, 45);
      } else if (menuIndex == 2) {
        currentDiff = (currentDiff + 1) % 3;
        prefs.putInt("diff", currentDiff);
        playTone(580, 45);
      } else if (menuIndex == 3) {
        currentSkin = equippedSkin;
        playTone(620, 45);
        state = GARAGE;
      } else if (menuIndex == 4) {
        recordsPage = 0;
        playTone(620, 45);
        state = RECORDS;
      } else if (menuIndex == 5) {
        storyLine = 0;
        playTone(620, 45);
        state = STORY;
      } else if (menuIndex == 6) {
        commitStatsToFlash();
        startWifiCompanion();
        playShieldUpSound();
        state = WIFI_SYNC;
      }
    }
  }

  // ================= 3. BYTE GARAGE (SKINS & POTIONS) =================
  else if (state == GARAGE) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    display.fillRect(2, 2, 124, 10, SSD1306_WHITE);
    display.setFont(&Org_01);
    display.setTextColor(SSD1306_BLACK);
    char hdr[24];
    snprintf(hdr, sizeof(hdr), "GARAGE  ORBS:%d", orbBank);
    drawCenteredText(hdr, 9);

    display.setTextColor(SSD1306_WHITE);

    display.drawRect(8, 15, 22, 21, SSD1306_WHITE);
    drawByteWithSkin(14, 21, 0, 0);

    display.setCursor(35, 20);
    display.print(skinNames[currentSkin]);
    if (currentSkin == equippedSkin) display.print(" [ON]");
    else if (!skinUnlocked[currentSkin]) display.print(" [LOCK]");

    display.setCursor(35, 27);
    if (skinUnlocked[currentSkin]) {
      display.print("PERK:");
      display.print(skinPerks[currentSkin]);
    } else {
      display.print("REQ:");
      display.print(skinReqs[currentSkin]);
    }

    display.setCursor(35, 34);
    display.print("POTIONS:");
    display.print(phasePotions);
    display.print("/3 (20)");

    display.drawLine(2, 41, 125, 41, SSD1306_WHITE);
    drawCenteredText("TAP[R]:USE  HOLD[R]:BREW", 48);
    drawCenteredText("[T]BACK          [L]NEXT", 57);
    display.display();

    if (topJustPressed) {
      currentSkin = equippedSkin;
      playTone(400, 40);
      state = MENU;
    } else if (leftJustPressed) {
      currentSkin = (currentSkin + 1) % 6;
      playTone(540, 40);
    }

    // Long-press [R] (>500ms) brews a Phase Potion; short tap equips unlocked skin
    if (rightJustPressed) {
      rightPressStart = millis();
      rightHandledLong = false;
    }
    if (rightPressed && !rightHandledLong && (millis() - rightPressStart > 500)) {
      rightHandledLong = true;
      if (orbBank >= POTION_COST && phasePotions < 3) {
        orbBank -= POTION_COST;
        phasePotions++;
        prefs.putInt("bank", orbBank);
        prefs.putInt("potions", phasePotions);
        playPotionSound();
      } else {
        playTone(180, 90);
      }
    }
    if (rightJustReleased && !rightHandledLong) {
      if (skinUnlocked[currentSkin]) {
        equippedSkin = currentSkin;
        prefs.putInt("skin", equippedSkin);
        playShieldUpSound();
      } else {
        playTone(180, 90);
      }
    }
  }

  // ================= 3B. TOP SCORES & CAREER STATS =================
  else if (state == RECORDS) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    display.fillRect(2, 2, 124, 10, SSD1306_WHITE);
    display.setFont(&Org_01);
    display.setTextColor(SSD1306_BLACK);
    drawCenteredText(recordsPage == 0 ? "HIGH SCORES (1/2)" : "CAREER STATS (2/2)", 9);

    display.setTextColor(SSD1306_WHITE);

    if (recordsPage == 0) {
      char line[24];
      for (int d = 0; d < 3; d++) {
        int yPos = 21 + (d * 9);
        display.setCursor(10, yPos);
        snprintf(line, sizeof(line), "%-7s : %d", diffNames[d], highScores[d]);
        display.print(line);

        // Award star badge when score reaches 500+ (Warden Boss milestone)
        if (highScores[d] >= 500) {
          display.setCursor(104, yPos);
          display.print("[*]");
        }
      }
    } else {
      int unlockedCount = 0;
      for (int s = 0; s < 6; s++) if (skinUnlocked[s]) unlockedCount++;

      char row1[24], row2[24], row3[24], row4[24];
      snprintf(row1, sizeof(row1), "TOTAL ORBS  : %d", totalOrbs);
      snprintf(row2, sizeof(row2), "TOTAL SLIDES: %d", totalSlides);
      snprintf(row3, sizeof(row3), "WARDENS BEAT: %d", bossesDefeated);
      snprintf(row4, sizeof(row4), "SKINS OWNED : %d/6", unlockedCount);

      display.setCursor(10, 20); display.print(row1);
      display.setCursor(10, 27); display.print(row2);
      display.setCursor(10, 34); display.print(row3);
      display.setCursor(10, 41); display.print(row4);
    }

    display.drawLine(2, 50, 125, 50, SSD1306_WHITE);
    drawCenteredText("[T]BACK   [L/R]PAGE", 58);
    display.display();

    if (topJustPressed) {
      playTone(400, 40);
      state = MENU;
    } else if (leftJustPressed || rightJustPressed) {
      recordsPage = (recordsPage + 1) % 2;
      playTone(540, 35);
    }
  }

  // ================= 4. SCROLLABLE STORY & OBJECTIVES =================
  else if (state == STORY) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    display.fillRect(2, 2, 124, 10, SSD1306_WHITE);
    display.setFont(&Org_01);
    display.setTextColor(SSD1306_BLACK);
    drawCenteredText("BYTE: STORY & GOALS", 9);

    display.setTextColor(SSD1306_WHITE);
    for (int i = 0; i < 4; i++) {
      int idx = storyLine + i;
      if (idx < TOTAL_STORY_LINES) {
        display.setCursor(6, 20 + (i * 7));
        display.print(storyLines[idx]);
      }
    }

    int maxScroll = TOTAL_STORY_LINES - 4;
    int barY = 14 + ((storyLine * 24) / maxScroll);
    display.fillRect(123, barY, 2, 10, SSD1306_WHITE);

    display.drawLine(2, 50, 125, 50, SSD1306_WHITE);
    drawCenteredText("[T]BACK [L]UP [R]DOWN", 58);
    display.display();

    bool repUp = false, repDn = false;
    if (leftJustPressed) leftPressStart = millis();
    else if (leftPressed && (millis() - leftPressStart > 350) && (millis() - lastRepeatTick > 120)) {
      repUp = true; lastRepeatTick = millis();
    }
    if (rightJustPressed) rightPressStart = millis();
    else if (rightPressed && (millis() - rightPressStart > 350) && (millis() - lastRepeatTick > 120)) {
      repDn = true; lastRepeatTick = millis();
    }

    if (topJustPressed) {
      playTone(400, 40);
      state = MENU;
    } else if ((leftJustPressed || repUp) && storyLine > 0) {
      storyLine--;
      playTone(560, 25);
    } else if ((rightJustPressed || repDn) && storyLine < maxScroll) {
      storyLine++;
      playTone(480, 25);
    }
  }

  // ================= 4B. ON-DEMAND WI-FI TELEMETRY SERVER =================
  else if (state == WIFI_SYNC) {
    server.handleClient();

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    display.fillRect(2, 2, 124, 10, SSD1306_WHITE);
    display.setFont(&Org_01);
    display.setTextColor(SSD1306_BLACK);
    drawCenteredText("WIFI COMPANION ON", 9);

    display.setTextColor(SSD1306_WHITE);
    display.setCursor(6, 20);
    display.print("SSID: BYTE-RUN-ARCADE");
    display.setCursor(6, 28);
    display.print("IP  : 192.168.4.1");
    display.setCursor(6, 36);
    display.print("OPEN IN PHONE BROWSER");

    display.drawLine(2, 50, 125, 50, SSD1306_WHITE);
    drawCenteredText("[T] STOP WIFI & EXIT", 58);
    display.display();

    if (topJustPressed) {
      stopWifiCompanion();
      playTone(400, 45);
      state = MENU;
    }
  }

  // ================= 5. ACTIVE GAMEPLAY LOOP =================
  else if (state == PLAYING) {
    if (topJustPressed) {
      playPauseSound(true);
      state = PAUSED;
      return;
    }

    bool isGrounded = (playerY >= GROUND_Y - playerH - 0.5);
    if (isGrounded) {
      lastGroundedTime = millis();
      jumpsRemaining = 2;
    }

    // Sample player position every 30ms for motion-trail afterimages
    if (millis() - lastTrailSample > 30) {
      for (int i = 3; i > 0; i--) trail[i] = trail[i - 1];
      trail[0] = {PLAYER_X, (int)playerY, isSliding, true};
      lastTrailSample = millis();
    }

    // Ground Slide & Mid-Air Fast-Drop
    if (leftPressed) {
      if (isGrounded) {
        if (!isSliding) {
          isSliding = true;
          playerH = SLIDE_H;
          playerY = GROUND_Y - playerH;
          enqueueSweep(240, 190, 35);
          totalSlides++;
          statsDirty = true;
          checkSkinUnlocks();
        }
        if (random(0, 3) == 0) spawnParticles(PLAYER_X, GROUND_Y - 1, 1);
      } else if (leftJustPressed) {
        velocityY = 8.5;
        enqueueSweep(360, 220, 45);
        spawnParticles(PLAYER_X + 4, playerY, 4);
      }
    } else if (isSliding) {
      isSliding = false;
      playerH = NORMAL_H;
      playerY = GROUND_Y - playerH;
    }

    // Coyote Time + Jump Buffering evaluation
    if (rightJustPressed) {
      jumpBufferTime = millis();
    }

    bool hasBufferedJump = (jumpBufferTime > 0) && (millis() - jumpBufferTime <= JUMP_BUFFER_MS);
    bool inCoyoteWindow  = (millis() - lastGroundedTime <= COYOTE_MS);

    if (hasBufferedJump) {
      if (isGrounded || inCoyoteWindow) {
        executeJump(true);
      } else if (jumpsRemaining > 0) {
        executeJump(false);
      }
    }

    // Variable jump height when jump button is released early
    if (!rightPressed && velocityY < -2.8) velocityY = -2.8;

    float activeGravity = 0.62;
    if (currentMap == 2) activeGravity -= 0.07;   // CORE LAB low gravity
    if (equippedSkin == 2) activeGravity -= 0.06; // JET-WING glide frame

    playerY += velocityY;
    velocityY += activeGravity;

    if (playerY >= GROUND_Y - playerH) {
      playerY = GROUND_Y - playerH;
      velocityY = 0;
      jumpsRemaining = 2;
      lastGroundedTime = millis();
    }

    // Trigger Escalating Firewall Warden Boss every 500 points
    if (!bossActive && score >= nextBossScore) {
      bossActive = true;
      bossWave = max(1, nextBossScore / 500);
      bossStartTime = millis();
      bolts[0].active = false;
      bolts[1].active = false;
      obsX = -30;
      for (int i = 0; i < 3; i++) pickups[i].type = 0;
      setupNextBossSalvo();

      char bossMsg[14];
      snprintf(bossMsg, sizeof(bossMsg), "WARDEN LV.%d!", bossWave);
      triggerPopup(26, 22, bossMsg);
      playBossFanfare(true);
    }

    int activeW = isSliding ? 12 : PLAYER_W;

    if (bossActive) {
      bossY = 24 + sin(millis() / 220.0) * 8.0;

      unsigned long aimDelay = (currentDiff == DIFF_HYPER) ? 780 : 1050;
      unsigned long elapsedAim = millis() - bossActionTimer;

      // Pattern 1 (Wave 2+): Mid-aim lane feint
      if (bossState == 0 && bossPattern == 1 && !bossFeinted && elapsedAim > (aimDelay / 2)) {
        bossFeinted = true;
        bossLane = 1 - bossLane;
        enqueueSweep(600, 820, 40);
      }

      if (bossState == 0 && elapsedAim > aimDelay) {
        fireBossSalvo();
      }

      // Update active boss bolts and check player hitboxes
      for (int b = 0; b < 2; b++) {
        if (bolts[b].active) {
          bolts[b].x -= (gameSpeed + 1.4);

          // Pattern 3 (Wave 3+): Vertical sine-wave trajectory
          if (bolts[b].pattern == 1) {
            bolts[b].y = bolts[b].baseY + (int)(sin(bolts[b].x * 0.14) * 5.0);
            bolts[b].y = constrain(bolts[b].y, GROUND_Y - 16, GROUND_Y - 4);
          }

          bool hitBoltX = (PLAYER_X + activeW - 1 > bolts[b].x) && (PLAYER_X + 1 < bolts[b].x + 11);
          bool hitBoltY = (playerY + playerH > bolts[b].y) && (playerY < bolts[b].y + 4);

          if (hitBoltX && hitBoltY) {
            bolts[b].active = false;
            if (!absorbOrSurviveHit(PLAYER_X + 6, playerY + 4)) {
              flashFrames = 2;
              playCrashSound();
              if (score > highScores[currentDiff]) {
                highScores[currentDiff] = score;
                char key[5]; snprintf(key, sizeof(key), "hi%d", currentDiff);
                prefs.putInt(key, score);
              }
              commitStatsToFlash();
              shakeFrames = 10;
              state = GAMEOVER;
              stateChangeTime = millis();
              return;
            }
          }

          if (bolts[b].x < -15) {
            bolts[b].active = false;
          }
        }
      }

      if (bossState == 1 && !bolts[0].active && !bolts[1].active) {
        setupNextBossSalvo();
      }

      // Boss encounter survived -> award bonus points, orbs, and shield
      if (millis() - bossStartTime >= BOSS_DURATION) {
        bossActive = false;
        bolts[0].active = false;
        bolts[1].active = false;
        int waveBonus = 250 * bossWave;
        rawScore += waveBonus;
        score = (int)rawScore;
        nextBossScore = score + 500;
        hasShield = true;
        int orbReward = 10 + (bossWave * 2);
        orbBank += orbReward;
        bossesDefeated++;
        statsDirty = true;
        commitStatsToFlash();
        spawnParticles(112, bossY + 8, 14);

        char winBuf[14];
        snprintf(winBuf, sizeof(winBuf), "+%d & %d ORB", waveBonus, orbReward);
        triggerPopup(18, 20, winBuf);
        playBossFanfare(false);
        obsX = 145;
      }
    } else {
      // Standard obstacle scrolling and collision detection
      obsX -= gameSpeed;
      if (obsX < -24) spawnObstacleAndPickups();

      int obsW = 9, obsH = 10, obsY = GROUND_Y - 10;
      if (obsType == 1) { obsW = 7; obsH = 18; obsY = GROUND_Y - 18; }
      else if (obsType == 2) { obsW = 14; obsH = 7; obsY = GROUND_Y - 13; }
      else if (obsType == 3) { obsW = 18; obsH = 10; obsY = GROUND_Y - 10; }
      else if (obsType == 4) { obsW = 14; obsH = 7; obsY = GROUND_Y - 23; }

      bool hitX = (PLAYER_X + activeW - 2 > obsX) && (PLAYER_X + 2 < obsX + obsW);
      bool hitY = (playerY + playerH - 1 > obsY) && (playerY + 1 < obsY + obsH);

      if (hitX && hitY) {
        if (absorbOrSurviveHit(obsX + 4, obsY + 4)) {
          obsX = -30;
        } else {
          flashFrames = 2;
          playCrashSound();
          if (score > highScores[currentDiff]) {
            highScores[currentDiff] = score;
            char key[5]; snprintf(key, sizeof(key), "hi%d", currentDiff);
            prefs.putInt(key, score);
          }
          commitStatsToFlash();
          shakeFrames = 10;
          state = GAMEOVER;
          stateChangeTime = millis();
          return;
        }
      }
    }

    // Update Orbs & Shield pickups
    for (int i = 0; i < 3; i++) {
      if (pickups[i].type > 0) {
        pickups[i].x -= gameSpeed;
        bool grabX = (PLAYER_X + activeW > pickups[i].x - 4) && (PLAYER_X < pickups[i].x + 4);
        bool grabY = (playerY + playerH > pickups[i].y - 4) && (playerY < pickups[i].y + 4);

        if (grabX && grabY) {
          if (pickups[i].type == 1) {
            int pts = (int)(25 * combo * diffScoreMult[currentDiff]);
            rawScore += pts;
            score = (int)rawScore;
            playArpeggioNote(pickups[i].noteIdx + (combo - 1));
            if (combo < 4) combo++;
            comboTimer = millis();

            int orbGain = (equippedSkin == 4) ? 2 : 1; // CROWN 2x Orb perk
            totalOrbs += orbGain;
            orbBank   += orbGain;
            statsDirty = true;
            checkSkinUnlocks();

            char buf[8]; snprintf(buf, sizeof(buf), "+%d", pts);
            triggerPopup(pickups[i].x, pickups[i].y - 4, buf);
          } else if (pickups[i].type == 2) {
            hasShield = true;
            playShieldUpSound();
            triggerPopup(pickups[i].x - 6, pickups[i].y - 4, "SHIELD!");
          }
          spawnParticles(pickups[i].x, pickups[i].y, 6);
          pickups[i].type = 0;
        } else if (pickups[i].x < -10) {
          if (pickups[i].type == 1 && !bossActive) combo = 1;
          pickups[i].type = 0;
        }
      }
    }

    if (combo > 1 && (millis() - comboTimer > 4500)) combo = 1;

    // Distance scoring and progressive speed ramp
    if (millis() - lastScoreTick >= 60) {
      rawScore += (combo * diffScoreMult[currentDiff]);
      score = (int)rawScore;
      lastScoreTick = millis();
      checkSkinUnlocks();
      float maxSpd = (currentDiff == DIFF_HYPER) ? 9.2 : 7.8;
      if (gameSpeed < maxSpd) gameSpeed += diffAccel[currentDiff];
    }

    // Background rhythmic bassline (yields cleanly whenever an SFX sweep is active)
    unsigned long beatInterval = max(95, (int)(230 - (gameSpeed * 13)));
    if (soundEnabled && !sfxPlaying && sfxHead == sfxTail && (millis() - lastBassTick > beatInterval)) {
      int noteFreq = bassNormal[bassStep];
      if (bossActive) noteFreq = bassBoss[bassStep];
      else if (hasShield || millis() < immuneUntil) noteFreq = bassShield[bassStep];

      if (harmonyToggle && (bassStep % 2 == 1)) noteFreq *= 2;
      harmonyToggle = !harmonyToggle;

      tone(PIN_BUZZER, noteFreq, 30);
      bassStep = (bassStep + 1) % 8;
      lastBassTick = millis();
    }

    int ox = 0, oy = 0;
    if (shakeFrames > 0) {
      ox = random(-2, 3);
      oy = random(-2, 3);
      shakeFrames--;
    }

    if (flashFrames > 0) {
      display.invertDisplay(true);
      flashFrames--;
    } else {
      display.invertDisplay(false);
    }

    renderGameplayScene(ox, oy);
    display.display();
    delay(16);
  }

  // ================= 6. PAUSED OVERLAY =================
  else if (state == PAUSED) {
    renderGameplayScene(0, 0);

    display.fillRect(12, 12, 104, 42, SSD1306_BLACK);
    display.drawRect(12, 12, 104, 42, SSD1306_WHITE);
    display.drawRect(14, 14, 100, 38, SSD1306_WHITE);

    display.setFont(&Org_01);
    display.setTextColor(SSD1306_WHITE);
    drawCenteredText("PAUSED", 24);
    drawCenteredText("[T] RESUME", 35);
    drawCenteredText("[L]MENU  [R]RETRY", 46);
    display.display();

    if (topJustPressed) {
      playPauseSound(false);
      lastScoreTick = millis();
      state = PLAYING;
    } else if (leftJustPressed) {
      commitStatsToFlash();
      state = MENU;
      stateChangeTime = millis();
    } else if (rightJustPressed) {
      commitStatsToFlash();
      resetGame();
      playTone(520, 50);
      state = PLAYING;
    }
    delay(16);
  }

  // ================= 7. GAME OVER SCREEN =================
  else if (state == GAMEOVER) {
    display.invertDisplay(false);
    int ox = 0, oy = 0;
    if (shakeFrames > 0) {
      ox = random(-3, 4);
      oy = random(-3, 4);
      shakeFrames--;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setFont(&FreeSansBold9pt7b);
    drawCenteredText("GAME OVER", 18 + oy, ox);

    display.setFont(&Org_01);
    char scoreLine[26];
    snprintf(scoreLine, sizeof(scoreLine), "%s SCORE: %d", diffNames[currentDiff], score);
    drawCenteredText(scoreLine, 30 + oy, ox);

    if (score >= highScores[currentDiff] && score > 0) {
      drawCenteredText("* NEW RECORD! *", 40 + oy, ox);
    } else {
      char bestLine[26];
      snprintf(bestLine, sizeof(bestLine), "BEST:%d   ORBS:%d", highScores[currentDiff], orbBank);
      drawCenteredText(bestLine, 40 + oy, ox);
    }

    if (millis() - stateChangeTime > 600) {
      display.drawLine(4, 47, 123, 47, SSD1306_WHITE);
      drawCenteredText("[T]SFX [L]MENU [R]RETRY", 56);

      if (topJustPressed) toggleSound();
      else if (leftJustPressed) {
        state = MENU;
        stateChangeTime = millis();
      } else if (rightJustPressed) {
        resetGame();
        playTone(520, 50);
        state = PLAYING;
      }
    }
    display.display();
    delay(16);
  }
}
