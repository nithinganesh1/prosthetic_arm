# Prosthetic Arm — Hand Gesture Servo Control

An ESP32-based prosthetic/robotic arm controller with two input sources for the
5 finger servos, plus a dedicated wrist/joint servo that's always controlled
from the web UI.

- **`esp32_gesture.ino`** — Firmware for the ESP32. Drives 6 servo channels via a
  PCA9685 (Adafruit_PWMServoDriver), hosts a WiFi access point with a control
  web page, and reads finger angles over USB serial.
- **`python_gesture.py`** — Desktop script using MediaPipe Hands to track a
  hand via webcam, compute a curl angle per finger, and stream those 5 angles
  to the ESP32 over USB serial.

## How it works

| Servo channel | Controls    | Default source                  | Manual mode source |
|----------------|------------|----------------------------------|---------------------|
| 0              | Thumb      | Serial (MediaPipe)               | Web slider          |
| 1              | Index      | Serial (MediaPipe)               | Web slider          |
| 2              | Middle     | Serial (MediaPipe)                | Web slider          |
| 3              | Ring       | Serial (MediaPipe)               | Web slider          |
| 4              | Pinky      | Serial (MediaPipe)                | Web slider          |
| 5              | Joint/wrist| Web only, always                 | Web only, always    |

The web page has a **Serial / Manual** toggle. In Serial mode (default), the
ESP32 parses angle data coming in over USB from `python_gesture.py` and drives
servos 0–4 directly. Flip the toggle to Manual and the same 5 servos are
instead driven by sliders on the web page; serial input for them is ignored
until you flip it back. The joint servo (channel 5) is never linked to
serial — it's always driven from its own web slider regardless of mode.

## Hardware setup

- ESP32 dev board
- PCA9685 PWM driver board (I2C)
- 6 servos wired to PCA9685 channels 0–5
- USB cable from ESP32 to the computer running `python_gesture.py`
- Webcam for hand tracking

## Firmware setup (`esp32_gesture.ino`)

1. Install libraries in Arduino IDE: `WiFi`, `WebServer` (bundled with ESP32
   core), `Wire` (bundled), `Adafruit PWM Servo Driver Library`.
2. Flash `esp32_gesture.ino` to the ESP32.
3. On boot it creates a WiFi access point:
   - SSID: `ESP32_Hand_Control`
   - Password: `12345678`
4. Connect a phone or laptop to that hotspot and open the ESP32's IP
   (printed to Serial Monitor at boot, e.g. `192.168.4.1`) in a browser to
   reach the control page.

## Python setup (`python_gesture.py`)

```bash
pip install opencv-python mediapipe pyserial
```

Before running, open `python_gesture.py` and set:

- `SERIAL_PORT` — your ESP32's USB serial port
  (e.g. `COM5` on Windows, `/dev/ttyUSB0` or `/dev/ttyACM0` on Linux/Mac)
- `RAW_ANGLE_CURLED` / `RAW_ANGLE_EXTENDED` — calibration constants. Run the
  script, watch the angle array printed on the video overlay, and adjust
  these two values until a fully curled finger reads near 0 and a fully
  extended finger reads near 180. Flip `INVERT = True` if your mechanism
  needs the opposite mapping.

Run it:

```bash
python python_gesture.py
```

Press `q` in the video window to quit.

## Usage

1. Power the ESP32 and connect it to the computer via USB.
2. Connect to the ESP32's WiFi hotspot from your phone/laptop and open the
   control page in a browser.
3. Run `python_gesture.py` on the computer connected via USB — with the mode
   toggle left on **Serial**, the fingers will now follow your hand in front
   of the webcam.
4. Use the joint slider on the web page any time to move the wrist —
   independent of whatever the fingers are doing.
5. Flip the toggle to **Manual** to take direct slider control of the
   fingers instead (useful for testing, calibration, or fine positioning
   without the camera running).

## Notes / things to tune

- Serial baud rate is `115200` on both ends — keep them matched if changed.
- The firmware only applies a serial frame if it parses exactly 5
  comma-separated values, so a truncated or garbled line is dropped rather
  than causing a bad movement.
- `SEND_INTERVAL` and `SMOOTHING_ALPHA` in the Python script control update
  rate and jitter smoothing — lower `SMOOTHING_ALPHA` for smoother but
  laggier motion.

## Credits

Arm design/mechanism: **Inmove**.
