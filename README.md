# BYTE RUN — ESP32-C3 SuperMini Arcade Runner

An endless cyber-runner built for the **ESP32-C3 SuperMini** and a **128×64 SSD1306 I2C OLED** display, featuring multi-wave boss battles, unlockable skins with passive perks, a non-blocking chiptune audio engine, an on-demand Wi-Fi telemetry dashboard, and deep-sleep power protection.

---

## Hardware & Wiring

| Component | ESP32-C3 SuperMini Pin | Notes |
| :--- | :--- | :--- |
| **OLED SDA** | `GPIO 8` | I2C Data (600 kHz Fast-Mode Plus) |
| **OLED SCL** | `GPIO 9` | I2C Clock |
| **OLED VCC / GND** | `3.3V` / `GND` | 0.96" or 1.3" SSD1306 (`0x3C`) |
| **Right Button `[R]`** | `GPIO 3` | Active LOW (`INPUT_PULLUP`) — Jump / Select / RTC Wake |
| **Left Button `[L]`** | `GPIO 5` | Active LOW (`INPUT_PULLUP`) — Slide / Fast-Drop / Next |
| **Top Button `[T]`** | `GPIO 7` | Active LOW (`INPUT_PULLUP`) — Pause / Back / Mute |
| **Passive Buzzer (+)** | `GPIO 4` | Wire through a **100 Ω series resistor** to protect GPIO |

---

## Controls

| Screen / State | `[R]` Right Button | `[L]` Left Button | `[T]` Top Button |
| :--- | :--- | :--- | :--- |
| **Title Screen** | Open Main Menu (also wakes from Deep Sleep) | — | Toggle SFX Mute |
| **Main Menu** | Confirm / Cycle Option | Next Item *(Hold to auto-scroll)* | Back to Title |
| **Garage** | **Tap:** Equip Skin · **Hold (0.5s):** Brew Phase Potion (`20 Orbs`) | Next Skin | Back to Menu |
| **Top Scores** | Toggle Page (`High Scores` / `Career Stats`) | Toggle Page | Back to Menu |
| **Story & Goals** | Scroll Down *(Hold to auto-scroll)* | Scroll Up *(Hold to auto-scroll)* | Back to Menu |
| **Wi-Fi Stats** | — | — | Stop Wi-Fi AP & Exit |
| **Gameplay** | **Tap:** Jump / Double-Jump *(with Coyote Time & Jump Buffer)* | **Hold (Ground):** Slide · **Tap (Air):** Fast-Drop | Pause Game |
| **Paused** | Retry Run | Quit to Main Menu | Resume Game |
| **Game Over** | Retry Run | Quit to Main Menu | Toggle SFX Mute |

---

## Gameplay Features

### 1. Maps & Difficulty Modes
* **Maps:** `NEON CITY` (Standard gravity), `DATA VOID` (Aerial drones & multi-mines), `CORE LAB` (Low-gravity physics).
* **Difficulties:** `CHILL` (`1.0x` score), `ARCADE` (`1.5x` score), `HYPER` (`2.0x` score, faster boss lock-on).

### 2. Skins & Garage Economy
Collect **Orbs** during runs to bank currency, brew **Phase Potions** (`20 Orbs`, max `3` held — automatically grants `2.5s` immunity on a fatal hit), and unlock new chassis perks:

| Skin | Unlock Requirement | Built-In Perk |
| :--- | :--- | :--- |
| **CLASSIC** | Free | Standard chassis |
| **VISOR** | Free | Starts every run with an active **Shield** |
| **JET-WING** | Free | Reduced gravity for extended air glide |
| **NINJA** | Perform `15` total slides | `35%` chance to **Evade** fatal hits |
| **CROWN** | Reach `500` pts in **HYPER** mode | Starts with **Shield** + `2x` **Orb** pickups |
| **PHANTOM** | Collect `50` lifetime orbs | `50%` chance to **Evade** fatal hits + ghost trail |

### 3. Escalating Firewall Warden Bosses
Every `500` points, the **Firewall Warden** halts regular obstacles for a 14-second duel. Watch the laser sight before each shot:
* **Wave 1 (`500` pts):** Single straight bolts — Low bolt (`Jump`), High bolt (`Slide`).
* **Wave 2 (`1000` pts):** Introduces **Lane Feints** (laser switches lanes mid-aim) and **Double-Tap Combos** (`!!` indicator — two staggered bolts in opposite lanes).
* **Wave 3+ (`1500+` pts):** Adds **Sine-Wave Plasma Bolts** (`~!` indicator) that oscillate vertically across the screen.

### 4. Wi-Fi Telemetry Portal & Power Safety
* Select **WIFI STATS** in the Main Menu to start a low-power (`11 dBm`) Wi-Fi Access Point named **`BYTE-RUN-ARCADE`**. Connect with a phone and open **`http://192.168.4.1`** to view high scores and lifetime stats. Wi-Fi stays completely powered off during gameplay.
* **NVS Flash Wear Protection:** Run stats batch in RAM and commit to flash only on run end, boss defeat, or menu changes.
* **Auto Deep Sleep:** After **2 minutes** of inactivity on the Title or Main Menu screens, the OLED powers off (`SSD1306_DISPLAYOFF`) to prevent burn-in and the ESP32-C3 enters deep sleep (`~45 µA`). Press `[R]` (`GPIO 3`) to wake instantly.

---

## Required Arduino Libraries
Install via the Arduino IDE Library Manager:
1. `Adafruit GFX Library`
2. `Adafruit SSD1306`
*(Built-in ESP32 libraries used: `Wire`, `Preferences`, `WiFi`, `WebServer`, `esp_sleep.h`)*
