/*
 * ═══════════════════════════════════════════════════════════════════════════
 *                    AUTOMATED PLANT WATERING SYSTEM
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * This intelligent watering system monitors environmental conditions and 
 * automatically waters plants based on sensor readings and ML predictions.
 * 
 * HARDWARE COMPONENTS:
 *   • ESP32 microcontroller - Main controller with WiFi capability
 *   • DHT11 sensor - Measures temperature and air humidity
 *   • Capacitive soil moisture sensor - Monitors soil moisture levels
 *   • LDR (Light Dependent Resistor) - Detects ambient light intensity
 *   • Water level sensor - Monitors water reservoir status
 *   • Water pump - Activates watering
 * 
 * FUNCTIONALITY:
 *   The system operates in two modes:
 *   
 *   AUTO MODE:
 *     - Continuously reads sensors (temperature, humidity, light, soil moisture)
 *     - Sends data to Flask ML API for intelligent watering predictions
 *     - Falls back to light-based logic if ML is unavailable
 *     - Automatically activates watering when conditions are met
 *     - Includes safety protection: prevents pump activation if water tank is empty
 *   
 *   MANUAL MODE:
 *     - Allows direct user control via web interface
 *     - Can override automatic decisions
 *     - Still respects water tank safety protection
 * 
 * WEB INTERFACE:
 *   The ESP32 hosts a responsive web dashboard accessible over WiFi that displays:
 *   - Real-time sensor readings (temperature, humidity, light, soil moisture)
 *   - Water tank status
 *   - ML prediction recommendations
 *   - Manual/Auto mode controls
 *   - Current watering state
 * 
 * SAFETY FEATURES:
 *   - Water level monitoring prevents pump dry-running
 *   - Configurable thresholds for all sensors
 *   - Timeout protection for WiFi and API connections
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DHT.h>

// ── SYSTEM CONSTANTS ───────────────────────────────────
const unsigned long serialBaudRate    = 115200;
const unsigned long wifiTimeoutMs     = 20000;
const unsigned long wifiDotIntervalMs = 300;
const int           adcMin            = 0;
const int           adcMax            = 4095;
const int           percentMin        = 0;
const int           percentMax        = 100;
const int           lumOffset         = 500;
// ───────────────────────────────────────────────────────

// ── Wi-Fi CONFIG ───────────────────────────────────────
const char*         wifiSsid      = "DIGI-5yX2";
const char*         wifiPassword  = "tuCEuCYa9H";
const uint16_t      serverPort    = 8080;
// ───────────────────────────────────────────────────────

// ── Flask API (ML) ─────────────────────────────────────
const char*         apiHost             = "192.168.1.135";
const uint16_t      apiPort             = 5000;
const char*         apiPredictPath      = "/predict";
const char*         keyShouldWater      = "should_water";
const unsigned long predictIntervalMs   = 5000;
// ───────────────────────────────────────────────────────

// ── ESP32 PINS ─────────────────────────────────────────
const int ldrPin    = 34;   // ADC1_CH6 (INPUT ONLY) - at LDR+10k junction
const int soilPin   = 35;   // ADC1_CH7 (INPUT ONLY) - AO soil sensor (capacitive)
const int waterPin  = 36;   // ADC1_CH0 (INPUT ONLY) - AO water level sensor (red board)
const int outPin    = 25;   // Controlled output (water pump)
const int dhtPin    = 4;    // DHT11 DATA
// ───────────────────────────────────────────────────────

#define DHTTYPE DHT11
DHT dht(dhtPin, DHTTYPE);

// ── SENSOR CALIBRATION ─────────────────────────────────
int ldrThresh   = 800;   // LDR threshold for AUTO mode (0..4095)
int soilDry     = 3000;  // RAW value when soil is dry (air)
int soilWet     = 1200;  // RAW value when soil is very wet
int waterThresh = 300;   // Water level threshold (below = empty)
// ───────────────────────────────────────────────────────

// State and measured values
volatile bool manualOverride = false; // false = AUTO, true = MANUAL
volatile bool manualState    = false; // desired state in MANUAL (OFF/ON)

int   ldrValue      = 0;
int   soilRaw       = 0;   // 0..4095
int   soilPercent   = 0;   // 0..100 (0 dry, 100 wet)
float humidity      = NAN; // %RH (from DHT11)
float temperature   = NAN; // °C   (from DHT11)

int   waterRaw      = 0;   // 0..4095
bool  waterPresent  = false; // true = sufficient water in reservoir

// ML/API state
bool   mlHaveDecision = false;
bool   mlShouldWater  = false;
bool   mlOk           = false;
String mlMsg          = "";
unsigned long lastPredictMs = 0;

// HTTP server on custom port
WebServer server(serverPort);

// Time for periodic readings
unsigned long lastReadMs = 0;
const unsigned long readIntervalMs = 1000;

// ── Utility Functions ──────────────────────────────────────
void readSensors() {
  // LDR
  ldrValue    = analogRead(ldrPin);         // 0..4095 (12-bit)

  // DHT11
  humidity    = dht.readHumidity();          // %RH (can be NAN)
  temperature = dht.readTemperature();       // °C (can be NAN)

  // Soil moisture (capacitive)
  soilRaw = analogRead(soilPin);            // 0..4095
  // Map to percent: 0% = dry (soilDry), 100% = wet (soilWet)
  soilPercent = map(soilRaw, soilDry, soilWet, percentMin, percentMax);
  soilPercent = constrain(soilPercent, percentMin, percentMax);

  // Water level (water sensor)
  waterRaw = analogRead(waterPin);          // 0..4095
  // New logic: BELOW waterThresh => false (too low/empty), OTHERWISE => true
  waterPresent = (waterRaw <= waterThresh);
}

// Simple boolean search in a small JSON (without ArduinoJson)
bool jsonFindBool(const String& body, const char* key, bool& out) {
  int kpos = body.indexOf(String("\"") + key + "\"");
  if (kpos < 0) return false;
  int colon = body.indexOf(':', kpos);
  if (colon < 0) return false;
  int p = colon + 1;
  while (p < (int)body.length() && (body[p] == ' ' || body[p] == '\t' || body[p] == '\r' || body[p] == '\n' || body[p] == '\"')) p++;
  if (p >= (int)body.length()) return false;

  if (body.startsWith("true", p))  { out = true;  return true; }
  if (body.startsWith("false", p)) { out = false; return true; }
  if (body[p] == '1') { out = true;  return true; }
  if (body[p] == '0') { out = false; return true; }
  return false;
}

bool parseShouldWaterFromBody(const String& body, bool& shouldWater) {
  return jsonFindBool(body, keyShouldWater, shouldWater);
}

// API /predict call; returns true if we received valid decision
bool callPredict(float tempC, float humRH, int lum, int soilPct, bool& shouldWater, String& infoMsg) {
    const int statusOK = 200;
    if (WiFi.status() != WL_CONNECTED) {
    infoMsg = "WiFi disconnected";
    return false;
  }

  if (isnan(tempC)) {
    infoMsg = "Invalid temp (DHT)";
    return false;
  }

  HTTPClient http;
  String url = String("http://") + apiHost + ":" + apiPort + apiPredictPath;

  String humField = isnan(humRH) ? String("null") : String((int)round(humRH));
  String payload = String("{\"temperature\":") + String(tempC, 1) +
                   ",\"air_humidity\":" + String(soilPct) +
                   ",\"luminosity\":" + String(lum - lumOffset) +
                   ",\"soil_humidity\":" + String(soilPct) +
                   "}";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(payload);
  String resp = http.getString();
  http.end();

  if (code != statusOK) {
    infoMsg = "HTTP " + String(code);
    return false;
  }

  bool sw = false;
  if (!parseShouldWaterFromBody(resp, sw)) {
    infoMsg = "Response without \"" + String(keyShouldWater) + "\"";
    return false;
  }

  shouldWater = sw;
  infoMsg = "OK";
  return true;
}

bool computeAutoDecision() {
  if (mlHaveDecision && mlOk) {
    return mlShouldWater;
  }
  bool outOn = (ldrValue < ldrThresh);
  return outOn;
}

void applyOutputLogic() {
  // Hard protection: if no water, pump stays off regardless of mode
  if (!waterPresent) {                     // <<< safety guard
    digitalWrite(outPin, LOW);
    return;
  }

  if (manualOverride) {
    digitalWrite(outPin, manualState ? HIGH : LOW);
  } else {
    bool outOn = computeAutoDecision();
    digitalWrite(outPin, outOn ? HIGH : LOW);
  }
}

String htmlPage() {
  String ip = WiFi.localIP().toString();
  String port = String(serverPort);

  String page = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>Watering System</title>
  <style>
    :root{color-scheme: light dark}
    body{font-family:system-ui,Arial,sans-serif;margin:0;padding:1rem;background:#0f172a;color:#e2e8f0}
    .card{max-width:900px;margin:auto;background:#111827;border:1px solid #334155;border-radius:12px;padding:1rem}
    h1{font-size:1.4rem;margin:.25rem 0 .5rem}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin:.75rem 0}
    .box{background:#0b1220;border:1px solid #23314f;border-radius:10px;padding:.8rem}
    .muted{color:#94a3b8}
    .row{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap;margin:.5rem 0}
    button{cursor:pointer;background:#0ea5e9;color:#fff;border:none;padding:.55rem .9rem;border-radius:8px}
    button.red{background:#ef4444}
    input[type=number]{width:110px;padding:.35rem .45rem;border-radius:8px;border:1px solid #334155;background:#0b1220;color:#e2e8f0}
    code{background:#0b1020;color:#eab308;padding:.15rem .35rem;border-radius:6px}
    a{color:#38bdf8}
    .ok{color:#22c55e}
    .bad{color:#ef4444}
  </style>
</head>
<body>
  <div class="card">
    <h1>MotoFlowers</h1>

    <div class="grid">
      <div class="box">
        <div class="muted">Light</div>
        <div id="ldr" style="font-size:1.6rem">-</div>
      </div>
      <div class="box">
        <div class="muted">Temperature (°C)</div>
        <div id="temp" style="font-size:1.6rem">-</div>
      </div>
      <div class="box">
        <div class="muted">Air Humidity (%)</div>
        <div id="hum" style="font-size:1.6rem">-</div>
      </div>
      <div class="box">
        <div class="muted">Soil Humidity (%)</div>
        <div id="soil" style="font-size:1.6rem">-</div>
      </div>
      <div class="box">
        <div class="muted">Watering</div>
        <div id="out" style="font-size:1.6rem">-</div>
      </div>
      <!-- Water tank card -->
      <div class="box">
        <div class="muted">Water Tank</div>
        <div id="tank" style="font-size:1.4rem">-</div>
        <div id="tankraw" class="muted" style="font-size:.9rem;margin-top:.25rem">–</div>
      </div>
      <div class="box">
        <div class="muted">Suggestion</div>
        <div id="ml" style="font-size:1.1rem">-</div>
        <div id="mlmsg" class="muted" style="font-size:.9rem;margin-top:.25rem">–</div>
      </div>
    </div>

    <div class="box">
      <div class="muted">Control</div>
      <div class="row">
        <button id="autoBtn" class="">Auto</button>
        <button id="manualBtn" class="">Manual</button>
        <button id="onBtn" class="">ON</button>
        <button id="offBtn" class="red">OFF</button>
      </div>
      <div class="muted" id="modeTxt">-</div>
    </div>
  </div>

<script>
async function getStatus(){
  const res = await fetch('/status');
  if(!res.ok) return;
  const d = await res.json();
  document.getElementById('ldr').textContent  = d.ldr;
  document.getElementById('temp').textContent = isNaN(d.temp)? 'nan' : d.temp.toFixed(1);
  document.getElementById('hum').textContent  = isNaN(d.hum)? 'nan' : d.hum.toFixed(0);
  document.getElementById('soil').textContent = (typeof d.soil === 'number') ? (d.soil + '%') : 'nan';
  document.getElementById('out').textContent  = d.out?'ON':'OFF';
  
  document.getElementById('modeTxt').textContent = 'Mode: ' + (d.manual?'MANUAL':'AUTO');

  // Display water tank
  const tank = document.getElementById('tank');
  const tankRaw = document.getElementById('tankraw');
  if (d.water === true) {
    tank.innerHTML = '<span class="ok">OK (has water)</span>';
  } else if (d.water === false) {
    tank.innerHTML = '<span class="bad">EMPTY</span>';
  } else {
    tank.textContent = '-';
  }
  tankRaw.textContent = (typeof d.water_raw === 'number') ? ('raw=' + d.water_raw) : 'raw=–';

  const mlBox = document.getElementById('ml');
  const mlMsg = document.getElementById('mlmsg');
  if (d.ml_ok) {
    mlBox.innerHTML = '<span class="ok">prediction: ' + (d.ml_should ? 'WATER' : 'DON\'T WATER') + '</span>';
  } else {
    mlBox.innerHTML = '<span class="bad">without prediction</span>';
  }
  mlMsg.textContent = d.ml_msg || '-';
}

async function setParams(params){
  const res = await fetch('/set?'+new URLSearchParams(params).toString());
  if(res.ok){ getStatus(); }
}

document.getElementById('autoBtn').onclick   = () => setParams({manual:0});
document.getElementById('manualBtn').onclick = () => setParams({manual:1});
document.getElementById('onBtn').onclick     = () => setParams({manual:1,state:1});
document.getElementById('offBtn').onclick    = () => setParams({manual:1,state:0});

getStatus();
setInterval(getStatus, 1000);
</script>
</body>
</html>)HTML";
  return page;
}

void handleRoot() {
  const int statusOk = 200;
  server.send(statusOk, "text/html; charset=utf-8", htmlPage());
}

void handleStatus() {
  // Determine current pin state
  bool outState = (digitalRead(outPin) == HIGH);

  // JSON status (includes ML/API info + soil + water tank)
  String json = String("{\"ldr\":") + ldrValue +
                ",\"temp\":" + (isnan(temperature) ? String("NaN") : String(temperature, 1)) +
                ",\"hum\":"  + (isnan(humidity)    ? String("NaN") : String(humidity, 0)) +
                ",\"soil\":" + String(soilPercent) +
                ",\"soil_raw\":" + String(soilRaw) +
                ",\"water\":" + (waterPresent ? "true" : "false") +
                ",\"water_raw\":" + String(waterRaw) +
                ",\"out\":"  + (outState ? "true" : "false") +
                ",\"manual\":"+ (manualOverride ? "true" : "false") +
                ",\"thresh\":" + String(ldrThresh) +
                ",\"ml_ok\":" + String(mlOk ? "true":"false") +
                ",\"ml_should\":" + String(mlShouldWater ? "true":"false") +
                ",\"ml_msg\":\"" + mlMsg + "\"" +
                "}";
  server.send(statusOk, "application/json; charset=utf-8", json);
}

void handleSet() {
  // /set?manual=0/1&state=0/1&thresh=NNN
  if (server.hasArg("manual")) {
    manualOverride = (server.arg("manual") == "1");
  }
  if (server.hasArg("state")) {
    manualState = (server.arg("state") == "1");
  }
  if (server.hasArg("thresh")) {
    int t = server.arg("thresh").toInt();
    if (t < adcMin) t = adcMin;
    if (t > adcMax) t = adcMax;
    ldrThresh = t;
  }
  // Apply logic immediately
  applyOutputLogic();
  handleStatus();
}

void connectToWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(wifiSsid);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(wifiSsid, wifiPassword);

  unsigned long start = millis();
  unsigned long lastDot = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < wifiTimeoutMs) {
    if (millis() - lastDot >= wifiDotIntervalMs) {
      Serial.print('.');
      lastDot = millis();
    }
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] NOT connected (timeout). Check SSID/password/2.4GHz band.");
  }
}

void setup() {
  Serial.begin(serialBaudRate);

  pinMode(outPin, OUTPUT);
  digitalWrite(outPin, LOW);

  dht.begin();
  connectToWiFi();

  // HTTP routes
  server.on("/",       HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/set",    HTTP_GET, handleSet);
  server.begin();
  Serial.print("[HTTP] Server started on http://");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(serverPort);
}

void loop() {
  server.handleClient();

  // Periodic sensor reading
  unsigned long now = millis();
  if (now - lastReadMs >= readIntervalMs) {
    lastReadMs = now;
    readSensors();

    // In AUTO mode request prediction at interval; in MANUAL mode DON'T
    if (!manualOverride && (now - lastPredictMs >= predictIntervalMs)) {
      lastPredictMs = now;
      bool sw = false;
      String info;
      bool ok = callPredict(temperature, humidity, ldrValue, soilPercent, sw, info);
      mlOk = ok;
      mlMsg = info;
      if (ok) {
        mlHaveDecision = true;
        mlShouldWater = sw;
      }
    }

    // Apply logic (ML or fallback) with water protection in applyOutputLogic()
    applyOutputLogic();

    // Compact serial log
    Serial.print("LDR=");
    Serial.print(ldrValue);
    Serial.print("  Temp=");
    if (isnan(temperature)) Serial.print("nan"); else Serial.print(temperature, 1);
    Serial.print("C  HumAir=");
    if (isnan(humidity)) Serial.print("nan"); else Serial.print(humidity, 0);
    Serial.print("%  Soil=");
    Serial.print(soilPercent);
    Serial.print("% (raw=");
    Serial.print(soilRaw);
    Serial.print(")  WATER=");
    Serial.print(waterPresent ? "OK" : "EMPTY");
    Serial.print(" (raw=");
    Serial.print(waterRaw);
    Serial.print(")  OUT=");
    Serial.print((digitalRead(outPin)==HIGH)?"HIGH":"LOW");
    Serial.print("  MODE=");
    Serial.print(manualOverride ? "MANUAL" : "AUTO");
    Serial.print("  ML=");
    Serial.print(mlOk ? (mlShouldWater ? "WATER" : "DON'T WATER") : "NONE");
    Serial.print("  MSG=");
    Serial.println(mlMsg);
  }
}