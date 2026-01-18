#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <math.h>
#include <esp_task_wdt.h>

// ── Configuration ────────────────────────────────────────────────
// WiFi credentials
const char* ssid = "wifi";
const char* password = "pass";

// Network configuration
IPAddress local_IP;
String subnetBase = "10.10.100.";
int startOctet   = 100;
int endOctet     = 140;

// Parallel scanning settings
#define MAX_PARALLEL_SCANS 8      // Reduced from 4 for better stability
#define SCAN_TIMEOUT_MS 1000      // Increased timeout
#define TASMOTA_IDENTIFIER "Tasmota"

// ── Global variables ─────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

struct DeviceInfo {
  String ip;
  String deviceName;
  String friendlyName;
};

std::vector<DeviceInfo> tasmotaDevices;
std::vector<uint32_t> webClients;  // Store legitimate web client IDs
SemaphoreHandle_t ipListMutex;
SemaphoreHandle_t clientListMutex;
TaskHandle_t scanTaskHandle = NULL;
bool isScanning = false;
int totalScannedIPs = 0;

// ── Forward Declarations ────────────────────────────────────────
void scanRangeTask(void* parameter);
void broadcastDevices();
void broadcastToWebClients(const String& message);

// HTML page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Tasmota Lights Control</title>
  <style>
    body {font-family:Arial; text-align:center; background:#111; color:#eee; padding:10px;}
    h1 {color:#0f8; margin:10px 0;}
    .devices-grid {
      display:grid;
      grid-template-columns:repeat(auto-fill, minmax(200px, 1fr));
      gap:10px;
      max-width:1200px;
      margin:0 auto;
    }
    .device {
      padding:8px;
      background:#222;
      border-radius:6px;
      display:flex;
      flex-direction:column;
      align-items:center;
    }
    .device-header {
      width:100%;
      padding:6px;
      border-radius:4px 4px 0 0;
      margin:-8px -8px 6px -8px;
      font-weight:bold;
      font-size:14px;
      text-align:center;
    }
    .on .device-header {background:#4CAF50;}
    .off .device-header {background:#f44336;}
    .device-name {font-size:13px; margin:2px 0; color:#fff;}
    .device-ip {font-size:11px; margin:2px 0; color:#aaa;}
    button {padding:6px 16px; font-size:14px; margin:4px; border:none; border-radius:4px; cursor:pointer;}
    button.scan {background:#2196F3; color:white; padding:8px 20px; font-size:16px;}
    button.scan:disabled {background:#666; cursor:not-allowed;}
    button.toggle {width:100%; background:#555; color:white;}
    button.toggle:hover {background:#777;}
    #progress {margin:15px; padding:10px; background:#333; border-radius:5px;}
    .progress-bar {width:100%; height:18px; background:#444; border-radius:10px; overflow:hidden;}
    .progress-fill {height:100%; background:#0f8; width:0%; transition:width 0.3s;}
    #progress p {margin:8px 0; font-size:14px;}
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
    <p id="statusMsg">Status: Idle</p>
  </div>
  <div class="devices-grid" id="devices"></div>

  <script>
    var ws;
    var wsConnected = false;
    
    function connectWebSocket() {
      console.log("Attempting WebSocket connection");
      ws = new WebSocket("ws://" + location.hostname + "/ws");
      
      ws.onopen = function() {
        console.log("WebSocket connected");
        wsConnected = true;
        document.getElementById("scanBtn").disabled = false;
      };
      
      ws.onmessage = function(event) {
        console.log("Received:", event.data);
        var data = JSON.parse(event.data);
        
        if (data.type === "devices") {
          var html = "";
          for (var i = 0; i < data.list.length; i++) {
            var d = data.list[i];
            var stateClass = d.state.indexOf("ON") >= 0 ? "on" : "off";
            var deviceName = d.devicename || "Unknown";
            var friendlyName = d.friendlyname || deviceName;
            
            html += "<div class='device " + stateClass + "' id='device-" + d.ip.replace(/\./g, '-') + "'>";
            html += "<div class='device-header'>" + friendlyName + "</div>";
            html += "<div class='device-name'>" + deviceName + "</div>";
            html += "<div class='device-ip'>" + d.ip + "</div>";
            html += "<button class='toggle' onclick=\"toggle('" + d.ip + "')\">Toggle</button>";
            html += "</div>";
          }
          document.getElementById("devices").innerHTML = html || "<p>No Tasmota devices found</p>";
          document.getElementById("scanBtn").disabled = false;
          document.getElementById("progress").style.display = "none";
          document.getElementById("statusMsg").textContent = "Status: Scan complete";
        }
        else if (data.type === "device_update") {
          // Fast update for single device
          var deviceId = "device-" + data.ip.replace(/\./g, '-');
          var deviceDiv = document.getElementById(deviceId);
          if (deviceDiv) {
            var stateClass = data.state.indexOf("ON") >= 0 ? "on" : "off";
            deviceDiv.className = "device " + stateClass;
          }
        }
        else if (data.type === "scan_progress") {
          document.getElementById("progress").style.display = "block";
          document.getElementById("currentIP").textContent = data.current_ip;
          document.getElementById("progressFill").style.width = data.progress + "%";
          document.getElementById("foundCount").textContent = "Found: " + data.found + " devices";
          document.getElementById("statusMsg").textContent = "Status: Scanning... " + data.progress.toFixed(1) + "%";
        }
        else if (data.type === "scan_start") {
          document.getElementById("scanBtn").disabled = true;
          document.getElementById("devices").innerHTML = "<p>Scanning...</p>";
          document.getElementById("progress").style.display = "block";
          document.getElementById("statusMsg").textContent = "Status: Starting scan...";
        }
        else if (data.type === "info") {
          console.log("Info:", data.msg);
        }
      };

      ws.onerror = function(error) {
        console.error("WebSocket error:", error);
        wsConnected = false;
      };
      
      ws.onclose = function() {
        console.log("WebSocket closed, reconnecting...");
        wsConnected = false;
        setTimeout(connectWebSocket, 2000);
      };
    }
    
    connectWebSocket();
      
    function toggle(ip) {
      if (!wsConnected) {
        alert("WebSocket not connected!");
        return;
      }
      console.log("Toggling: " + ip);
      ws.send(JSON.stringify({action:"toggle", ip:ip}));
    }
    
    function scan() {
      console.log("Scan button clicked");
      if (!wsConnected) {
        alert("WebSocket not connected!");
        return;
      }
      ws.send(JSON.stringify({action:"scan"}));
      document.getElementById("scanBtn").disabled = true;
    }
  </script>
</body>
</html>
)rawliteral";

// ── Tasmota Detection Function ──────────────────────────────────
String getDeviceName(const String& ip) {
  esp_task_wdt_reset();
  
  HTTPClient http;
  http.setTimeout(2000);
  
  String url = "http://" + ip + "/cm?cmnd=DeviceName";
  http.begin(url);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    
    response.trim();
    int jsonStart = response.indexOf('{');
    if (jsonStart > 0 && jsonStart < 10) {
      response = response.substring(jsonStart);
    }
    
    // Parse {"DeviceName":"YourDeviceName"}
    int start = response.indexOf("\"DeviceName\":\"");
    if (start >= 0) {
      start += 14; // length of "DeviceName":\"
      int end = response.indexOf("\"", start);
      if (end > start) {
        return response.substring(start, end);
      }
    }
  } else {
    http.end();
  }
  
  return "Unknown";
}

String getFriendlyName(const String& ip) {
  esp_task_wdt_reset();
  
  HTTPClient http;
  http.setTimeout(2000);
  
  String url = "http://" + ip + "/cm?cmnd=FriendlyName1";
  http.begin(url);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    
    response.trim();
    int jsonStart = response.indexOf('{');
    if (jsonStart > 0 && jsonStart < 10) {
      response = response.substring(jsonStart);
    }
    
    // Parse {"FriendlyName1":"YourFriendlyName"}
    int start = response.indexOf("\"FriendlyName1\":\"");
    if (start >= 0) {
      start += 17; // length of "FriendlyName1":\"
      int end = response.indexOf("\"", start);
      if (end > start) {
        return response.substring(start, end);
      }
    }
  } else {
    http.end();
  }
  
  return "";
}

bool checkTasmota(const String& ip) {
  // Feed watchdog before each HTTP request
  esp_task_wdt_reset();
  
  Serial.printf("Checking IP: %s\n", ip.c_str());
  
  HTTPClient http;
  http.setTimeout(SCAN_TIMEOUT_MS);
  
  String url = "http://" + ip + "/cm?cmnd=Power";
  http.begin(url);
  
  int httpCode = http.GET();
  
  // Feed watchdog after HTTP request completes
  esp_task_wdt_reset();
  
  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    
    response.trim();
    
    // Remove chunk size prefix if present (like "a" or "1b")
    int jsonStart = response.indexOf('{');
    if (jsonStart > 0 && jsonStart < 10) {
      response = response.substring(jsonStart);
    }
    
    // Check for Tasmota Power response: {"POWER":"ON"} or {"POWER":"OFF"}
    bool isTasmota = (response.indexOf("\"POWER") >= 0 && 
                      (response.indexOf("\"ON\"") >= 0 || response.indexOf("\"OFF\"") >= 0));
    
    if (isTasmota) {
      Serial.printf("  ✓ Found Tasmota at: %s\n", ip.c_str());
      Serial.printf("    Response: %s\n", response.c_str());
    } else {
      Serial.printf("  ✗ Not Tasmota at: %s (response: %s)\n", ip.c_str(), response.substring(0, 50).c_str());
    }
    
    return isTasmota;
  } else {
    http.end();
    if (httpCode > 0) {
      Serial.printf("  HTTP error %d from %s\n", httpCode, ip.c_str());
    } else {
      Serial.printf("  Connection failed to %s\n", ip.c_str());
    }
    return false;
  }
}

// ── Scan Range Task ─────────────────────────────────────────────
void scanRangeTask(void* parameter) {
  int* range = (int*)parameter;
  int start = range[0];
  int end = range[1];
  
  Serial.printf("[Task] Scanning range: %d to %d\n", start, end);
  
  for (int i = start; i <= end; i++) {
    // Feed watchdog at the start of each iteration
    esp_task_wdt_reset();
    
    String ip = subnetBase + String(i);
    
    if (checkTasmota(ip)) {
      // Get device info
      String deviceName = getDeviceName(ip);
      String friendlyName = getFriendlyName(ip);
      
      if (friendlyName.length() == 0) {
        friendlyName = deviceName;
      }
      
      Serial.printf("  Device: %s, Friendly: %s\n", deviceName.c_str(), friendlyName.c_str());
      
      if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
        DeviceInfo device;
        device.ip = ip;
        device.deviceName = deviceName;
        device.friendlyName = friendlyName;
        tasmotaDevices.push_back(device);
        xSemaphoreGive(ipListMutex);
      }
    }
    
    totalScannedIPs++;
    
    // Feed watchdog after processing
    esp_task_wdt_reset();
    delay(10);
  }
  
  delete[] range;
  Serial.printf("[Task] Range scan complete\n");
  vTaskSuspend(NULL);
}

// ── Parallel Scan Task ──────────────────────────────────────────
void parallelScanTask(void* parameter) {
  Serial.println("[ParallelScan] Starting parallel scan...");
  Serial.printf("[Memory] Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Feed watchdog at start
  esp_task_wdt_reset();
  
  isScanning = true;
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    tasmotaDevices.clear();
    xSemaphoreGive(ipListMutex);
  }
  
  totalScannedIPs = 0;
  int totalIPs = endOctet - startOctet + 1;
  int ipsPerTask = ceil((float)totalIPs / MAX_PARALLEL_SCANS);
  
  Serial.printf("[ParallelScan] Total IPs: %d, IPs per task: %d\n", totalIPs, ipsPerTask);
  
  TaskHandle_t taskHandles[MAX_PARALLEL_SCANS];
  memset(taskHandles, 0, sizeof(taskHandles));
  
  // Create scanning tasks
  for (int taskNum = 0; taskNum < MAX_PARALLEL_SCANS; taskNum++) {
    int taskStart = startOctet + (taskNum * ipsPerTask);
    int taskEnd = min(startOctet + ((taskNum + 1) * ipsPerTask) - 1, endOctet);
    
    if (taskStart > endOctet) break;
    
    int* range = new int[2];
    range[0] = taskStart;
    range[1] = taskEnd;
    
    char taskName[20];
    snprintf(taskName, sizeof(taskName), "Scan%d", taskNum);
    
    Serial.printf("[ParallelScan] Creating task %d for IPs %d-%d\n", taskNum, taskStart, taskEnd);
    
    xTaskCreatePinnedToCore(scanRangeTask, taskName, 8192, (void*)range, 1, &taskHandles[taskNum], 0);
    
    // Feed watchdog after creating each task
    esp_task_wdt_reset();
  }
  
  // Monitor progress
  unsigned long lastUpdate = 0;
  
  while (totalScannedIPs < totalIPs) {
    // Feed watchdog in monitoring loop
    esp_task_wdt_reset();
    
    delay(100);
    
    if (millis() - lastUpdate > 500) {
      float progress = (float)totalScannedIPs / totalIPs * 100.0;
      int currentIP = startOctet + totalScannedIPs;
      
      Serial.printf("[ParallelScan] Progress: %.1f%% (%d/%d) - IP: %s%d\n", 
                    progress, totalScannedIPs, totalIPs, subnetBase.c_str(), currentIP);
      
      JsonDocument doc;
      doc["type"] = "scan_progress";
      doc["current_ip"] = String(subnetBase + String(currentIP));
      doc["progress"] = progress;
      
      xSemaphoreTake(ipListMutex, portMAX_DELAY);
      doc["found"] = tasmotaDevices.size();
      xSemaphoreGive(ipListMutex);
      
      String json;
      serializeJson(doc, json);
      broadcastToWebClients(json);  // Only to web clients
      
      lastUpdate = millis();
      
      // Feed watchdog after broadcast
      esp_task_wdt_reset();
    }
    
    // Check if all done
    bool allDone = true;
    for (int i = 0; i < MAX_PARALLEL_SCANS; i++) {
      if (taskHandles[i] != NULL) {
        eTaskState state = eTaskGetState(taskHandles[i]);
        if (state != eDeleted && state != eInvalid && state != eSuspended) {
          allDone = false;
          break;
        }
      }
    }
    
    if (allDone) break;
  }
  
  delay(100);
  
  // Feed watchdog before cleanup
  esp_task_wdt_reset();
  
  // Cleanup
  Serial.println("[ParallelScan] Cleaning up tasks...");
  for (int i = 0; i < MAX_PARALLEL_SCANS; i++) {
    if (taskHandles[i] != NULL) {
      vTaskDelete(taskHandles[i]);
      taskHandles[i] = NULL;
    }
  }
  
  delay(100);
  
  isScanning = false;
  Serial.printf("[ParallelScan] Scan complete. Found %d devices\n", tasmotaDevices.size());
  Serial.printf("[Memory] Free heap after scan: %d bytes\n", ESP.getFreeHeap());
  
  // Feed watchdog before final broadcast
  esp_task_wdt_reset();
  
  broadcastDevices();
  vTaskDelete(NULL);
}

// ── Broadcast to Web Clients Only ───────────────────────────────
void broadcastToWebClients(const String& message) {
  if (xSemaphoreTake(clientListMutex, portMAX_DELAY)) {
    for (uint32_t clientId : webClients) {
      AsyncWebSocketClient* client = ws.client(clientId);
      if (client && client->status() == WS_CONNECTED) {
        client->text(message);
      }
    }
    xSemaphoreGive(clientListMutex);
  }
}

// ── Get Power State ──────────────────────────────────────────────
String getPowerState(const String& ip) {
  // Feed watchdog before HTTP request
  esp_task_wdt_reset();
  
  HTTPClient http;
  http.setTimeout(2000);
  
  String url = "http://" + ip + "/cm?cmnd=Power";
  http.begin(url);
  
  int code = http.GET();
  
  // Feed watchdog after HTTP request
  esp_task_wdt_reset();
  
  if (code == 200) {
    String response = http.getString();
    http.end();
    
    Serial.printf("  Power response from %s: %s\n", ip.c_str(), response.c_str());
    
    if (response.indexOf("\"ON\"") >= 0) return "ON";
    if (response.indexOf("\"OFF\"") >= 0) return "OFF";
    
    return response.substring(0, 50);
  }
  
  http.end();
  return "UNKNOWN";
}

// ── Toggle Power ─────────────────────────────────────────────────
void togglePower(const String& ip) {
  Serial.printf("Toggling power for: %s\n", ip.c_str());
  
  // Feed watchdog before HTTP request
  esp_task_wdt_reset();
  
  HTTPClient http;
  http.setTimeout(2000);
  
  String url = "http://" + ip + "/cm?cmnd=Power%20TOGGLE";
  http.begin(url);
  int code = http.GET();
  String response = http.getString();
  http.end();
  
  // Feed watchdog after HTTP request
  esp_task_wdt_reset();
  
  Serial.printf("  Toggle response (%d): %s\n", code, response.c_str());
  delay(200);
}

// ── Broadcast Devices ────────────────────────────────────────────
void broadcastDevices() {
  Serial.println("Broadcasting devices to WebSocket clients");
  
  // Feed watchdog at start
  esp_task_wdt_reset();
  
  JsonDocument doc;
  JsonArray list = doc["list"].to<JsonArray>();
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (const auto& device : tasmotaDevices) {
      JsonObject dev = list.add<JsonObject>();
      dev["ip"] = device.ip;
      dev["devicename"] = device.deviceName;
      dev["friendlyname"] = device.friendlyName;
      String state = getPowerState(device.ip);
      dev["state"] = state;
      Serial.printf("  Device: %s (%s) -> %s\n", device.friendlyName.c_str(), device.ip.c_str(), state.c_str());
      
      // Feed watchdog for each device
      esp_task_wdt_reset();
    }
    xSemaphoreGive(ipListMutex);
  }
  
  doc["type"] = "devices";
  
  String json;
  serializeJson(doc, json);
  broadcastToWebClients(json);  // Only to web clients
  
  Serial.printf("Broadcast complete. Total devices: %d\n", tasmotaDevices.size());
  
  // Feed watchdog at end
  esp_task_wdt_reset();
}

// ── WebSocket Events ─────────────────────────────────────────────
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", 
                  client->id(), client->remoteIP().toString().c_str());
    
    // Add to web clients list
    if (xSemaphoreTake(clientListMutex, portMAX_DELAY)) {
      webClients.push_back(client->id());
      xSemaphoreGive(clientListMutex);
    }
    
    client->text("{\"type\":\"info\",\"msg\":\"Connected\"}");
    broadcastDevices();
  }
  else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    
    // Remove from web clients list
    if (xSemaphoreTake(clientListMutex, portMAX_DELAY)) {
      webClients.erase(std::remove(webClients.begin(), webClients.end(), client->id()), webClients.end());
      xSemaphoreGive(clientListMutex);
    }
  }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      String msg = (char*)data;
      
      Serial.printf("WebSocket message: %s\n", msg.c_str());
      
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, msg);
      if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return;
      }
      
      String action = doc["action"] | "";
      
      if (action == "scan") {
        Serial.println("Received scan request");
        if (!isScanning) {
          JsonDocument startDoc;
          startDoc["type"] = "scan_start";
          String startJson;
          serializeJson(startDoc, startJson);
          broadcastToWebClients(startJson);  // Only to web clients
          
          Serial.println("Starting parallel scan task...");
          
          xTaskCreatePinnedToCore(parallelScanTask, "ParallelScan", 12288, NULL, 1, &scanTaskHandle, 1);
        } else {
          client->text("{\"type\":\"info\",\"msg\":\"Scan already in progress\"}");
        }
      }
      else if (action == "toggle") {
        String ip = doc["ip"] | "";
        if (ip.length() > 0) {
          togglePower(ip);
          delay(100);  // Reduced delay
          
          // Only update the toggled device, not all devices
          String newState = getPowerState(ip);
          
          JsonDocument updateDoc;
          updateDoc["type"] = "device_update";
          updateDoc["ip"] = ip;
          updateDoc["state"] = newState;
          
          String updateJson;
          serializeJson(updateDoc, updateJson);
          broadcastToWebClients(updateJson);
          
          Serial.printf("Updated device %s -> %s\n", ip.c_str(), newState.c_str());
        }
      }
    }
  }
}

// ── Setup ────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== Tasmota Network Scanner ===");
  
  // Configure watchdog timer with 30 second timeout
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);  // Add current task to WDT
  
  Serial.println("Watchdog timer configured (30s timeout)");
  
  ipListMutex = xSemaphoreCreateMutex();
  clientListMutex = xSemaphoreCreateMutex();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.printf("Connecting to WiFi: %s", ssid);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    esp_task_wdt_reset();  // Feed watchdog during WiFi connection
    if (millis() - startTime > 15000) {
      Serial.println("\nWiFi connection timeout!");
      break;
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  }
  
  local_IP = WiFi.localIP();
  subnetBase = String(local_IP[0]) + "." + String(local_IP[1]) + "." + String(local_IP[2]) + ".";
  
  int myLast = local_IP[3];
  //startOctet = max(2, myLast - 30);
  //endOctet = min(254, myLast + 30);
  
  Serial.printf("\nNetwork Configuration:\n");
  Serial.printf("  Subnet base: %s\n", subnetBase.c_str());
  Serial.printf("  Scan range: %s%d to %s%d\n", subnetBase.c_str(), startOctet, subnetBase.c_str(), endOctet);
  Serial.printf("  Total IPs to scan: %d\n", endOctet - startOctet + 1);
  Serial.printf("  Parallel scans: %d\n", MAX_PARALLEL_SCANS);
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", index_html);
  });
  
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
  
  Serial.println("HTTP server started");
  Serial.println("Open browser: http://" + WiFi.localIP().toString());
  Serial.println("======================================\n");
  
  // Feed watchdog at end of setup
  esp_task_wdt_reset();
}

// ── Loop ─────────────────────────────────────────────────────────
void loop() {
  // Feed watchdog in main loop
  esp_task_wdt_reset();
  
  ws.cleanupClients();
  delay(10);
}
