#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ── Configuration ────────────────────────────────────────────────
const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// Network configuration
IPAddress local_IP;
String subnetBase = "192.168.1.";
int startOctet   = 100;
int endOctet     = 150;

// Parallel scanning settings
#define MAX_PARALLEL_SCANS 8      // Number of simultaneous scans
#define SCAN_TIMEOUT_MS 800       // Timeout per device (reduced from 1200)
#define TASMOTA_IDENTIFIER "Tasmota"  // Quick identifier

// ── Global variables ─────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

std::vector<String> tasmotaIPs;
SemaphoreHandle_t ipListMutex;    // Mutex for thread-safe access
TaskHandle_t scanTaskHandle = NULL;
bool isScanning = false;

// HTML page (same as before, but with progress)
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
    button.scan:disabled {background:#666; cursor:not-allowed;}
    #progress {margin:20px; padding:10px; background:#333; border-radius:5px;}
    .progress-bar {width:100%; height:20px; background:#444; border-radius:10px; overflow:hidden;}
    .progress-fill {height:100%; background:#0f8; width:0%; transition:width 0.3s;}
  </style>
</head>
<body>
  <h1>Tasmota Lights Controller</h1>
  <button class="scan" onclick="scan()" id="scanBtn">Scan Network</button>
  <div id="progress" style="display:none;">
    <p>Scanning: <span id="currentIP">-</span></p>
    <div class="progress-bar">
      <div class="progress-fill" id="progressFill"></div>
    </div>
    <p id="foundCount">Found: 0 devices</p>
  </div>
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
        document.getElementById("devices").innerHTML = html || "<p>No Tasmota devices found</p>";
        document.getElementById("scanBtn").disabled = false;
        document.getElementById("progress").style.display = "none";
      }
      else if (data.type === "scan_progress") {
        document.getElementById("progress").style.display = "block";
        document.getElementById("currentIP").textContent = data.current_ip;
        document.getElementById("progressFill").style.width = data.progress + "%";
        document.getElementById("foundCount").textContent = "Found: " + data.found + " devices";
      }
      else if (data.type === "scan_start") {
        document.getElementById("scanBtn").disabled = true;
        document.getElementById("devices").innerHTML = "<p>Scanning... (Parallel scan active)</p>";
      }
    };

    function toggle(ip) {
      ws.send(JSON.stringify({action:"toggle", ip:ip}));
    }
    
    function scan() {
      ws.send(JSON.stringify({action:"scan"}));
      document.getElementById("scanBtn").disabled = true;
    }
  </script>
</body>
</html>
)rawliteral";

// ── Fast Tasmota Detection Function ──────────────────────────────
bool checkTasmota(const String& ip) {
  WiFiClient client;
  client.setTimeout(SCAN_TIMEOUT_MS / 1000);  // Convert to seconds
  
  // Try to connect with short timeout
  if (!client.connect(ip.c_str(), 80, SCAN_TIMEOUT_MS)) {
    return false;
  }
  
  // Send minimal HTTP request
  String request = String("GET /cm?cmnd=Status%200 HTTP/1.1\r\n") +
                   "Host: " + ip + "\r\n" +
                   "User-Agent: ESP32-Tasmota-Scanner\r\n" +
                   "Connection: close\r\n\r\n";
  
  client.print(request);
  
  // Wait for response with timeout
  unsigned long start = millis();
  while (!client.available() && (millis() - start < SCAN_TIMEOUT_MS)) {
    delay(1);
  }
  
  if (!client.available()) {
    client.stop();
    return false;
  }
  
  // Read first part of response (enough to identify)
  String response = client.readStringUntil('\n');
  client.stop();
  
  // Quick check for Tasmota signatures
  return (response.indexOf(TASMOTA_IDENTIFIER) >= 0 ||
          response.indexOf("Status") >= 0 ||
          response.indexOf("POWER") >= 0);
}

// ── Task Function for Parallel Scanning ──────────────────────────
void parallelScanTask(void* parameter) {
  isScanning = true;
  
  // Clear previous results
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    tasmotaIPs.clear();
    xSemaphoreGive(ipListMutex);
  }
  
  int totalIPs = endOctet - startOctet + 1;
  int ipsPerTask = ceil((float)totalIPs / MAX_PARALLEL_SCANS);
  
  // Array to hold task handles
  TaskHandle_t taskHandles[MAX_PARALLEL_SCANS];
  
  // Create scanning tasks
  for (int taskNum = 0; taskNum < MAX_PARALLEL_SCANS; taskNum++) {
    int taskStart = startOctet + (taskNum * ipsPerTask);
    int taskEnd = min(startOctet + ((taskNum + 1) * ipsPerTask) - 1, endOctet);
    
    if (taskStart > endOctet) break;
    
    // Pass scan range to task
    int* range = new int[2];
    range[0] = taskStart;
    range[1] = taskEnd;
    
    char taskName[20];
    snprintf(taskName, sizeof(taskName), "ScanTask%d", taskNum);
    
    xTaskCreatePinnedToCore(
      scanRangeTask,          // Task function
      taskName,               // Name
      4096,                   // Stack size
      (void*)range,           // Parameter
      1,                      // Priority
      &taskHandles[taskNum],  // Task handle
      0                       // Core (0 or 1)
    );
  }
  
  // Monitor progress and send updates
  int scanned = 0;
  while (scanned < totalIPs) {
    delay(100);
    
    // Calculate progress
    scanned = 0;
    for (int i = startOctet; i <= endOctet; i++) {
      // This is simplified - you'd need to track actual progress
      scanned++;
    }
    
    float progress = (float)scanned / totalIPs * 100;
    
    // Send progress update
    DynamicJsonDocument doc(256);
    doc["type"] = "scan_progress";
    doc["current_ip"] = String(subnetBase + String(min(startOctet + scanned, endOctet)));
    doc["progress"] = progress;
    
    xSemaphoreTake(ipListMutex, portMAX_DELAY);
    doc["found"] = tasmotaIPs.size();
    xSemaphoreGive(ipListMutex);
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
    
    // Check if all tasks are done
    bool allDone = true;
    for (int i = 0; i < MAX_PARALLEL_SCANS; i++) {
      if (taskHandles[i] != NULL && eTaskGetState(taskHandles[i]) != eDeleted) {
        allDone = false;
        break;
      }
    }
    
    if (allDone) break;
  }
  
  // Clean up
  for (int i = 0; i < MAX_PARALLEL_SCANS; i++) {
    if (taskHandles[i] != NULL) {
      vTaskDelete(taskHandles[i]);
    }
  }
  
  isScanning = false;
  broadcastDevices();
  vTaskDelete(NULL);
}

// ── Task to scan a specific IP range ─────────────────────────────
void scanRangeTask(void* parameter) {
  int* range = (int*)parameter;
  int start = range[0];
  int end = range[1];
  
  for (int i = start; i <= end; i++) {
    String ip = subnetBase + String(i);
    
    if (checkTasmota(ip)) {
      if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
        tasmotaIPs.push_back(ip);
        xSemaphoreGive(ipListMutex);
        Serial.printf("[Parallel] Found Tasmota at: %s\n", ip.c_str());
      }
    }
    
    // Small delay to prevent overwhelming
    delay(10);
  }
  
  delete[] range;
  vTaskDelete(NULL);
}

// ── Optimized Device Functions ───────────────────────────────────
String getPowerState(const String& ip) {
  HTTPClient http;
  http.setTimeout(500);
  http.setReuse(true);
  
  String url = "http://" + ip + "/cm?cmnd=Power";
  http.begin(url);
  
  int code = http.GET();
  if (code == 200) {
    String response = http.getString();
    http.end();
    
    // Quick parsing
    if (response.indexOf("\"ON\"") > 0) return "ON";
    if (response.indexOf("\"OFF\"") > 0) return "OFF";
    if (response.indexOf("ON") > 0 && response.indexOf("OFF") == -1) return "ON";
    if (response.indexOf("OFF") > 0 && response.indexOf("ON") == -1) return "OFF";
    
    return response.substring(0, 30);  // Return truncated response
  }
  
  http.end();
  return "ERROR";
}

void togglePower(const String& ip) {
  HTTPClient http;
  http.setTimeout(300);
  http.setReuse(true);
  
  String url = "http://" + ip + "/cm?cmnd=Power%20Toggle";
  http.begin(url);
  http.GET();
  http.end();
}

// ── Broadcast Devices ────────────────────────────────────────────
void broadcastDevices() {
  DynamicJsonDocument doc(4096);
  JsonArray list = doc.createNestedArray("list");
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (const auto& ip : tasmotaIPs) {
      JsonObject dev = list.createNestedObject();
      dev["ip"] = ip;
      dev["state"] = getPowerState(ip);
    }
    xSemaphoreGive(ipListMutex);
  }
  
  doc["type"] = "devices";
  
  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

// ── WebSocket Events ─────────────────────────────────────────────
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  
  if (type == WS_EVT_CONNECT) {
    client->text("{\"type\":\"info\",\"msg\":\"Connected\"}");
    broadcastDevices();
  }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      String msg = (char*)data;
      
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, msg);
      if (err) return;
      
      String action = doc["action"] | "";
      
      if (action == "scan") {
        if (!isScanning) {
          // Notify scan start
          DynamicJsonDocument startDoc(128);
          startDoc["type"] = "scan_start";
          String startJson;
          serializeJson(startDoc, startJson);
          ws.textAll(startJson);
          
          // Start parallel scan
          xTaskCreatePinnedToCore(
            parallelScanTask,
            "ParallelScan",
            8192,
            NULL,
            1,
            &scanTaskHandle,
            1
          );
        }
      }
      else if (action == "toggle") {
        String ip = doc["ip"] | "";
        if (ip.length() > 0) {
          togglePower(ip);
          delay(200);
          broadcastDevices();
        }
      }
    }
  }
}

// ── Setup ────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  
  // Create mutex for thread-safe access
  ipListMutex = xSemaphoreCreateMutex();
  
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected → " + WiFi.localIP().toString());
  
  // Configure network settings
  local_IP = WiFi.localIP();
  subnetBase = String(local_IP[0]) + "." + 
               String(local_IP[1]) + "." + 
               String(local_IP[2]) + ".";
  
  // Auto-configure scan range based on local IP
  int myLast = local_IP[3];
  startOctet = max(2, myLast - 30);    // Smaller range for faster parallel scan
  endOctet   = min(254, myLast + 30);
  
  Serial.printf("Scan range: %s%d to %s%d\n", 
                subnetBase.c_str(), startOctet, 
                subnetBase.c_str(), endOctet);
  
  // Web server setup
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", index_html);
  });
  
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
  
  Serial.println("HTTP server started");
}

// ── Loop ─────────────────────────────────────────────────────────
void loop() {
  ws.cleanupClients();
  delay(10);
}
