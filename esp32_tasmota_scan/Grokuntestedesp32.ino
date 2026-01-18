#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

// ── WiFi credentials ────────────────────────────────────────────────
const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// ── Network configuration (ADJUST TO YOUR ROUTER!) ──────────────────
IPAddress local_IP;               // Will be read from WiFi
String     subnetBase   = "192.168.1.";   // Change to your subnet, e.g. "192.168.178."
int        startOctet   = 100;            // Start scanning from .100
int        endOctet     = 150;            // To .150 (51 IPs → fast) — increase to 254 for full scan

// ── Web server & websocket ──────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// List of discovered Tasmota devices (IPs)
std::vector<String> tasmotaIPs;

// HTML page (simple websocket + scan button + toggle)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Tasmota Lights Control</title>
  <style>
    body {font-family:Arial; text-align:center; background:#111; color:#eee;}
    h1 {color:#0f8;}
    .device {margin:12px; padding:12px; background:#222; border-radius:8px;}
    .on  {background:#4CAF50 !important;}
    .off {background:#f44336 !important;}
    button {padding:10px 24px; font-size:18px; margin:6px; border:none; border-radius:6px; cursor:pointer;}
    button.scan {background:#2196F3; color:white;}
  </style>
</head>
<body>
  <h1>Tasmota Lights Controller</h1>
  <button class="scan" onclick="scan()">Scan Network</button>
  <div id="devices"></div>

  <script>
    var ws = new WebSocket("ws://" + location.hostname + "/ws");
    ws.onmessage = function(event) {
      let data = JSON.parse(event.data);
      if (data.type === "devices") {
        let html = "";
        data.list.forEach(d => {
          let stateClass = d.state.includes("ON") ? "on" : "off";
          html += `<div class="device ${stateClass}">
            <strong>${d.ip}</strong><br>
            State: ${d.state}<br>
            <button onclick="toggle('${d.ip}')">Toggle</button>
          </div>`;
        });
        document.getElementById("devices").innerHTML = html || "<p>No Tasmota devices found yet...</p>";
      }
    };

    function toggle(ip) {
      ws.send(JSON.stringify({action:"toggle", ip:ip}));
    }
    function scan() {
      ws.send(JSON.stringify({action:"scan"}));
      document.getElementById("devices").innerHTML = "<p>Scanning... please wait (10–60s)</p>";
    }
  </script>
</body>
</html>
)rawliteral";

// Check if IP looks like Tasmota (fast probe)
bool isTasmotaDevice(const String& ip) {
  HTTPClient http;
  String url = "http://" + ip + "/cm?cmnd=Status%200";
  http.setTimeout(1200);           // 1.2s timeout per device
  http.begin(url);
  int code = http.GET();
  if (code <= 0 || code != 200) {
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  // Quick & dirty identification
  if (payload.indexOf("Tasmota") >= 0 ||
      payload.indexOf("\"StatusFWR\"") >= 0 ||
      payload.indexOf("\"POWER\"") >= 0) {
    return true;
  }
  return false;
}

// Get current POWER state (usually "ON"/"OFF" or {"POWER":"ON"})
String getPowerState(const String& ip) {
  HTTPClient http;
  String url = "http://" + ip + "/cm?cmnd=Power";
  http.setTimeout(800);
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String p = http.getString();
    if (p.indexOf("ON") >= 0) return "ON";
    if (p.indexOf("OFF") >= 0) return "OFF";
    return p;  // raw if complex
  }
  http.end();
  return "ERR";
}

// Toggle power
void togglePower(const String& ip) {
  HTTPClient http;
  String url = "http://" + ip + "/cm?cmnd=Power%20Toggle";
  http.begin(url);
  http.GET();
  http.end();
}

// Scan subnet for Tasmota devices
void scanNetwork() {
  tasmotaIPs.clear();
  Serial.println("Starting network scan...");

  for (int i = startOctet; i <= endOctet; i++) {
    String ip = subnetBase + String(i);
    if (isTasmotaDevice(ip)) {
      tasmotaIPs.push_back(ip);
      Serial.printf("Found Tasmota → %s\n", ip.c_str());
    }
    yield();  // prevent watchdog
  }
  Serial.println("Scan finished.");
  broadcastDevices();
}

// Send current list + states to all websocket clients
void broadcastDevices() {
  DynamicJsonDocument doc(2048);
  JsonArray list = doc.createNestedArray("list");
  for (const auto& ip : tasmotaIPs) {
    JsonObject dev = list.createNestedObject();
    dev["ip"]   = ip;
    dev["state"] = getPowerState(ip);
  }
  doc["type"] = "devices";

  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

// ── WebSocket events ────────────────────────────────────────────────
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client,
               AwsEventType type, void * arg, uint8_t *data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    client->text("{\"type\":\"info\",\"msg\":\"Connected\"}");
    broadcastDevices();           // send current list on connect
  }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      String msg = (char*)data;

      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, msg);
      if (err) return;

      String action = doc["action"] | "";
      if (action == "scan") {
        scanNetwork();
      }
      else if (action == "toggle") {
        String ip = doc["ip"] | "";
        if (ip.length() > 0) {
          togglePower(ip);
          delay(400);               // small delay for state to settle
          broadcastDevices();
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected → " + WiFi.localIP().toString());

  local_IP = WiFi.localIP();
  subnetBase = String(local_IP[0]) + "." + String(local_IP[1]) + "." + String(local_IP[2]) + ".";

  // Optional: try to guess a good range from your own IP
  int myLast = local_IP[3];
  startOctet = max(2, myLast - 60);
  endOctet   = min(254, myLast + 60);

  // Web server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.begin();

  // Optional: initial scan at boot
  // scanNetwork();
}

void loop() {
  ws.cleanupClients();
  delay(50);
}
