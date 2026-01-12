#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "wifiname";
const char* password = "wifipassword";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
Preferences preferences;

// Scan settings
struct ScanSettings {
  uint8_t network1;
  uint8_t network2;
  uint8_t network3;
  uint8_t startIP;
  uint8_t endIP;
};

ScanSettings scanSettings = {10, 10, 100, 100, 150};

// Store discovered Tasmota devices
struct TasmotaDevice {
  IPAddress ip;
  String hostname;
  String deviceName;
  String friendlyName;
  String macAddress;
  bool powerState;
  unsigned long lastSeen;
  // Power monitoring
  float currentPower;      // Current power in Watts
  float voltage;           // Voltage in V
  float current;           // Current in A
  float totalEnergy;       // Total energy in kWh
  bool hasPowerMonitoring; // Does device support power monitoring
};

TasmotaDevice devices[50];
int deviceCount = 0;
unsigned long lastScan = 0;
bool scanInProgress = false;

// Mutex for thread-safe device list access
SemaphoreHandle_t deviceMutex;

// Parallel scan task parameters
#define MAX_PARALLEL_SCANS 10
struct ScanTask {
  IPAddress ip;
  bool active;
  bool found;
  TasmotaDevice device;
};

ScanTask scanTasks[MAX_PARALLEL_SCANS];
SemaphoreHandle_t scanMutex;

// Forward declarations
void handleRoot();
void handleScan();
void handleSettings();
void handleGetSettings();
void scanNetwork();
void scanNetworkParallel();
void sendDeviceList();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void handleWebSocketControl(String message);
void controlDevice(IPAddress ip, bool turnOn);
void handleAllOn();
void handleAllOff();
void handleAllOnParallel();
void handleAllOffParallel();
void refreshAllStates();
void refreshAllStatesParallel();
bool checkTasmotaDevice(IPAddress ip);
void getTasmotaDetails(int index);
String getTimeSince(unsigned long timestamp);
String extractJSON(String httpResponse);
String decodeChunked(String response);
void parallelScanTask(void* parameter);
void parallelControlTask(void* parameter);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\nTasmota WebSocket Dashboard (PARALLEL VERSION)");
  Serial.println("================================================\n");
  
  // Create mutex for thread safety
  deviceMutex = xSemaphoreCreateMutex();
  scanMutex = xSemaphoreCreateMutex();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  
  // Load scan settings from preferences
  preferences.begin("tasmota", false);
  scanSettings.network1 = preferences.getUChar("net1", 10);
  scanSettings.network2 = preferences.getUChar("net2", 10);
  scanSettings.network3 = preferences.getUChar("net3", 100);
  scanSettings.startIP = preferences.getUChar("startIP", 100);
  scanSettings.endIP = preferences.getUChar("endIP", 150);
  preferences.end();
  
  Serial.print("Scan settings: ");
  Serial.print(scanSettings.network1);
  Serial.print(".");
  Serial.print(scanSettings.network2);
  Serial.print(".");
  Serial.print(scanSettings.network3);
  Serial.print(".");
  Serial.print(scanSettings.startIP);
  Serial.print("-");
  Serial.println(scanSettings.endIP);
  
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/get-settings", handleGetSettings);
  
  server.begin();
  Serial.println("Web server started!");
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81");
  
  Serial.print("Open browser to: http://");
  Serial.println(WiFi.localIP());
  Serial.println();
  
  Serial.println("Performing initial parallel scan...");
  scanNetworkParallel();
}

void loop() {
  server.handleClient();
  webSocket.loop();
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
      
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      sendDeviceList();
      break;
    }
    
    case WStype_TEXT: {
      String message = String((char*)payload);
      Serial.printf("[%u] Received: %s\n", num, message.c_str());
      
      if (message.startsWith("CONTROL:")) {
        handleWebSocketControl(message);
      } else if (message == "SCAN") {
        scanNetworkParallel();
      } else if (message == "REFRESH") {
        refreshAllStatesParallel();
      } else if (message == "ALL_ON") {
        handleAllOnParallel();
      } else if (message == "ALL_OFF") {
        handleAllOffParallel();
      }
      break;
    }
  }
}

void handleWebSocketControl(String message) {
  int firstColon = message.indexOf(':');
  int secondColon = message.indexOf(':', firstColon + 1);
  
  if (firstColon > 0 && secondColon > 0) {
    String ipStr = message.substring(firstColon + 1, secondColon);
    String action = message.substring(secondColon + 1);
    
    IPAddress ip;
    ip.fromString(ipStr);
    
    bool turnOn = (action == "on");
    
    Serial.print("Controlling ");
    Serial.print(ip);
    Serial.print(" -> ");
    Serial.println(turnOn ? "ON" : "OFF");
    
    controlDevice(ip, turnOn);
    
    // Find the device in our list and update its state
    if (xSemaphoreTake(deviceMutex, portMAX_DELAY)) {
      for (int i = 0; i < deviceCount; i++) {
        if (devices[i].ip == ip) {
          devices[i].powerState = turnOn;
          devices[i].lastSeen = millis();
          break;
        }
      }
      xSemaphoreGive(deviceMutex);
    }
    
    delay(300);
    sendDeviceList();
  }
}

void controlDevice(IPAddress ip, bool turnOn) {
  WiFiClient client;
  
  if (client.connect(ip, 80, 300)) {
    String cmd = turnOn ? "1" : "0";
    client.print(String("GET /cm?cmnd=Power%20") + cmd + " HTTP/1.1\r\n" +
                 "Host: " + ip.toString() + "\r\n" +
                 "Connection: close\r\n\r\n");
    delay(50);
    client.stop();
    
    Serial.print("Controlled ");
    Serial.print(ip);
    Serial.print(" -> ");
    Serial.println(turnOn ? "ON" : "OFF");
  }
}

// Parallel control task structure
struct ControlTaskParam {
  IPAddress ip;
  bool turnOn;
  bool* powerState;
};

void parallelControlTask(void* parameter) {
  ControlTaskParam* param = (ControlTaskParam*)parameter;
  
  WiFiClient client;
  if (client.connect(param->ip, 80, 300)) {
    String cmd = param->turnOn ? "1" : "0";
    client.print(String("GET /cm?cmnd=Power%20") + cmd + " HTTP/1.1\r\n" +
                 "Host: " + param->ip.toString() + "\r\n" +
                 "Connection: close\r\n\r\n");
    delay(50);
    client.stop();
    *(param->powerState) = param->turnOn;
  }
  
  delete param;
  vTaskDelete(NULL);
}

void handleAllOnParallel() {
  static unsigned long lastAllOn = 0;
  if (millis() - lastAllOn < 2000) {
    Serial.println("ALL_ON ignored (debounce)");
    return;
  }
  lastAllOn = millis();
  
  Serial.println("Executing PARALLEL ALL_ON");
  
  if (xSemaphoreTake(deviceMutex, portMAX_DELAY)) {
    // Launch parallel tasks for all devices
    for (int i = 0; i < deviceCount; i++) {
      ControlTaskParam* param = new ControlTaskParam;
      param->ip = devices[i].ip;
      param->turnOn = true;
      param->powerState = &devices[i].powerState;
      
      char taskName[20];
      sprintf(taskName, "on_%d", i);
      xTaskCreate(parallelControlTask, taskName, 4096, param, 1, NULL);
      
      Serial.print("  Started task for: ");
      Serial.print(devices[i].ip);
      Serial.print(" (");
      Serial.print(devices[i].friendlyName);
      Serial.println(")");
    }
    xSemaphoreGive(deviceMutex);
  }
  
  // Wait a bit for tasks to complete
  delay(800);
  
  Serial.println("PARALLEL ALL_ON complete");
  sendDeviceList();
}

void handleAllOffParallel() {
  static unsigned long lastAllOff = 0;
  if (millis() - lastAllOff < 2000) {
    Serial.println("ALL_OFF ignored (debounce)");
    return;
  }
  lastAllOff = millis();
  
  Serial.println("Executing PARALLEL ALL_OFF");
  
  if (xSemaphoreTake(deviceMutex, portMAX_DELAY)) {
    // Launch parallel tasks for all devices
    for (int i = 0; i < deviceCount; i++) {
      ControlTaskParam* param = new ControlTaskParam;
      param->ip = devices[i].ip;
      param->turnOn = false;
      param->powerState = &devices[i].powerState;
      
      char taskName[20];
      sprintf(taskName, "off_%d", i);
      xTaskCreate(parallelControlTask, taskName, 4096, param, 1, NULL);
      
      Serial.print("  Started task for: ");
      Serial.print(devices[i].ip);
      Serial.print(" (");
      Serial.print(devices[i].friendlyName);
      Serial.println(")");
    }
    xSemaphoreGive(deviceMutex);
  }
  
  // Wait a bit for tasks to complete
  delay(800);
  
  Serial.println("PARALLEL ALL_OFF complete");
  sendDeviceList();
}

// Parallel refresh task
struct RefreshTaskParam {
  IPAddress ip;
  bool* powerState;
  bool* hasPowerMonitoring;
  float* currentPower;
  float* voltage;
  float* current;
  float* totalEnergy;
  String friendlyName;
};

void parallelRefreshTask(void* parameter) {
  RefreshTaskParam* param = (RefreshTaskParam*)parameter;
  
  WiFiClient client;
  
  // First, get power state
  if (client.connect(param->ip, 80, 500)) {
    client.print(String("GET /cm?cmnd=Power HTTP/1.1\r\n") +
                 "Host: " + param->ip.toString() + "\r\n" +
                 "Connection: close\r\n\r\n");
    
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 800) {
        client.stop();
        delete param;
        vTaskDelete(NULL);
        return;
      }
      delay(10);
    }
    
    delay(50);
    
    String response = "";
    unsigned long readStart = millis();
    while (client.connected() || client.available()) {
      if (client.available()) {
        response += (char)client.read();
        readStart = millis();
      }
      if (millis() - readStart > 300) break;
      if (response.length() > 500) break;
    }
    client.stop();
    
    String decoded = decodeChunked(response);
    String jsonStr = extractJSON(decoded);
    
    if (jsonStr.length() > 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, jsonStr)) {
        if (doc["POWER"]) {
          String powerState = doc["POWER"].as<String>();
          *(param->powerState) = (powerState == "ON" || powerState == "1");
        }
      }
    }
  }
  
  // If device has power monitoring, update energy data
  if (*(param->hasPowerMonitoring)) {
    delay(100);
    
    if (client.connect(param->ip, 80, 500)) {
      client.print(String("GET /cm?cmnd=Status%208 HTTP/1.1\r\n") +
                   "Host: " + param->ip.toString() + "\r\n" +
                   "Connection: close\r\n\r\n");
      
      unsigned long timeout = millis();
      while (client.available() == 0) {
        if (millis() - timeout > 800) {
          client.stop();
          delete param;
          vTaskDelete(NULL);
          return;
        }
        delay(10);
      }
      
      delay(100);
      
      String response = "";
      unsigned long readStart = millis();
      while (client.connected() || client.available()) {
        if (client.available()) {
          response += (char)client.read();
          readStart = millis();
        }
        if (millis() - readStart > 500) break;
        if (response.length() > 2000) break;
      }
      client.stop();
      
      String decoded = decodeChunked(response);
      String jsonStr = extractJSON(decoded);
      
      if (jsonStr.length() > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, jsonStr)) {
          if (doc["StatusSNS"]["ENERGY"]) {
            JsonObject energy = doc["StatusSNS"]["ENERGY"];
            
            if (energy["Power"]) {
              *(param->currentPower) = energy["Power"].as<float>();
            }
            if (energy["Voltage"]) {
              *(param->voltage) = energy["Voltage"].as<float>();
            }
            if (energy["Current"]) {
              *(param->current) = energy["Current"].as<float>();
            }
            if (energy["Total"]) {
              *(param->totalEnergy) = energy["Total"].as<float>();
            }
          }
        }
      }
    }
  }
  
  Serial.print("  ");
  Serial.print(param->friendlyName);
  Serial.print(" -> ");
  Serial.print(*(param->powerState) ? "ON" : "OFF");
  if (*(param->hasPowerMonitoring)) {
    Serial.print(", ");
    Serial.print(*(param->currentPower));
    Serial.print("W");
  }
  Serial.println();
  
  delete param;
  vTaskDelete(NULL);
}

void refreshAllStatesParallel() {
  Serial.println("Refreshing all device states (PARALLEL)...");
  
  if (xSemaphoreTake(deviceMutex, portMAX_DELAY)) {
    for (int i = 0; i < deviceCount; i++) {
      RefreshTaskParam* param = new RefreshTaskParam;
      param->ip = devices[i].ip;
      param->powerState = &devices[i].powerState;
      param->hasPowerMonitoring = &devices[i].hasPowerMonitoring;
      param->currentPower = &devices[i].currentPower;
      param->voltage = &devices[i].voltage;
      param->current = &devices[i].current;
      param->totalEnergy = &devices[i].totalEnergy;
      param->friendlyName = devices[i].friendlyName;
      
      char taskName[20];
      sprintf(taskName, "refresh_%d", i);
      xTaskCreate(parallelRefreshTask, taskName, 8192, param, 1, NULL); // Increased stack size
    }
    xSemaphoreGive(deviceMutex);
  }
  
  // Wait for all refresh tasks to complete
  delay(1500);
  
  Serial.println("PARALLEL refresh complete!");
  sendDeviceList();
}

void parallelScanTask(void* parameter) {
  int taskIndex = (int)parameter;
  
  if (xSemaphoreTake(scanMutex, portMAX_DELAY)) {
    if (!scanTasks[taskIndex].active) {
      xSemaphoreGive(scanMutex);
      vTaskDelete(NULL);
      return;
    }
    
    IPAddress ip = scanTasks[taskIndex].ip;
    xSemaphoreGive(scanMutex);
    
    WiFiClient client;
    
    if (!client.connect(ip, 80, 400)) {
      if (xSemaphoreTake(scanMutex, portMAX_DELAY)) {
        scanTasks[taskIndex].active = false;
        scanTasks[taskIndex].found = false;
        xSemaphoreGive(scanMutex);
      }
      vTaskDelete(NULL);
      return;
    }
    
    client.print(String("GET /cm?cmnd=Status HTTP/1.1\r\n") +
                 "Host: " + ip.toString() + "\r\n" +
                 "Connection: close\r\n\r\n");
    
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 1000) {
        client.stop();
        if (xSemaphoreTake(scanMutex, portMAX_DELAY)) {
          scanTasks[taskIndex].active = false;
          scanTasks[taskIndex].found = false;
          xSemaphoreGive(scanMutex);
        }
        vTaskDelete(NULL);
        return;
      }
      delay(10);
    }
    
    delay(100);
    
    String response = "";
    unsigned long readStart = millis();
    while (client.connected() || client.available()) {
      if (client.available()) {
        response += (char)client.read();
        readStart = millis();
      }
      if (millis() - readStart > 300) break;
      if (response.length() > 3000) break;
    }
    client.stop();
    
    String decoded = decodeChunked(response);
    
    if (decoded.indexOf("\"Status\":{") > 0 || 
        decoded.indexOf("\"DeviceName\"") > 0 ||
        decoded.indexOf("\"FriendlyName\"") > 0 ||
        decoded.indexOf("\"Module\":") > 0 ||
        decoded.indexOf("Tasmota") > 0) {
      
      if (xSemaphoreTake(scanMutex, portMAX_DELAY)) {
        scanTasks[taskIndex].found = true;
        scanTasks[taskIndex].device.ip = ip;
        scanTasks[taskIndex].device.lastSeen = millis();
        scanTasks[taskIndex].active = false;
        xSemaphoreGive(scanMutex);
        
        Serial.print("  Found Tasmota at ");
        Serial.println(ip);
      }
    } else {
      if (xSemaphoreTake(scanMutex, portMAX_DELAY)) {
        scanTasks[taskIndex].active = false;
        scanTasks[taskIndex].found = false;
        xSemaphoreGive(scanMutex);
      }
    }
  }
  
  vTaskDelete(NULL);
}

void scanNetworkParallel() {
  if (scanInProgress) {
    Serial.println("Scan already in progress");
    return;
  }
  
  scanInProgress = true;
  
  if (xSemaphoreTake(deviceMutex, portMAX_DELAY)) {
    deviceCount = 0;
    xSemaphoreGive(deviceMutex);
  }
  
  Serial.print("PARALLEL Scanning ");
  Serial.print(scanSettings.network1);
  Serial.print(".");
  Serial.print(scanSettings.network2);
  Serial.print(".");
  Serial.print(scanSettings.network3);
  Serial.print(".");
  Serial.print(scanSettings.startIP);
  Serial.print("-");
  Serial.print(scanSettings.endIP);
  Serial.println(" for Tasmota devices...");
  
  JsonDocument doc;
  doc["type"] = "scan";
  doc["status"] = "started";
  String output;
  serializeJson(doc, output);
  webSocket.broadcastTXT(output);
  
  int totalIPs = scanSettings.endIP - scanSettings.startIP + 1;
  int foundCount = 0;
  
  for (int i = scanSettings.startIP; i <= scanSettings.endIP; i += MAX_PARALLEL_SCANS) {
    // Initialize scan tasks
    if (xSemaphoreTake(scanMutex, portMAX_DELAY)) {
      for (int j = 0; j < MAX_PARALLEL_SCANS; j++) {
        int currentIP = i + j;
        if (currentIP <= scanSettings.endIP) {
          scanTasks[j].ip = IPAddress(scanSettings.network1, scanSettings.network2, 
                                       scanSettings.network3, currentIP);
          scanTasks[j].active = true;
          scanTasks[j].found = false;
        } else {
          scanTasks[j].active = false;
        }
      }
      xSemaphoreGive(scanMutex);
    }
    
    // Launch parallel scan tasks
    for (int j = 0; j < MAX_PARALLEL_SCANS; j++) {
      if (scanTasks[j].active) {
        char taskName[20];
        sprintf(taskName, "scan_%d", j);
        xTaskCreate(parallelScanTask, taskName, 4096, (void*)j, 1, NULL);
      }
    }
    
    // Wait for this batch to complete
    bool allDone = false;
    unsigned long batchStart = millis();
    while (!allDone && (millis() - batchStart < 3000)) {
      delay(100);
      allDone = true;
      
      if (xSemaphoreTake(scanMutex, portMAX_DELAY)) {
        for (int j = 0; j < MAX_PARALLEL_SCANS; j++) {
          if (scanTasks[j].active) {
            allDone = false;
            break;
          }
        }
        xSemaphoreGive(scanMutex);
      }
    }
    
    // Collect results
    if (xSemaphoreTake(scanMutex, portMAX_DELAY)) {
      for (int j = 0; j < MAX_PARALLEL_SCANS; j++) {
        if (scanTasks[j].found && deviceCount < 50) {
          if (xSemaphoreTake(deviceMutex, portMAX_DELAY)) {
            devices[deviceCount] = scanTasks[j].device;
            
            // Get detailed info
            getTasmotaDetails(deviceCount);
            
            deviceCount++;
            foundCount++;
            xSemaphoreGive(deviceMutex);
          }
        }
      }
      xSemaphoreGive(scanMutex);
    }
    
    Serial.print("Progress: ");
    Serial.print(min(i + MAX_PARALLEL_SCANS - scanSettings.startIP, totalIPs));
    Serial.print("/");
    Serial.print(totalIPs);
    Serial.print(" (found: ");
    Serial.print(foundCount);
    Serial.println(")");
  }
  
  lastScan = millis();
  scanInProgress = false;
  
  Serial.print("PARALLEL scan complete! Found ");
  Serial.print(deviceCount);
  Serial.println(" Tasmota device(s)\n");
  
  sendDeviceList();
  
  JsonDocument doc2;
  doc2["type"] = "scan";
  doc2["status"] = "complete";
  doc2["count"] = deviceCount;
  String output2;
  serializeJson(doc2, output2);
  webSocket.broadcastTXT(output2);
}

void getTasmotaDetails(int index) {
  WiFiClient client;
  IPAddress ip = devices[index].ip;
  
  // Get FriendlyName1
  if (client.connect(ip, 80, 500)) {
    client.print(String("GET /cm?cmnd=FriendlyName1 HTTP/1.1\r\n") +
                 "Host: " + ip.toString() + "\r\n" +
                 "Connection: close\r\n\r\n");
    
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 1000) {
        client.stop();
        goto tryDeviceName;
      }
      delay(10);
    }
    
    delay(100);
    
    String response = "";
    unsigned long readStart = millis();
    while (client.connected() || client.available()) {
      if (client.available()) {
        response += (char)client.read();
        readStart = millis();
      }
      if (millis() - readStart > 500) break;
      if (response.length() > 1000) break;
    }
    client.stop();
    
    String decoded = decodeChunked(response);
    String jsonStr = extractJSON(decoded);
    
    if (jsonStr.length() > 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, jsonStr)) {
        if (doc["FriendlyName1"]) {
          devices[index].friendlyName = doc["FriendlyName1"].as<String>();
        }
      }
    }
  }
  
  tryDeviceName:
  delay(100);
  
  // Get DeviceName
  if (client.connect(ip, 80, 500)) {
    client.print(String("GET /cm?cmnd=DeviceName HTTP/1.1\r\n") +
                 "Host: " + ip.toString() + "\r\n" +
                 "Connection: close\r\n\r\n");
    
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 1000) {
        client.stop();
        goto tryPower;
      }
      delay(10);
    }
    
    delay(100);
    
    String response = "";
    unsigned long readStart = millis();
    while (client.connected() || client.available()) {
      if (client.available()) {
        response += (char)client.read();
        readStart = millis();
      }
      if (millis() - readStart > 500) break;
      if (response.length() > 1000) break;
    }
    client.stop();
    
    String decoded = decodeChunked(response);
    String jsonStr = extractJSON(decoded);
    
    if (jsonStr.length() > 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, jsonStr)) {
        if (doc["DeviceName"]) {
          devices[index].deviceName = doc["DeviceName"].as<String>();
        }
      }
    }
  }
  
  tryPower:
  delay(100);
  
  // Get Power state
  if (client.connect(ip, 80, 500)) {
    client.print(String("GET /cm?cmnd=Power HTTP/1.1\r\n") +
                 "Host: " + ip.toString() + "\r\n" +
                 "Connection: close\r\n\r\n");
    
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 1000) {
        client.stop();
        return;
      }
      delay(10);
    }
    
    delay(100);
    
    String response = "";
    unsigned long readStart = millis();
    while (client.connected() || client.available()) {
      if (client.available()) {
        response += (char)client.read();
        readStart = millis();
      }
      if (millis() - readStart > 500) break;
      if (response.length() > 1000) break;
    }
    client.stop();
    
    String decoded = decodeChunked(response);
    String jsonStr = extractJSON(decoded);
    
    if (jsonStr.length() > 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, jsonStr)) {
        if (doc["POWER"]) {
          String powerState = doc["POWER"].as<String>();
          devices[index].powerState = (powerState == "ON" || powerState == "1");
        }
      }
    }
  }
  
  // Try to get power monitoring data (Status 8)
  delay(100);
  devices[index].hasPowerMonitoring = false; // Default to no power monitoring
  
  if (client.connect(ip, 80, 500)) {
    client.print(String("GET /cm?cmnd=Status%208 HTTP/1.1\r\n") +
                 "Host: " + ip.toString() + "\r\n" +
                 "Connection: close\r\n\r\n");
    
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 1000) {
        client.stop();
        return;
      }
      delay(10);
    }
    
    delay(100);
    
    String response = "";
    unsigned long readStart = millis();
    while (client.connected() || client.available()) {
      if (client.available()) {
        response += (char)client.read();
        readStart = millis();
      }
      if (millis() - readStart > 500) break;
      if (response.length() > 2000) break;
    }
    client.stop();
    
    String decoded = decodeChunked(response);
    String jsonStr = extractJSON(decoded);
    
    if (jsonStr.length() > 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, jsonStr)) {
        // Check for ENERGY data (common in Tasmota devices with power monitoring)
        if (doc["StatusSNS"]["ENERGY"]) {
          devices[index].hasPowerMonitoring = true;
          
          JsonObject energy = doc["StatusSNS"]["ENERGY"];
          
          if (energy["Power"]) {
            devices[index].currentPower = energy["Power"].as<float>();
          }
          if (energy["Voltage"]) {
            devices[index].voltage = energy["Voltage"].as<float>();
          }
          if (energy["Current"]) {
            devices[index].current = energy["Current"].as<float>();
          }
          if (energy["Total"]) {
            devices[index].totalEnergy = energy["Total"].as<float>();
          }
          
          Serial.print("    Power Monitoring: ");
          Serial.print(devices[index].currentPower);
          Serial.print("W, ");
          Serial.print(devices[index].voltage);
          Serial.print("V, ");
          Serial.print(devices[index].current);
          Serial.print("A, Total: ");
          Serial.print(devices[index].totalEnergy);
          Serial.println("kWh");
        }
      }
    }
  }
}

void sendDeviceList() {
  if (xSemaphoreTake(deviceMutex, portMAX_DELAY)) {
    JsonDocument doc;
    doc["type"] = "devices";
    doc["lastScan"] = getTimeSince(lastScan);
    
    JsonArray dataArray = doc["data"].to<JsonArray>();
    
    for (int i = 0; i < deviceCount; i++) {
      JsonObject device = dataArray.add<JsonObject>();
      device["ip"] = devices[i].ip.toString();
      device["hostname"] = devices[i].hostname;
      device["deviceName"] = devices[i].deviceName;
      device["friendlyName"] = devices[i].friendlyName;
      device["macAddress"] = devices[i].macAddress;
      device["powerState"] = devices[i].powerState;
      device["hasPowerMonitoring"] = devices[i].hasPowerMonitoring;
      
      if (devices[i].hasPowerMonitoring) {
        device["currentPower"] = devices[i].currentPower;
        device["voltage"] = devices[i].voltage;
        device["current"] = devices[i].current;
        device["totalEnergy"] = devices[i].totalEnergy;
      }
    }
    
    String output;
    serializeJson(doc, output);
    webSocket.broadcastTXT(output);
    
    xSemaphoreGive(deviceMutex);
  }
}

String extractJSON(String httpResponse) {
  int jsonStart = httpResponse.indexOf('{');
  if (jsonStart < 0) return "";
  
  int braceCount = 0;
  int jsonEnd = -1;
  
  for (int i = jsonStart; i < httpResponse.length(); i++) {
    char c = httpResponse[i];
    if (c == '{') {
      braceCount++;
    } else if (c == '}') {
      braceCount--;
      if (braceCount == 0) {
        jsonEnd = i;
        break;
      }
    }
  }
  
  if (jsonEnd > jsonStart) {
    return httpResponse.substring(jsonStart, jsonEnd + 1);
  }
  
  return "";
}

String decodeChunked(String response) {
  if (response.indexOf("Transfer-Encoding: chunked") < 0 && 
      response.indexOf("Transfer-Enc") < 0) {
    return response;
  }
  
  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart < 0) return response;
  bodyStart += 4;
  
  String decoded = "";
  int pos = bodyStart;
  
  while (pos < response.length()) {
    int lineEnd = response.indexOf('\n', pos);
    if (lineEnd < 0) break;
    
    String chunkSizeStr = response.substring(pos, lineEnd);
    chunkSizeStr.trim();
    chunkSizeStr.replace("\r", "");
    
    if (chunkSizeStr.length() == 0) {
      pos = lineEnd + 1;
      continue;
    }
    
    long chunkSize = strtol(chunkSizeStr.c_str(), NULL, 16);
    if (chunkSize == 0) break;
    
    pos = lineEnd + 1;
    if (pos + chunkSize <= response.length()) {
      decoded += response.substring(pos, pos + chunkSize);
      pos += chunkSize;
      
      if (pos < response.length() && response[pos] == '\r') pos++;
      if (pos < response.length() && response[pos] == '\n') pos++;
    } else {
      decoded += response.substring(pos);
      break;
    }
  }
  
  return decoded;
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Tasmota Control (PARALLEL)</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      max-width: 1200px;
      margin: 20px auto;
      padding: 20px;
      background: #f5f5f5;
    }
    
    h1 {
      color: #333;
      text-align: center;
    }
    
    .parallel-badge {
      display: inline-block;
      background: #28a745;
      color: white;
      padding: 4px 12px;
      border-radius: 12px;
      font-size: 14px;
      font-weight: bold;
      margin-left: 10px;
    }
    
    .connection {
      text-align: center;
      padding: 10px;
      margin-bottom: 20px;
      border-radius: 5px;
      font-weight: bold;
    }
    
    .connection.connected {
      background: #d4edda;
      color: #155724;
    }
    
    .connection.disconnected {
      background: #f8d7da;
      color: #721c24;
    }
    
    .controls {
      text-align: center;
      margin-bottom: 20px;
    }
    
    button {
      padding: 12px 24px;
      margin: 5px;
      font-size: 16px;
      cursor: pointer;
      border: none;
      border-radius: 5px;
      font-weight: bold;
    }
    
    button:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    
    .btn-scan {
      background: #007bff;
      color: white;
    }
    
    .btn-scan:hover {
      background: #0056b3;
    }
    
    .btn-refresh {
      background: #17a2b8;
      color: white;
    }
    
    .btn-refresh:hover {
      background: #138496;
    }
    
    .btn-all-on {
      background: #28a745;
      color: white;
    }
    
    .btn-all-on:hover {
      background: #218838;
    }
    
    .btn-all-off {
      background: #dc3545;
      color: white;
    }
    
    .btn-all-off:hover {
      background: #c82333;
    }
    
    .btn-settings {
      background: #6c757d;
      color: white;
    }
    
    .btn-settings:hover {
      background: #5a6268;
    }
    
    .status {
      text-align: center;
      padding: 10px;
      background: white;
      border-radius: 5px;
      margin-bottom: 20px;
    }
    
    .device-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
      gap: 15px;
    }
    
    .device {
      background: white;
      padding: 15px;
      border-radius: 5px;
      border: 2px solid #ddd;
    }
    
    .device-name {
      font-size: 18px;
      font-weight: bold;
      margin-bottom: 10px;
      color: #333;
    }
    
    .device-info {
      font-size: 12px;
      color: #666;
      margin-bottom: 10px;
    }
    
    .device-status {
      padding: 8px;
      margin: 10px 0;
      border-radius: 3px;
      text-align: center;
      font-weight: bold;
      font-size: 16px;
    }
    
    .device-status.on {
      background: #28a745;
      color: white;
    }
    
    .device-status.off {
      background: #6c757d;
      color: white;
    }
    
    .loading {
      text-align: center;
      padding: 40px;
      font-size: 18px;
      color: #666;
    }
    
    .modal {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(0, 0, 0, 0.8);
      z-index: 1000;
      overflow-y: auto;
    }
    
    .modal-content {
      background: white;
      max-width: 600px;
      margin: 50px auto;
      padding: 30px;
      border-radius: 10px;
    }
    
    .modal h2 {
      color: #333;
      margin-bottom: 20px;
      text-align: center;
    }
    
    .form-group {
      margin-bottom: 20px;
    }
    
    .form-group label {
      display: block;
      color: #666;
      margin-bottom: 5px;
      font-weight: bold;
    }
    
    .form-group select,
    .form-group input {
      width: 100%;
      padding: 10px;
      border: 1px solid #ddd;
      border-radius: 5px;
      font-size: 14px;
    }
    
    .ip-inputs {
      display: grid;
      grid-template-columns: 1fr auto 1fr auto 1fr auto 1fr;
      gap: 5px;
      align-items: center;
    }
    
    .ip-inputs input {
      text-align: center;
      width: 100%;
    }
    
    .ip-dot {
      font-size: 20px;
      font-weight: bold;
      color: #666;
      text-align: center;
    }
    
    .range-inputs {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 15px;
    }
    
    .modal-buttons {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 15px;
      margin-top: 25px;
    }
    
    .modal-buttons button {
      padding: 12px;
      font-size: 16px;
      font-weight: bold;
      border: none;
      border-radius: 5px;
      cursor: pointer;
    }
    
    .btn-save {
      background: #28a745;
      color: white;
    }
    
    .btn-save:hover {
      background: #218838;
    }
    
    .btn-cancel {
      background: #6c757d;
      color: white;
    }
    
    .btn-cancel:hover {
      background: #5a6268;
    }
  </style>
</head>
<body>
  <h1>Tasmota Device Control<span class="parallel-badge">⚡ PARALLEL</span></h1>
  
  <div class="connection disconnected" id="wsStatus">Connecting to WebSocket...</div>
  
  <div class="controls">
    <button class="btn btn-scan" onclick="scanDevices()">⚡ SCAN NETWORK</button>
    <button class="btn btn-refresh" onclick="refreshStates()">⚡ REFRESH STATE</button>
    <button class="btn btn-all-on" onclick="allOn()">⚡ ALL ON</button>
    <button class="btn btn-all-off" onclick="allOff()">⚡ ALL OFF</button>
    <button class="btn btn-settings" onclick="showSettings()">⚙️ SETTINGS</button>
    <button class="btn btn-settings" onclick="clearRemovedDevices()" style="background: #ffc107; color: #000;" title="Clear list of manually removed devices">🔄 RESTORE REMOVED</button>
  </div>
  
  <div class="status" id="status">Connecting...</div>
  
  <div class="device-grid" id="deviceGrid">
    <div class="loading">Connecting...</div>
  </div>

<div id="settingsModal" class="modal">
  <div class="modal-content">
    <h2>⚙️ Scan Settings</h2>
    
    <div class="form-group">
      <label>Quick Select (Common Ranges):</label>
      <select id="presetRange" onchange="applyPreset()">
        <option value="">-- Select a preset or enter custom --</option>
        <option value="192.168.1">192.168.1.x (Most Common Router)</option>
        <option value="192.168.0">192.168.0.x (Common Router)</option>
        <option value="192.168.2">192.168.2.x</option>
        <option value="10.0.0">10.0.0.x (Apple/Enterprise)</option>
        <option value="10.0.1">10.0.1.x</option>
        <option value="10.10.100">10.10.100.x (Current)</option>
        <option value="172.16.0">172.16.0.x (Enterprise)</option>
      </select>
    </div>
    
    <div class="form-group">
      <label>Network Address:</label>
      <div class="ip-inputs">
        <input type="number" id="net1" min="0" max="255" placeholder="10">
        <span class="ip-dot">.</span>
        <input type="number" id="net2" min="0" max="255" placeholder="10">
        <span class="ip-dot">.</span>
        <input type="number" id="net3" min="0" max="255" placeholder="100">
        <span class="ip-dot">.</span>
        <span style="color:#666;">x</span>
      </div>
    </div>
    
    <div class="form-group">
      <label>IP Range to Scan:</label>
      <div class="range-inputs">
        <div>
          <label style="font-size:12px; font-weight:normal;">Start IP (1-254):</label>
          <input type="number" id="startIP" min="1" max="254" placeholder="100" oninput="updatePreview()">
        </div>
        <div>
          <label style="font-size:12px; font-weight:normal;">End IP (1-254):</label>
          <input type="number" id="endIP" min="1" max="254" placeholder="150" oninput="updatePreview()">
        </div>
      </div>
      <div style="margin-top:10px; padding:10px; background:#f0f0f0; border-radius:5px; font-size:14px; color:#666;">
        Will scan: <span id="scanPreview" style="font-weight:bold; color:#333;">10.10.100.100 - 10.10.100.150</span>
      </div>
    </div>
    
    <div class="modal-buttons">
      <button class="btn-save" onclick="saveSettings()">💾 SAVE & SCAN</button>
      <button class="btn-cancel" onclick="hideSettings()">✖ CANCEL</button>
    </div>
  </div>
</div>

<script>
    let ws;
    let devices = [];
    let removedDevices = new Set(); // Track manually removed devices
    
    // Load removed devices from localStorage on page load
    function loadRemovedDevices() {
      const saved = localStorage.getItem('tasmota_removed_devices');
      if (saved) {
        try {
          const parsed = JSON.parse(saved);
          removedDevices = new Set(parsed);
          console.log('Loaded removed devices:', Array.from(removedDevices));
        } catch (e) {
          console.error('Error loading removed devices:', e);
        }
      }
    }
    
    // Save removed devices to localStorage
    function saveRemovedDevices() {
      localStorage.setItem('tasmota_removed_devices', JSON.stringify(Array.from(removedDevices)));
    }
    
    // Load devices from localStorage
    function loadDevicesFromStorage() {
      const saved = localStorage.getItem('tasmota_devices');
      if (saved) {
        try {
          const parsed = JSON.parse(saved);
          devices = parsed;
          console.log('Loaded', devices.length, 'devices from localStorage');
          updateDeviceGrid();
        } catch (e) {
          console.error('Error loading devices:', e);
        }
      }
    }
    
    // Save devices to localStorage
    function saveDevicesToStorage() {
      localStorage.setItem('tasmota_devices', JSON.stringify(devices));
    }
    
    // Remove device from list
    function removeDevice(ip) {
      if (confirm('Remove this device from the list?')) {
        // Add to removed set
        removedDevices.add(ip);
        saveRemovedDevices();
        
        // Remove from current devices array
        devices = devices.filter(d => d.ip !== ip);
        saveDevicesToStorage();
        updateDeviceGrid();
        
        console.log('Removed device:', ip);
      }
    }
    
    // Clear all removed devices (useful for re-scanning)
    function clearRemovedDevices() {
      if (confirm('Clear the list of manually removed devices? They will reappear on next scan.')) {
        removedDevices.clear();
        saveRemovedDevices();
        console.log('Cleared removed devices list');
        alert('Removed devices list cleared. Run a new scan to see them again.');
      }
    }
    
    // Initialize on page load
    loadRemovedDevices();
    loadDevicesFromStorage();
    
    function connectWebSocket() {
      ws = new WebSocket('ws://' + window.location.hostname + ':81/');
      
      ws.onopen = function() {
        console.log('WebSocket connected');
        document.getElementById('wsStatus').className = 'connection connected';
        document.getElementById('wsStatus').textContent = '⚡ Connected (PARALLEL MODE)';
        document.getElementById('status').textContent = 'Waiting for data...';
      };
      
      ws.onmessage = function(event) {
        const data = JSON.parse(event.data);
        
        if (data.type === 'devices') {
          // Deduplicate devices by IP address (keep the first occurrence)
          const uniqueDevices = [];
          const seenIPs = new Set();
          
          for (const device of data.data) {
            if (!seenIPs.has(device.ip)) {
              seenIPs.add(device.ip);
              uniqueDevices.push(device);
            }
          }
          
          // Filter out manually removed devices
          devices = uniqueDevices.filter(d => !removedDevices.has(d.ip));
          
          // Save to localStorage
          saveDevicesToStorage();
          
          updateDeviceGrid();
          document.getElementById('status').textContent = 
            'Found ' + devices.length + ' device(s) • Last scan: ' + data.lastScan;
        } else if (data.type === 'scan') {
          if (data.status === 'started') {
            document.getElementById('status').textContent = '⚡ Parallel scanning network...';
          } else if (data.status === 'complete') {
            document.getElementById('status').textContent = 
              '⚡ Parallel scan complete! Found ' + data.count + ' device(s)';
          }
        }
      };
      
      ws.onerror = function(error) {
        console.error('WebSocket error:', error);
      };
      
      ws.onclose = function() {
        console.log('WebSocket disconnected');
        document.getElementById('wsStatus').className = 'connection disconnected';
        document.getElementById('wsStatus').textContent = 'Disconnected - Reconnecting...';
        document.getElementById('status').textContent = 'Connection lost';
        setTimeout(connectWebSocket, 3000);
      };
    }
    
    function updateDeviceGrid() {
      const grid = document.getElementById('deviceGrid');
      
      if (devices.length === 0) {
        grid.innerHTML = '<div class="loading">No devices found. Click SCAN NETWORK.</div>';
        return;
      }
      
      grid.innerHTML = devices.map(function(device) {
        const statusClass = device.powerState ? 'on' : 'off';
        const statusText = device.powerState ? 'ON' : 'OFF';
        
        let displayName = device.deviceName || device.friendlyName || device.hostname || 'Tasmota';
        
        if (displayName === 'Tasmota' || displayName === '' || !displayName) {
          const ipParts = device.ip.split('.');
          displayName = 'Tasmota-' + ipParts[3];
        }
        
        let subtitle = '';
        if (device.friendlyName && 
            device.friendlyName !== displayName && 
            device.friendlyName !== 'Tasmota' &&
            device.friendlyName !== device.deviceName) {
          subtitle = device.friendlyName;
        }
        
        // Control Card
        let controlCard = `
          <div class="device" style="position: relative;">
            <button onclick="removeDevice('${device.ip}')" style="position: absolute; top: 5px; right: 5px; background: #dc3545; color: white; border: none; border-radius: 50%; width: 24px; height: 24px; cursor: pointer; font-size: 16px; line-height: 1; padding: 0; font-weight: bold;" title="Remove device from list">×</button>
            <div style="background: #e3f2fd; padding: 5px 10px; margin: -15px -15px 10px -15px; border-radius: 5px 5px 0 0; font-weight: bold; font-size: 12px; color: #1976d2;">🎛️ CONTROL</div>
            <div class="device-name">${displayName}</div>
            ${subtitle ? '<div style="font-size:12px; color:#999; margin-top:-5px; margin-bottom:5px;">' + subtitle + '</div>' : ''}
            <div class="device-info">
              IP: <a href="http://${device.ip}" target="_blank" style="color: #007bff; text-decoration: none;">${device.ip}</a>
            </div>
            <div class="device-status ${statusClass}" onclick="toggleDevice('${device.ip}')" style="cursor: pointer;">${statusText}</div>
          </div>
        `;
        
        // Power Monitoring Card (only if device has power monitoring)
        let powerCard = '';
        if (device.hasPowerMonitoring) {
          powerCard = `
            <div class="device" style="position: relative;">
              <div style="background: #fff3cd; padding: 5px 10px; margin: -15px -15px 10px -15px; border-radius: 5px 5px 0 0; font-weight: bold; font-size: 12px; color: #856404;">⚡ POWER USAGE</div>
              <div class="device-name">${displayName}</div>
              ${subtitle ? '<div style="font-size:12px; color:#999; margin-top:-5px; margin-bottom:5px;">' + subtitle + '</div>' : ''}
              <div style="margin: 10px 0; padding: 10px; background: #f8f9fa; border-radius: 5px;">
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; font-size: 14px;">
                  <div>
                    <div style="color: #666; font-size: 11px;">POWER</div>
                    <div style="font-weight: bold; color: #28a745; font-size: 18px;">${device.currentPower.toFixed(1)} W</div>
                  </div>
                  <div>
                    <div style="color: #666; font-size: 11px;">VOLTAGE</div>
                    <div style="font-weight: bold; font-size: 18px;">${device.voltage.toFixed(0)} V</div>
                  </div>
                  <div>
                    <div style="color: #666; font-size: 11px;">CURRENT</div>
                    <div style="font-weight: bold; font-size: 18px;">${device.current.toFixed(2)} A</div>
                  </div>
                  <div>
                    <div style="color: #666; font-size: 11px;">TOTAL</div>
                    <div style="font-weight: bold; color: #007bff; font-size: 18px;">${device.totalEnergy.toFixed(2)} kWh</div>
                  </div>
                </div>
              </div>
            </div>
          `;
        }
        
        return controlCard + powerCard;
      }).join('');
    }
    
    function toggleDevice(ip) {
      const device = devices.find(d => d.ip === ip);
      if (!device) return;
      
      const newAction = device.powerState ? 'off' : 'on';
      controlDevice(ip, newAction);
    }
    
    function scanDevices() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('SCAN');
      }
    }
    
    function refreshStates() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('REFRESH');
      }
    }
    
    function controlDevice(ip, action) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('CONTROL:' + ip + ':' + action);
      }
    }
    
    function allOn() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        const btn = event.target;
        btn.disabled = true;
        ws.send('ALL_ON');
        setTimeout(function() { btn.disabled = false; }, 2000);
      }
    }
    
    function allOff() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        const btn = event.target;
        btn.disabled = true;
        ws.send('ALL_OFF');
        setTimeout(function() { btn.disabled = false; }, 2000);
      }
    }
    
    connectWebSocket();
    
    function showSettings() {
      fetch('/get-settings')
        .then(response => response.json())
        .then(data => {
          document.getElementById('net1').value = data.network1;
          document.getElementById('net2').value = data.network2;
          document.getElementById('net3').value = data.network3;
          document.getElementById('startIP').value = data.startIP;
          document.getElementById('endIP').value = data.endIP;
          updatePreview();
          document.getElementById('settingsModal').style.display = 'block';
        })
        .catch(err => {
          console.error('Error loading settings:', err);
          alert('Error loading settings. Please try again.');
        });
    }
    
    function hideSettings() {
      document.getElementById('settingsModal').style.display = 'none';
      document.getElementById('presetRange').value = '';
    }
    
    function applyPreset() {
      const preset = document.getElementById('presetRange').value;
      if (preset && preset !== 'custom') {
        const parts = preset.split('.');
        document.getElementById('net1').value = parts[0];
        document.getElementById('net2').value = parts[1];
        document.getElementById('net3').value = parts[2];
        document.getElementById('startIP').value = 1;
        document.getElementById('endIP').value = 254;
        updatePreview();
      }
    }
    
    function updatePreview() {
      const n1 = document.getElementById('net1').value || '0';
      const n2 = document.getElementById('net2').value || '0';
      const n3 = document.getElementById('net3').value || '0';
      const start = document.getElementById('startIP').value || '1';
      const end = document.getElementById('endIP').value || '254';
      
      document.getElementById('scanPreview').textContent = 
        n1 + '.' + n2 + '.' + n3 + '.' + start + ' - ' + n1 + '.' + n2 + '.' + n3 + '.' + end;
    }
    
    function saveSettings() {
      const settings = {
        network1: parseInt(document.getElementById('net1').value) || 0,
        network2: parseInt(document.getElementById('net2').value) || 0,
        network3: parseInt(document.getElementById('net3').value) || 0,
        startIP: parseInt(document.getElementById('startIP').value) || 1,
        endIP: parseInt(document.getElementById('endIP').value) || 254
      };
      
      if (settings.network1 < 0 || settings.network1 > 255 ||
          settings.network2 < 0 || settings.network2 > 255 ||
          settings.network3 < 0 || settings.network3 > 255) {
        alert('Network octets must be between 0-255');
        return;
      }
      
      if (settings.startIP < 1 || settings.startIP > 254 ||
          settings.endIP < 1 || settings.endIP > 254) {
        alert('IP range must be between 1-254');
        return;
      }
      
      if (settings.startIP > settings.endIP) {
        alert('Start IP must be less than or equal to End IP');
        return;
      }
      
      fetch('/settings', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(settings)
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          hideSettings();
          document.getElementById('status').textContent = 'Settings saved! Starting parallel scan...';
          if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send('SCAN');
          }
        } else {
          alert('Failed to save settings. Please try again.');
        }
      })
      .catch(err => {
        console.error('Error saving settings:', err);
        alert('Error saving settings. Please try again.');
      });
    }
  </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleSettings() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }
    
    if (!doc["network1"].is<int>() || !doc["network2"].is<int>() || 
        !doc["network3"].is<int>() || !doc["startIP"].is<int>() || 
        !doc["endIP"].is<int>()) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing fields\"}");
      return;
    }
    
    scanSettings.network1 = doc["network1"];
    scanSettings.network2 = doc["network2"];
    scanSettings.network3 = doc["network3"];
    scanSettings.startIP = doc["startIP"];
    scanSettings.endIP = doc["endIP"];
    
    if (scanSettings.network1 > 255 || scanSettings.network2 > 255 || 
        scanSettings.network3 > 255 || scanSettings.startIP < 1 || 
        scanSettings.startIP > 254 || scanSettings.endIP < 1 || 
        scanSettings.endIP > 254 || scanSettings.startIP > scanSettings.endIP) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid ranges\"}");
      return;
    }
    
    preferences.begin("tasmota", false);
    preferences.putUChar("net1", scanSettings.network1);
    preferences.putUChar("net2", scanSettings.network2);
    preferences.putUChar("net3", scanSettings.network3);
    preferences.putUChar("startIP", scanSettings.startIP);
    preferences.putUChar("endIP", scanSettings.endIP);
    preferences.end();
    
    Serial.println("Settings saved!");
    Serial.print("New range: ");
    Serial.print(scanSettings.network1);
    Serial.print(".");
    Serial.print(scanSettings.network2);
    Serial.print(".");
    Serial.print(scanSettings.network3);
    Serial.print(".");
    Serial.print(scanSettings.startIP);
    Serial.print("-");
    Serial.println(scanSettings.endIP);
    
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"No data\"}");
  }
}

void handleGetSettings() {
  JsonDocument doc;
  doc["network1"] = scanSettings.network1;
  doc["network2"] = scanSettings.network2;
  doc["network3"] = scanSettings.network3;
  doc["startIP"] = scanSettings.startIP;
  doc["endIP"] = scanSettings.endIP;
  
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleScan() {
  Serial.println("Manual parallel scan requested via HTTP");
  scanNetworkParallel();
  server.send(200, "text/plain", "Parallel scan started");
}

String getTimeSince(unsigned long timestamp) {
  unsigned long seconds = (millis() - timestamp) / 1000;
  
  if (seconds < 60) return String(seconds) + "s ago";
  if (seconds < 3600) return String(seconds / 60) + "m ago";
  return String(seconds / 3600) + "h ago";
}

// Legacy functions for compatibility
void handleAllOn() { handleAllOnParallel(); }
void handleAllOff() { handleAllOffParallel(); }
void refreshAllStates() { refreshAllStatesParallel(); }
void scanNetwork() { scanNetworkParallel(); }
