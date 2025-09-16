# Repository Guidelines

## Project Structure & Module Organization
The firmware lives entirely in `ESP32_BusPirate_Stable.ino`. Inside the sketch you will find grouped sections for CLI I/O, protocol handlers, display support, and utility helpers—keep related additions with the existing banner comments. Reference photos stay in `img/`, while user-facing docs reside in `README.md` and `USAGE.md`; sync any interface changes across these files. If you introduce auxiliary scripts (e.g., build helpers), place them in a new `tools/` directory and document their usage here.

## Build, Test, and Development Commands
- `arduino-cli core install esp32:esp32` — install the ESP32 board support package before first compile.
- `arduino-cli compile --fqbn esp32:esp32:esp32 ESP32_BusPirate_Stable.ino` — verify the sketch for a generic ESP32 DevKit target.
- `arduino-cli upload --port /dev/ttyUSB0 --fqbn esp32:esp32:esp32 ESP32_BusPirate_Stable.ino` — flash the board (adjust port and FQBN to match your hardware).
Capture compile/upload logs and attach the relevant excerpt to your pull request.

## Coding Style & Naming Conventions
Use Arduino-flavoured C++ with four-space indentation and opening braces on the same line. Maintain the `// -------- Section --------` banner pattern to segment logic. Constants and macros stay UPPER_CASE; functions, locals, and member-like globals use camelCase. Reserve `String` for serial buffers, prefer fixed-width integer types for protocol data, and favour early returns over deep nesting. Run the Arduino IDE auto-formatter (Ctrl/Cmd+T) before committing sizeable edits.

## Testing Guidelines
There is no automated suite; verification happens on hardware. After compiling, boot into Hi-Z, execute `mode i2c` and `i2c scan`, perform a representative `spi x` transfer, and confirm UART echo at 115200 baud. Test the optional OLED if touched and watch for watchdog resets or heap warnings. Document the board revision, peripherals connected, and observed serial output in the PR.

## Commit & Pull Request Guidelines
Adopt the project’s short, imperative commit subjects (e.g., `Fix OLED init hang`). Keep commits focused and avoid bundling unrelated documentation or firmware changes. Pull requests should explain the motivation, call out protocol coverage in testing, and link any tracked issues. Include terminal transcripts or screenshots when behaviour changes (serial prompts, OLED screens) so reviewers can verify outcomes quickly.
