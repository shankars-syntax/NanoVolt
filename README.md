# 🎮 NanoVolt

"NanoVolt" is a handheld retro gaming console built from scratch using an Arduino Nano. It features a graphical OLED display, analog joystick controls, motion sensing via an accelerometer, and six built-in games — all packed into a compact, portable, rechargeable device.

This project was built end-to-end: circuit design, soldering, firmware development, and game design, going from a rough sketch on paper to a working prototype.

Photos of the build and a demo video are available in the `Images/` and `Demo/` folders.

 Features

- 6 built-in games, controlled via joystick and tilt
- 1.3" SH1106 OLED graphical display
- ADXL345 accelerometer for motion-based gameplay
- Buzzer sound effects
- Nested, scrollable menu system
- Adjustable brightness (5 levels)
- Difficulty selection (Easy / Normal / Hard)
- Persistent high-score tracking (EEPROM)
- Joystick and accelerometer calibration
- Tap to pause (Continue / Back to Menu), hold to quit to main menu
- Power-on self-test (POST) for all peripherals
- Rechargeable, portable design

 Games Included

| Game | Input | Description |
|---|---|---|
| Bounce Ball | Joystick | Breakout-style paddle game |
| XO (2-Player) | Joystick | Tic-Tac-Toe, hot-seat two-player |
| Reflex Test | Button | Reaction-time challenge |
| Space Shooter | Tilt | Dodge and shoot enemy waves |
| Star Catcher | Tilt | Catch falling stars |
| Maze Runner | Tilt | Navigate a randomized maze against the clock |

(More games can be added in future updates.)

 Hardware Used

| Component | Description |
|---|---|
| Arduino Nano | Main microcontroller (ATmega328P) |
| SH1106 OLED Display | 1.3" 128×64 graphics display (I2C) |
| Analog Joystick | Navigation and gameplay input |
| ADXL345 Accelerometer | Motion/tilt-based games (I2C) |
| Active Buzzer | Audio feedback |
| Slide Switch | Power |
| Li-ion Battery | Portable power source |
| TP4056 Charging Module | USB-C battery charging |
| Perfboard | Prototype PCB |

 What I Learnt

Building NanoVolt taught me a lot beyond just making games work:

- Embedded C/C++ under constraints — Working with an ATmega328P's limited RAM meant learning to use `PROGMEM` and `F()` macros to keep strings out of RAM, and packing game state into small structs.
- I2C communication — Talking directly to the SH1106 OLED and ADXL345 accelerometer over the Wire library, including register reads/writes and device-ID checks.
- Non-blocking game loops — Moving away from `delay()`-heavy code to `millis()`-based timing so input, physics, and rendering stay responsive.
- Signal filtering — Smoothing noisy accelerometer readings with a simple exponential moving average for stable tilt controls.
- UI design on small displays — Designing a nested, scrollable menu system on a 128×64 monochrome screen.
- EEPROM persistence — Reading and writing scores, calibration offsets, and settings safely, including handling default/unwritten memory states.
- Input handling — Implementing short-press vs. long-press detection on a single button, plus a pause/confirm dialog.
- Calibration routines — Building joystick and accelerometer calibration flows, and understanding why sensors need it in the first place.
- Hardware and power management — Practical electronics: battery charging with a TP4056 module, power switching, and soldering a reliable perfboard prototype.
- Debugging real hardware — Diagnosing issues that could be a cold solder joint, a miswired I2C line, or a noisy sensor, which led to building the POST screen for quick diagnostics.
- Shipping something complete— Learning what turns a working sketch into a finished product: boot animation, sound design, persistent settings, and an About screen.

Future Improvements

- Custom PCB
- 3D printed enclosure
- More games
- Better UI animations
- Multiplayer support over wireless (nRF24 / Bluetooth)

Author
Vidya Shankar

## Author

Vidya Shankar
