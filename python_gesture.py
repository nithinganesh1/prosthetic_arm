"""
MediaPipe Hand Tracking -> ESP32 Servo Array (5 fingers)
---------------------------------------------------------
Reads webcam, tracks one hand, computes a curl angle (0-180) for each
of the 5 fingers, and streams them to the ESP32 over serial as:

    "<thumb>,<index>,<middle>,<ring>,<pinky>\n"

The ESP32 firmware only acts on this data when it is NOT in "manual"
web mode. The 6th (joint) servo is never touched from here - it's
web-only, per the firmware.

Install deps:
    pip install opencv-python mediapipe pyserial

Before running:
    1. Set SERIAL_PORT below to your ESP32's port
       (e.g. "COM5" on Windows, "/dev/ttyUSB0" or "/dev/ttyACM0" on Linux/Mac)
    2. Plug the ESP32 in over USB (this uses direct USB serial, not WiFi)
"""

import time
import math

import cv2
import mediapipe as mp
import serial

# ---------------- Configuration ----------------
SERIAL_PORT = "COM5"          # <-- CHANGE THIS to your ESP32 port
BAUD_RATE = 115200
SEND_INTERVAL = 0.05          # seconds between serial sends (~20 Hz)
SMOOTHING_ALPHA = 0.4         # exponential smoothing factor (0-1, higher = less smoothing)

# Calibration: raw angle range measured at the PIP joint for a
# fully-curled vs fully-extended finger. Tune these per your hand/rig.
RAW_ANGLE_CURLED = 40
RAW_ANGLE_EXTENDED = 175

# If a finger should map to 180 when *extended*, keep INVERT = False.
# If your mechanism needs the opposite (extended = 0), set INVERT = True.
INVERT = False
# -------------------------------------------------

FINGER_TIPS = [4, 8, 12, 16, 20]
FINGER_PIPS = [3, 6, 10, 14, 18]
FINGER_MCPS = [2, 5, 9, 13, 17]

smoothed_angles = [90, 90, 90, 90, 90]


def calc_angle(a, b, c):
    """Angle at point b, formed by points a-b-c, in degrees (0-180)."""
    ang = math.degrees(
        math.atan2(c[1] - b[1], c[0] - b[0]) - math.atan2(a[1] - b[1], a[0] - b[0])
    )
    ang = abs(ang)
    if ang > 180:
        ang = 360 - ang
    return ang


def remap(value, in_min, in_max, out_min=0, out_max=180):
    value = max(min(value, max(in_min, in_max)), min(in_min, in_max))
    result = (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min
    return max(0, min(180, result))


def get_finger_angles(landmarks):
    raw_angles = []
    for tip, pip, mcp in zip(FINGER_TIPS, FINGER_PIPS, FINGER_MCPS):
        a = (landmarks[mcp].x, landmarks[mcp].y)
        b = (landmarks[pip].x, landmarks[pip].y)
        c = (landmarks[tip].x, landmarks[tip].y)
        raw_angles.append(calc_angle(a, b, c))

    servo_angles = []
    for raw in raw_angles:
        mapped = remap(raw, RAW_ANGLE_CURLED, RAW_ANGLE_EXTENDED, 0, 180)
        if INVERT:
            mapped = 180 - mapped
        servo_angles.append(int(mapped))
    return servo_angles


def smooth(new_angles):
    for i in range(5):
        smoothed_angles[i] = int(
            SMOOTHING_ALPHA * new_angles[i] + (1 - SMOOTHING_ALPHA) * smoothed_angles[i]
        )
    return smoothed_angles


def main():
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)  # allow ESP32 to reset after opening the port

    mp_hands = mp.solutions.hands
    mp_draw = mp.solutions.drawing_utils

    hands = mp_hands.Hands(
        static_image_mode=False,
        max_num_hands=1,
        min_detection_confidence=0.7,
        min_tracking_confidence=0.7,
    )

    cap = cv2.VideoCapture(0)
    last_sent = 0.0

    print("Running. Press 'q' in the video window to quit.")

    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                break

            frame = cv2.flip(frame, 1)
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            results = hands.process(rgb)

            if results.multi_hand_landmarks:
                hand_landmarks = results.multi_hand_landmarks[0]
                mp_draw.draw_landmarks(frame, hand_landmarks, mp_hands.HAND_CONNECTIONS)

                raw = get_finger_angles(hand_landmarks.landmark)
                angles = smooth(raw)

                now = time.time()
                if now - last_sent > SEND_INTERVAL:
                    line = ",".join(str(a) for a in angles) + "\n"
                    ser.write(line.encode())
                    last_sent = now

                cv2.putText(
                    frame, str(angles), (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2
                )

            cv2.imshow("Hand Gesture Control", frame)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
    finally:
        cap.release()
        cv2.destroyAllWindows()
        ser.close()


if __name__ == "__main__":
    main()
