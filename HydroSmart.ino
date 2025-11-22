#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_BMP280.h>
#include <MPU6050.h>
#include <ESP32Servo.h>   // <-- Use this instead of Servo.h

// === Pin Assignments ===
#define RELAY_PUMP     13
#define RELAY_VALVE    12
#define SERVO_PIN      14
#define BUTTON_PIN     15
#define FLOW_SENSOR    4
#define DHT_PIN        5
#define TRIG_PIN       18
#define ECHO_PIN       19
#define PIR_PIN        23
#define SDA_PIN        21
#define SCL_PIN        22

#define DHTTYPE DHT11

// ==== WiFi Credentials ====
const char* ssid = "vivo Y300 5G";
const char* password = "12345678";

// ==== HTML Frontend ====
const char htmlTemplate[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart Tanker</title>
<style>
body { font-family: 'Poppins', sans-serif; background: radial-gradient(circle at top left, #0f172a, #1e293b, #020617); margin: 0; padding: 0; color: #f1f5f9; }
/* ... (include all your CSS styles here) ... */
header { background: linear-gradient(90deg, #0ea5e9, #38bdf8, #0ea5e9); color: white; text-align: center; padding: 25px 10px; }
.card { background: rgba(30, 41, 59, 0.9); border-radius: 15px; padding: 20px; box-shadow: 0 5px 15px rgba(0,0,0,0.3); }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 25px; margin-bottom: 40px; }
.control-grid button { width: 100%; margin: 8px 0; padding: 12px 20px; font-size: 1em; background-color: #0ea5e9; color: white; border: none; border-radius: 8px; cursor: pointer; }
footer { text-align: center; padding: 20px; font-size: 0.9em; color: #cbd5e1; background: #0f172a; }
</style>
</head>
<body>
<header>
  <h1>Smart Tanker</h1>
  <p>Real-time Monitoring • Automation • Driver Safety</p>
</header>
<div class="container">
  <section class="grid">
    <div class="card"><h3>Temperature</h3><p id="temperature">-</p></div>
    <div class="card"><h3>Pressure</h3><p id="pressure">-</p></div>
    <div class="card"><h3>Water Level</h3><p id="waterLevel">-</p></div>
    <div class="card"><h3>Flow Rate</h3><p id="flowRate">-</p></div>
    <div class="card"><h3>Vibration</h3><p id="vibration">-</p></div>
  </section>
  <section class="grid">
    <div class="card"><h3>Lid Status</h3><p id="lidStatus" class="status">-</p></div>
    <div class="card"><h3>Tap Status</h3><p id="tapStatus" class="status">-</p></div>
    <div class="card"><h3>Flow System</h3><p id="flowSystem" class="status">-</p></div>
    <div class="card"><h3>PIR Sensor</h3><p id="pir">-</p></div>
  </section>
  <section class="grid control-grid">
    <div class="card">
      <h3>Tank Controls</h3>
      <button onclick="sendControl('openLid')">Open Lid</button>
      <button onclick="sendControl('closeLid')">Close Lid</button>
      <button onclick="sendControl('openTap')">Open Tap</button>
      <button onclick="sendControl('closeTap')">Close Tap</button>
      <button onclick="sendControl('toggleFlow')">Toggle Flow Sensor</button>
    </div>
    <div class="card">
      <h3>Pump Controls</h3>
      <button onclick="sendControl('pumpOn')">Pump ON</button>
      <button onclick="sendControl('pumpOff')">Pump OFF</button>
    </div>
  </section>
</div>
<footer>
  &copy; 2025 Smart Tanker | Designed for Safety, Speed & Sustainability
</footer>
<script>
function updateSensors() {
  fetch('/getdata')
    .then(r=>r.json())
    .then(d=>{
      document.getElementById('temperature').textContent = d.temperature + ' °C';
      document.getElementById('pressure').textContent = d.pressure + ' hPa';
      document.getElementById('waterLevel').textContent = d.waterLevel + ' cm';
      document.getElementById('flowRate').textContent = d.flowRate + ' L/min';
      document.getElementById('vibration').textContent = d.vibration + ' g';
      document.getElementById('lidStatus').textContent = d.lidStatus;
      document.getElementById('flowSystem').textContent = d.flowSystem;
      document.getElementById('pir').textContent = d.pir ? 'Motion Detected' : 'No Motion';
    });
}
function sendControl(action) {
  fetch('/control', {
    method: 'POST',
    body: action
  }).then(()=>setTimeout(updateSensors,500));
}
setInterval(updateSensors, 2000);
window.onload = updateSensors;
</script>
</body></html>
)rawliteral";

// ==== Objects and Variables ====
DHT dht(DHT_PIN, DHTTYPE);
Adafruit_BMP280 bmp;
MPU6050 mpu;
Servo lidServo;
WebServer server(80);

volatile unsigned long flowPulseCount = 0;
bool flowSensorEnabled = true;
unsigned long servoLastToggle = 0;

void IRAM_ATTR flowISR() {
  if (flowSensorEnabled) flowPulseCount++;
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PUMP, OUTPUT);    digitalWrite(RELAY_PUMP, LOW);
  pinMode(RELAY_VALVE, OUTPUT);   digitalWrite(RELAY_VALVE, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(FLOW_SENSOR, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);      digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  Wire.begin(SDA_PIN, SCL_PIN);

  dht.begin();
  bmp.begin(0x76);
  mpu.initialize();
  lidServo.attach(SERVO_PIN, 500, 2400); // min/max pulse width for SG90
  lidServo.write(0);

  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR), flowISR, RISING);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected! IP:"); Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/getdata", HTTP_GET, handleData);
  server.on("/control", HTTP_POST, handleControl);
  server.begin();
}

// ==== Sensor and Logic Functions ====
float getUltrasonicCM() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration * 0.034 / 2.0;
}

float getFlowRateLmin() {
  static unsigned long lastRead = 0; static float flowRate = 0;
  if (millis() - lastRead > 1000) {
    flowRate = (flowPulseCount * 60.0) / 7.5;
    flowPulseCount = 0;
    lastRead = millis();
  }
  return flowRate;
}

void processServoAutomation() {
  static bool went = false;
  unsigned long now = millis();
  if (!went && now - servoLastToggle > 60000) {
    lidServo.write(180); went = true; servoLastToggle = now;
  }
  else if (went && now - servoLastToggle > 5000) {
    lidServo.write(0); went = false; servoLastToggle = now;
  }
}

void processButtonToggle() {
  static bool lastButtonState = HIGH;
  bool btn = digitalRead(BUTTON_PIN);
  if (btn == LOW && lastButtonState == HIGH) {
    flowSensorEnabled = !flowSensorEnabled;
    delay(300); // debounce
  }
  lastButtonState = btn;
}

// ==== Web Handlers ====
void handleRoot() {
  server.send(200, "text/html", String(FPSTR(htmlTemplate)));
}
void handleData() {
  float temp = dht.readTemperature();
  float pres = bmp.readPressure() / 100.0;
  float waterLevel = getUltrasonicCM();
  float flow = getFlowRateLmin();
  int16_t ax, ay, az; mpu.getAcceleration(&ax, &ay, &az);
  float vibration = sqrt(ax*ax+ay*ay+az*az)/16384.0;
  bool pir = digitalRead(PIR_PIN);
  String json = "{";
  json += "\"temperature\":" + String(isnan(temp) ? 0 : temp) + ",";
  json += "\"pressure\":" + String(isnan(pres) ? 0 : pres) + ",";
  json += "\"waterLevel\":" + String(waterLevel) + ",";
  json += "\"flowRate\":" + String(flow) + ",";
  json += "\"pir\":" + String(pir ? 1 : 0) + ",";
  json += "\"vibration\":" + String(vibration,3) + ",";
  json += "\"lidStatus\":\"" + String(lidServo.read()>90 ? "Open" : "Closed") + "\",";
  json += "\"flowSystem\":\"" + String(flowSensorEnabled ? "On" : "Off") + "\"";
  json += "}";
  server.send(200, "application/json", json);
}
void handleControl() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "No data"); return; }
  String cmd = server.arg("plain");
  if (cmd == "openLid") lidServo.write(180);
  else if (cmd == "closeLid") lidServo.write(0);
  else if (cmd == "openTap") digitalWrite(RELAY_VALVE, HIGH);
  else if (cmd == "closeTap") digitalWrite(RELAY_VALVE, LOW);
  else if (cmd == "toggleFlow") flowSensorEnabled = !flowSensorEnabled;
  else if (cmd == "pumpOn") digitalWrite(RELAY_PUMP, HIGH);
  else if (cmd == "pumpOff") digitalWrite(RELAY_PUMP, LOW);
  server.send(200, "text/plain", "Done");
}

void loop() {
  server.handleClient();
  processServoAutomation();
  processButtonToggle();
}