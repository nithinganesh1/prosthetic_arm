#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// --- Hotspot Credentials ---
const char* ssid     = "ESP32_Hand_Control";
const char* password = "12345678";

WebServer server(80);
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// --- Servo Configuration ---
#define USMIN  600
#define USMAX  2400
#define SERVO_FREQ 50

const int NUM_FINGER_SERVOS = 5;   // channels 0-4: thumb, index, middle, ring, pinky
const int JOINT_SERVO       = 5;   // channel 5: wrist/joint - WEB ONLY, never touched by serial

const int MIN_DEGREE = 0;
const int MAX_DEGREE = 180;

int fingerAngle[NUM_FINGER_SERVOS] = {90, 90, 90, 90, 90};
int jointAngle = 90;

// false = fingers follow Serial data from MediaPipe (default)
// true  = fingers follow the web sliders instead
bool manualMode = false;

String serialBuffer = "";

// --- Low-level servo write ---
void setServoAngle(int channel, int angle) {
  angle = constrain(angle, MIN_DEGREE, MAX_DEGREE);
  int pulseMicros = map(angle, 0, 180, USMIN, USMAX);
  pwm.writeMicroseconds(channel, pulseMicros);
}

void applyFingerAngle(int idx, int angle) {
  if (idx < 0 || idx >= NUM_FINGER_SERVOS) return;
  angle = constrain(angle, MIN_DEGREE, MAX_DEGREE);
  fingerAngle[idx] = angle;
  setServoAngle(idx, angle);
}

void applyJointAngle(int angle) {
  angle = constrain(angle, MIN_DEGREE, MAX_DEGREE);
  jointAngle = angle;
  setServoAngle(JOINT_SERVO, angle);
}

// --- Parse one line like "90,45,120,60,30" -> 5 finger angles ---
void parseSerialLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  int values[NUM_FINGER_SERVOS];
  int count = 0;
  int startIndex = 0;
  int commaIndex = line.indexOf(',');

  while (startIndex < line.length() && count < NUM_FINGER_SERVOS) {
    String piece;
    if (commaIndex == -1) {
      piece = line.substring(startIndex);
      startIndex = line.length();
    } else {
      piece = line.substring(startIndex, commaIndex);
      startIndex = commaIndex + 1;
      commaIndex = line.indexOf(',', startIndex);
    }
    values[count] = piece.toInt();
    count++;
  }

  // Only apply if we got a complete, well-formed frame of exactly 5 values
  if (count == NUM_FINGER_SERVOS) {
    for (int i = 0; i < NUM_FINGER_SERVOS; i++) {
      applyFingerAngle(i, values[i]);
    }
  }
}

void readSerialData() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      if (!manualMode) {           // ignore serial while in manual/web mode
        parseSerialLine(serialBuffer);
      }
      serialBuffer = "";
    } else if (c != '\r') {
      serialBuffer += c;
      if (serialBuffer.length() > 64) serialBuffer = ""; // safety against garbage
    }
  }
}

// --- Web Interface ---
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Hand Gesture Servo Control</title>";
  html += "<style>body{font-family:Arial,sans-serif; text-align:center; background:#f4f4f4; color:#333;} ";
  html += ".container{max-width:520px; margin:20px auto; background:white; padding:20px; border-radius:10px; box-shadow:0 0 10px rgba(0,0,0,0.1);} ";
  html += ".mode-toggle{margin:20px 0; padding:15px; border-radius:8px; background:#eef;} ";
  html += ".finger-card{margin:15px 0; padding:15px; background:#f9f9f9; border-radius:8px; border:1px solid #e0e0e0; transition:opacity .2s;} ";
  html += ".disabled{opacity:0.4; pointer-events:none;} ";
  html += ".joint-card{margin:30px 0; padding:20px; background:#fff3e0; border-radius:8px; border:1px solid #ffcc80;} ";
  html += "input[type=range]{width:90%; height:12px; border-radius:5px;} h1{color:#007bff;} ";
  html += ".switch{position:relative; display:inline-block; width:60px; height:30px; vertical-align:middle;} ";
  html += ".switch input{display:none;} ";
  html += ".slider-round{position:absolute; cursor:pointer; top:0; left:0; right:0; bottom:0; background-color:#ccc; transition:.3s; border-radius:30px;} ";
  html += ".slider-round:before{position:absolute; content:''; height:22px; width:22px; left:4px; bottom:4px; background-color:white; transition:.3s; border-radius:50%;} ";
  html += "input:checked + .slider-round{background-color:#007bff;} ";
  html += "input:checked + .slider-round:before{transform:translateX(30px);}</style>";
  html += "</head><body><div class='container'><h1>Hand Gesture Servo Control</h1>";

  // Mode toggle
  html += "<div class='mode-toggle'><h3>Finger Control Source</h3>";
  html += "<p>Serial (MediaPipe) &nbsp;<label class='switch'><input type='checkbox' id='modeSwitch' " + String(manualMode ? "checked" : "") + " onchange='toggleMode(this.checked)'><span class='slider-round'></span></label>&nbsp; Manual (Web)</p>";
  html += "<p id='modeLabel'>Current: <b>" + String(manualMode ? "MANUAL (web control)" : "SERIAL (MediaPipe)") + "</b></p></div>";

  // Finger sliders (0-4)
  const char* fingerNames[5] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};
  html += "<div id='fingerCards'>";
  for (int i = 0; i < NUM_FINGER_SERVOS; i++) {
    html += "<div class='finger-card" + String(manualMode ? "" : " disabled") + "' id='card" + String(i) + "'>";
    html += "<h4>" + String(fingerNames[i]) + ": <span id='val" + String(i) + "'>" + String(fingerAngle[i]) + "</span>&deg;</h4>";
    html += "<input type='range' min='0' max='180' value='" + String(fingerAngle[i]) + "' oninput='sendFinger(" + String(i) + ", this.value)'>";
    html += "</div>";
  }
  html += "</div>";

  // Joint slider - ALWAYS web controlled, independent of mode
  html += "<div class='joint-card'><h3>Joint (channel 6, always web-controlled)</h3>";
  html += "<h4>Angle: <span id='jointVal'>" + String(jointAngle) + "</span>&deg;</h4>";
  html += "<input type='range' min='0' max='180' value='" + String(jointAngle) + "' oninput='sendJoint(this.value)'>";
  html += "</div>";

  html += "</div><script>";
  html += "function toggleMode(isManual) {";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('GET', '/mode?state=' + (isManual ? 'manual' : 'serial'), true);";
  html += "  xhr.onload = function() { location.reload(); };";
  html += "  xhr.send();";
  html += "}";
  html += "function sendFinger(id, val) {";
  html += "  document.getElementById('val' + id).innerText = val;";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('GET', '/moveFinger?id=' + id + '&val=' + val, true);";
  html += "  xhr.send();";
  html += "}";
  html += "function sendJoint(val) {";
  html += "  document.getElementById('jointVal').innerText = val;";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('GET', '/moveJoint?val=' + val, true);";
  html += "  xhr.send();";
  html += "}";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleMode() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    manualMode = (state == "manual");
    server.send(200, "text/plain", "OK");
    return;
  }
  server.send(400, "text/plain", "Bad Request");
}

void handleMoveFinger() {
  if (!manualMode) {
    server.send(403, "text/plain", "Serial mode active - switch to Manual to use web control");
    return;
  }
  if (server.hasArg("id") && server.hasArg("val")) {
    int id = server.arg("id").toInt();
    int val = server.arg("val").toInt();
    applyFingerAngle(id, val);
    server.send(200, "text/plain", "OK");
    return;
  }
  server.send(400, "text/plain", "Bad Request");
}

void handleMoveJoint() {
  // Joint is web-only regardless of mode
  if (server.hasArg("val")) {
    int val = server.arg("val").toInt();
    applyJointAngle(val);
    server.send(200, "text/plain", "OK");
    return;
  }
  server.send(400, "text/plain", "Bad Request");
}

void setup() {
  Serial.begin(115200);

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);

  for (int i = 0; i < NUM_FINGER_SERVOS; i++) {
    setServoAngle(i, fingerAngle[i]);
  }
  setServoAngle(JOINT_SERVO, jointAngle);

  Serial.print("Setting up Access Point...");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.println("Ready!");
  Serial.print("Hotspot IP Address: ");
  Serial.println(IP);

  server.on("/", handleRoot);
  server.on("/mode", handleMode);
  server.on("/moveFinger", handleMoveFinger);
  server.on("/moveJoint", handleMoveJoint);
  server.begin();
}

void loop() {
  server.handleClient();
  readSerialData();   // non-blocking; only acts on data when NOT in manual mode
}
