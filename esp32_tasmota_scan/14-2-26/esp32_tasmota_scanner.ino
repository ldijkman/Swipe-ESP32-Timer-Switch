/*
 * Tasmota Network Scanner - Modified Version
 * 
 * LIBRARY COMPATIBILITY NOTES:
 * - This sketch uses DynamicJsonDocument for ArduinoJson v6 compatibility
 * - If you have ArduinoJson v7, all DynamicJsonDocument can be changed to JsonDocument
 * - ESPAsyncWebServer library may show const-correctness warnings - these can be ignored
 * i edited line 1479 else 
 * ~/Arduino/libraries/ESP_Async_WebServer/src/ESPAsyncWebServer.h
   Step 2: Find line 1479
   Look for this code (around line 1479):
   cpptcp_state state() const {
    return static_cast<tcp_state>(_server.status());
 *   or fixed by modifying ESPAsyncWebServer.h line 1479 to add const to status() method
 * 
 * Required Libraries:
 * - WiFi (ESP32 core)
 * - ESPAsyncWebServer (v3.x recommended)
 * - AsyncTCP
 * - HTTPClient
 * - ArduinoJson (v6.x or v7.x)
 * - Preferences
 * - ImprovWiFiLibrary
 * - LittleFS
 */

#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <LittleFS.h>

#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <math.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <ImprovWiFiLibrary.h>

// Improv Serial WiFi
ImprovWiFi improvSerial(&Serial);

// WiFi credentials storage
String wifi_ssid = "";
String wifi_password = "";

// Authentication credentials
String auth_username = "admin";
String auth_password = "admin";
bool auth_enabled = true;

// WiFi credentials (fallback if Improv not configured)
const char* default_ssid = "";
const char* default_password = "";

// Network configuration
IPAddress local_IP;
String subnetBase = "10.10.100.";
int startOctet   = 100;
int endOctet     = 140;
int maxParallelScans = 12;
int scanTimeoutMs = 1000;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;

struct DeviceInfo {
  String ip;
  String deviceName;
  String friendlyName;
  String module;
  String cachedState;
  unsigned long lastStateUpdate;
};

std::vector<DeviceInfo> tasmotaDevices;
std::vector<uint32_t> webClients;
SemaphoreHandle_t ipListMutex;
SemaphoreHandle_t clientListMutex;
TaskHandle_t scanTaskHandle = NULL;
bool isScanning = false;
int totalScannedIPs = 0;

void scanRangeTask(void* parameter);
void broadcastDevices();
void broadcastToWebClients(const String& message);
void loadConfig();
void saveConfig();
void loadDevicesFromFile();
void removeDevice(const String& ip);

bool downloadFileToLittleFS(const String& url, const String& filename, AsyncWebSocketClient* client) {
  Serial.print(F("Downloading: "));
  Serial.println(url);
  
  // Send initial status
  if (client) {
    JsonDocument statusDoc;
    statusDoc["type"] = "download_progress";
    statusDoc["file"] = filename;
    statusDoc["status"] = "starting";
    statusDoc["progress"] = 0;
    String statusJson;
    serializeJson(statusDoc, statusJson);
    client->text(statusJson);
  }
  
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    File file = LittleFS.open("/" + filename, "w");
    if (!file) {
      Serial.println(F("Failed to open file for writing"));
      http.end();
      
      if (client) {
        JsonDocument statusDoc;
        statusDoc["type"] = "download_progress";
        statusDoc["file"] = filename;
        statusDoc["status"] = "error";
        statusDoc["message"] = "Failed to open file";
        String statusJson;
        serializeJson(statusDoc, statusJson);
        client->text(statusJson);
      }
      return false;
    }
    
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buff[128];
    int len = http.getSize();
    int bytesWritten = 0;
    int lastProgress = 0;
    
    while (http.connected() && (len > 0 || len == -1)) {
      size_t size = stream->available();
      if (size) {
        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
        file.write(buff, c);
        bytesWritten += c;
        if (len > 0) len -= c;
        
        // Send progress updates
        if (client && len > 0) {
          int progress = (bytesWritten * 100) / http.getSize();
          if (progress != lastProgress && progress % 10 == 0) { // Update every 10%
            JsonDocument statusDoc;
            statusDoc["type"] = "download_progress";
            statusDoc["file"] = filename;
            statusDoc["status"] = "downloading";
            statusDoc["progress"] = progress;
            statusDoc["bytes"] = bytesWritten;
            statusDoc["total"] = http.getSize();
            String statusJson;
            serializeJson(statusDoc, statusJson);
            client->text(statusJson);
            lastProgress = progress;
          }
        }
      }
      delay(1);
    }
    
    file.close();
    http.end();
    
    Serial.print(F("Download complete: "));
    Serial.print(bytesWritten);
    Serial.println(F(" bytes"));
    
    // Send completion status
    if (client) {
      JsonDocument statusDoc;
      statusDoc["type"] = "download_progress";
      statusDoc["file"] = filename;
      statusDoc["status"] = "complete";
      statusDoc["progress"] = 100;
      statusDoc["bytes"] = bytesWritten;
      String statusJson;
      serializeJson(statusDoc, statusJson);
      client->text(statusJson);
    }
    
    return true;
  } else {
    Serial.print(F("Download failed, HTTP code: "));
    Serial.println(httpCode);
    http.end();
    
    if (client) {
      JsonDocument statusDoc;
      statusDoc["type"] = "download_progress";
      statusDoc["file"] = filename;
      statusDoc["status"] = "error";
      statusDoc["message"] = "HTTP " + String(httpCode);
      String statusJson;
      serializeJson(statusDoc, statusJson);
      client->text(statusJson);
    }
    
    return false;
  }
}

String urlEncode(const String& str) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encoded += "%20";
    } else if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

void broadcastDevicesInstant() {
  Serial.println(F("Broadcasting devices instantly (cached states only)"));
  
  esp_task_wdt_reset();
  JsonDocument doc;
  JsonArray list = doc["list"].to<JsonArray>();
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (auto& device : tasmotaDevices) {
      JsonObject dev = list.add<JsonObject>();
      dev["ip"] = device.ip;
      dev["devicename"] = device.deviceName;
      dev["friendlyname"] = device.friendlyName;
      dev["module"] = device.module;
      dev["state"] = device.cachedState.length() > 0 ? device.cachedState : "UNKNOWN";
    }
    xSemaphoreGive(ipListMutex);
  }
  
  doc["type"] = "devices";
  String json;
  serializeJson(doc, json);
  broadcastToWebClients(json);
  
  Serial.print(F("Instant broadcast complete. Total devices: "));
  Serial.println(tasmotaDevices.size());
  esp_task_wdt_reset();
}

void updateDeviceStatesTask(void* parameter) {
  Serial.println(F("Updating device states in background..."));
  delay(100);
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (auto& device : tasmotaDevices) {
      esp_task_wdt_reset();
      
      if (device.cachedState.length() > 0 && (millis() - device.lastStateUpdate) < 5000) {
        Serial.print(F("  Skipping "));
        Serial.print(device.ip);
        Serial.println(F(" - state is fresh"));
        continue;
      }
      
      String state = getPowerState(device.ip);
      device.cachedState = state;
      device.lastStateUpdate = millis();
      
      JsonDocument updateDoc;
      updateDoc["type"] = "device_update";
      updateDoc["ip"] = device.ip;
      updateDoc["state"] = state;
      String updateJson;
      serializeJson(updateDoc, updateJson);
      
      xSemaphoreGive(ipListMutex);
      broadcastToWebClients(updateJson);
      delay(50);
      xSemaphoreTake(ipListMutex, portMAX_DELAY);
      
      Serial.print(F("  Updated: "));
      Serial.print(device.ip);
      Serial.print(F(" -> "));
      Serial.println(state);
    }
    xSemaphoreGive(ipListMutex);
  }
  
  Serial.println(F("Background state update complete"));
  vTaskDelete(NULL);
}

// Serve HTML page
void handleRoot(AsyncWebServerRequest *request) {
  // Check authentication if enabled
  if (auth_enabled) {
    if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
      return request->requestAuthentication();
    }
  }
  
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  
  response->print("<!DOCTYPE HTML><html><head>");
  response->print("<meta charset='UTF-8'>");
  response->print("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  response->print("<title>Tasmota Lights Control</title>");
  response->print("<style>");
  response->print("body{font-family:Arial,sans-serif;text-align:center;background:"); 
  response->print("#111;color:#eee;padding:10px 10px 60px}");
  response->print("h1{color:#0f8;margin:10px 0}");
  response->print(".control-panel{max-width:1200px;margin:0 auto 20px;display:flex;gap:10px;flex-wrap:wrap;justify-content:center}");
  response->print(".devices-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:10px;max-width:1200px;margin:0 auto}");
  response->print(".device{padding:8px;background:#666;border-radius:6px;position:relative}");
  response->print(".device-header{width:calc(100% + 3px);padding:6px;border-radius:4px 4px 0 0;margin:-8px -8px 6px -8px;font-weight:bold;font-size:14px;cursor:pointer}");
  response->print(".on .device-header{background:#4CAF50}");
  response->print(".off .device-header{background:#f44336}");
  response->print(".device-name{font-size:13px;margin:2px 0;color:#fff}");
  response->print(".device-ip{font-size:11px;margin:2px 0;color:#4da6ff;cursor:pointer;text-decoration:underline}");
  response->print(".device-ip:hover{color:#80c1ff}");
  response->print(".device-module{font-size:10px;margin:2px 0;color:#aaa;font-style:italic}");
  response->print("button{padding:6px 16px;font-size:14px;margin:4px;border:none;border-radius:4px;cursor:pointer}");
  response->print(".scan{background:#2196F3;color:white;padding:8px 20px;font-size:16px}");
  response->print(".scan:disabled{background:#666;cursor:not-allowed}");
  response->print(".all-on{background:#4CAF50;color:white;padding:8px 20px;font-size:16px}");
  response->print(".all-off{background:#f44336;color:white;padding:8px 20px;font-size:16px}");
  response->print(".config-btn{background:#FF9800;color:white;padding:8px 20px;font-size:16px}");
  response->print(".toggle{width:calc(50% - 2px);background:#555;color:white;margin-top:4px;display:inline-block}");
  response->print(".toggle:hover{background:#777}");
  response->print(".edit-btn{width:calc(50% - 2px);background:#555;color:white;margin-top:4px;display:inline-block;font-size:13px}");
  response->print(".edit-btn:hover{background:#FFA726}");
  response->print(".edit-btn.hidden{display:none}");
  response->print(".remove-btn{position:absolute;top:2px;right:2px;background:#f44336;color:white;padding:2px 6px;font-size:11px;border-radius:3px;cursor:pointer;z-index:10}");
  response->print(".remove-btn:hover{background:#d32f2f}");
  response->print(".remove-btn.hidden{display:none}");
  response->print(".progress-section{margin:15px;padding:10px;background:#333;border-radius:5px}");
  response->print(".progress-bar{width:100%;height:18px;background:#444;border-radius:10px;overflow:hidden}");
  response->print(".progress-fill{height:100%;background:#0f8;width:0%;transition:width 0.3s}");
  response->print(".progress-section p{margin:8px 0;font-size:14px}");
  response->print(".modal{display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;background:rgba(0,0,0,0.8);overflow-y:auto}");
  response->print(".modal-content{background:#222;margin:20px auto;padding:20px;border:1px solid #888;width:90%;max-width:600px;border-radius:10px;color:#eee;max-height:90vh;overflow-y:auto}");
  response->print(".modal-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}");
  response->print(".close{color:#aaa;font-size:28px;font-weight:bold;cursor:pointer}");
  response->print(".close:hover{color:#fff}");
  response->print(".config-section{margin:20px 0;text-align:left}");
  response->print(".config-section h3{color:#0f8;margin-bottom:10px}");
  response->print(".form-group{margin:15px 0}");
  response->print(".form-group label{display:block;margin-bottom:5px;color:#aaa}");
  response->print(".form-group input{width:100%;padding:8px;border:1px solid #555;border-radius:4px;background:#333;color:#eee;font-size:14px;box-sizing:border-box}");
  response->print(".preset-buttons{display:flex;gap:10px;flex-wrap:wrap;margin-top:10px}");
  response->print(".preset-btn{background:#555;color:white;padding:6px 12px;border:none;border-radius:4px;cursor:pointer;font-size:13px}");
  response->print(".preset-btn:hover{background:#777}");
  response->print(".save-btn{background:#4CAF50;color:white;padding:10px 30px;font-size:16px;margin-top:20px;border:none;border-radius:4px;cursor:pointer}");
  response->print(".footer{position:fixed;bottom:0;left:0;right:0;background:#222;padding:12px;border-top:2px solid #0f8;text-align:center}");
  response->print(".footer a{color:#4da6ff;text-decoration:none;font-size:13px}");
  response->print(".footer a:hover{color:#80c1ff;text-decoration:underline}");
  response->print(".file-links{margin:20px auto;max-width:1200px;display:flex;gap:10px;justify-content:center;flex-wrap:wrap}");
  response->print(".file-link{background:#555;color:white;padding:8px 16px;border-radius:4px;text-decoration:none;font-size:14px}");
  response->print(".file-link:hover{background:#777}");
  response->print(".download-popover{display:none;position:absolute;background:#1a1a1a;border:2px solid #0f8;border-radius:8px;padding:20px;");
  response->print("min-width:300px;box-shadow:0 4px 20px rgba(0,0,0,0.5);z-index:2000;color:#eee}");
  response->print(".download-popover.show{display:block}");
  response->print(".download-popover h4{color:#0f8;margin:0 0 15px 0;font-size:16px}");
  response->print(".download-item{margin:10px 0;padding:8px;background:#2a2a2a;border-radius:4px}");
  response->print(".download-file-name{font-weight:bold;margin-bottom:5px;font-size:13px}");
  response->print(".download-status{font-size:12px;color:#aaa;margin-bottom:5px}");
  response->print(".download-progress-bar{height:6px;background:#444;border-radius:3px;overflow:hidden;margin-top:5px}");
  response->print(".download-progress-fill{height:100%;background:#0f8;width:0%;transition:width 0.3s}");
  response->print(".download-complete{color:#0f8}");
  response->print(".download-error{color:#f44336}");
  response->print("</style></head><body>");
  
  response->print("<h1>Tasmota Lights Controller</h1>");
  response->print("<div class='control-panel'>");
  response->print("<button class='scan' id='scanBtn'>Scan Network</button>");
  response->print("<button class='config-btn' id='configBtn'>Settings</button>");
  response->print("<button class='all-on' id='allOnBtn'>All ON</button>");
  response->print("<button class='all-off' id='allOffBtn'>All OFF</button>");
  response->print("<button class='config-btn' id='toggleEditBtn'>Toggle Edit</button>");
  response->print("</div>");
  
  response->print("<div id='progress' class='progress-section' style='display:none'>");
  response->print("<p>Scanning: <span id='currentIP'>-</span></p>");
  response->print("<div class='progress-bar'><div class='progress-fill' id='progressFill'></div></div>");
  response->print("<p id='foundCount'>Found: 0 devices</p>");
  response->print("<p id='statusMsg'>Status: Idle</p>");
  response->print("</div>");
  
  response->print("<div class='devices-grid' id='devices'></div>");
  
  response->print("<div class='file-links'>");
  response->print("<a href='/timer.html' class='file-link' target='Timer'>Timer</a>");
  response->print("<a href='/ace.html' class='file-link' target='Ace'>Ace</a>");
  response->print("</div>");
  
  response->print("<div class='footer' id='footerContent'>");
  response->print("<a href='https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/tree/main/esp32_tasmota_scan' target='_blank'>");
  response->print("GitHub Repository - ESP32 Tasmota Scanner</a></div>");
  
  response->print("<div id='configModal' class='modal'>");
  response->print("<div class='modal-content'>");
  response->print("<div class='modal-header'><h2>Network Configuration</h2>");
  response->print("<span class='close' id='closeBtn'>&times;</span></div>");
  response->print("<div class='config-section'><h3>Quick Presets</h3>");
  response->print("<div class='preset-buttons' id='presets'></div></div>");
  response->print("<div class='config-section'><h3>Custom Network Range</h3>");
  response->print("<div class='form-group'><label>Subnet Base (e.g., 192.168.1)</label>");
  response->print("<input type='text' id='subnetBase' placeholder='192.168.1'></div>");
  response->print("<div class='form-group'><label>Start IP (Last Octet)</label>");
  response->print("<input type='number' id='startOctet' min='1' max='254' placeholder='2'></div>");
  response->print("<div class='form-group'><label>End IP (Last Octet)</label>");
  response->print("<input type='number' id='endOctet' min='1' max='254' placeholder='254'></div>");
  response->print("<p style='color:#aaa;font-size:12px;margin-top:10px'>Will scan from <span id='rangePreview'>-</span></p></div>");
  response->print("<div class='config-section'><h3>Scan Settings</h3>");
  response->print("<div class='form-group'><label>Parallel Scans (1-20)</label>");
  response->print("<input type='number' id='maxScans' min='1' max='20' placeholder='12'></div>");
  response->print("<div class='form-group'><label>Timeout per IP (ms)</label>");
  response->print("<input type='number' id='scanTimeout' min='500' max='5000' step='100' placeholder='1000'></div>");
  response->print("<p style='color:#aaa;font-size:12px;margin-top:5px'>More parallel scans = faster but uses more memory. Lower timeout = faster but may miss devices.</p></div>");
  response->print("<div class='config-section'><h3>Security Settings</h3>");
  response->print("<div class='form-group'><label>Enable Login</label>");
  response->print("<input type='checkbox' id='authEnabled' style='width:auto;margin-left:10px'></div>");
  response->print("<div class='form-group'><label>Username</label>");
  response->print("<input type='text' id='authUsername' placeholder='admin'></div>");
  response->print("<div class='form-group'><label>Password</label>");
  response->print("<input type='password' id='authPassword' placeholder='Enter new password'></div>");
  response->print("<div class='form-group'><label>Confirm Password</label>");
  response->print("<input type='password' id='authPasswordConfirm' placeholder='Confirm password'></div>");
  response->print("<p style='color:#aaa;font-size:12px;margin-top:5px'>Leave password fields empty to keep current password. Default: admin/admin</p></div>");
  response->print("<div class='config-section'><h3>Download Files</h3>");
  response->print("<div style='position:relative'>");
  response->print("<button class='save-btn' id='downloadFilesBtn'>Download Timer & Ace Files</button>");
  response->print("<div id='downloadPopover' class='download-popover'>");
  response->print("<h4>Download Progress</h4>");
  response->print("<div id='downloadItems'></div>");
  response->print("</div>");
  response->print("</div>");
  response->print("<p style='color:#aaa;font-size:12px;margin-top:10px'>Downloads timer.html and ace.html from GitHub to local storage</p></div>");
  response->print("<button class='save-btn' id='saveBtn'>Save and Apply</button>");
  response->print("</div></div>");
  
  response->print("<div id='editModal' class='modal'>");
  response->print("<div class='modal-content'>");
  response->print("<div class='modal-header'><h2>Edit Device Names</h2>");
  response->print("<span class='close' id='editCloseBtn'>&times;</span></div>");
  response->print("<div class='config-section'>");
  response->print("<div class='form-group'><label>Friendly Name (displayed name)</label>");
  response->print("<input type='text' id='editFriendlyName' placeholder='Friendly Name'></div>");
  response->print("<div class='form-group'><label>Device Name (internal name)</label>");
  response->print("<input type='text' id='editDeviceName' placeholder='Device Name'></div>");
  response->print("<input type='hidden' id='editDeviceIP'>");
  response->print("<p style='color:#aaa;font-size:12px;margin-top:10px'>Changes will be saved to the Tasmota device</p></div>");
  response->print("<button class='save-btn' id='saveNamesBtn'>Save Names</button>");
  response->print("</div></div>");
  
  // JavaScript
  response->print("<script>");
  response->print("var ws,wsConnected=false,editingIP='',editButtonsVisible=true;");
  response->print("function loadEditButtonState(){var saved=localStorage.getItem('editButtonsVisible');");
  response->print("if(saved!==null){editButtonsVisible=saved==='true';updateEditButtonsDisplay()}}");
  response->print("function saveEditButtonState(){localStorage.setItem('editButtonsVisible',editButtonsVisible)}");
  response->print("function updateEditButtonsDisplay(){var editBtns=document.querySelectorAll('.edit-btn');");
  response->print("var removeBtns=document.querySelectorAll('.remove-btn');");
  response->print("for(var i=0;i<editBtns.length;i++){");
  response->print("if(editButtonsVisible){editBtns[i].classList.remove('hidden')}");
  response->print("else{editBtns[i].classList.add('hidden')}}");
  response->print("for(var i=0;i<removeBtns.length;i++){");
  response->print("if(editButtonsVisible){removeBtns[i].classList.remove('hidden')}");
  response->print("else{removeBtns[i].classList.add('hidden')}}}");
  response->print("function vibrate(d){d=d||100;if(navigator.vibrate)navigator.vibrate(d)}");
  
  // Load external footer
  response->print("function loadExternalFooter(){");
  response->print("fetch('https://ldijkman.github.io/Swipe-ESP32-Timer-Switch/burn/footer.html')");
  response->print(".then(response=>response.text())");
  response->print(".then(html=>{document.getElementById('footerContent').innerHTML=html})");
  response->print(".catch(err=>{console.log('Could not load external footer, using default')});}");
  response->print("loadExternalFooter();");
  
  response->print("function connectWebSocket(){console.log('Connecting...');");
  response->print("ws=new WebSocket('ws://'+location.hostname+'/ws');");
  response->print("ws.onopen=function(){console.log('Connected');wsConnected=true;");
  response->print("document.getElementById('scanBtn').disabled=false;");
  response->print("ws.send(JSON.stringify({action:'get_config'}))};");
  
  response->print("ws.onmessage=function(e){console.log('Received:',e.data);var d=JSON.parse(e.data);");
  response->print("if(d.type==='config'){document.getElementById('subnetBase').value=d.subnet||'';");
  response->print("document.getElementById('startOctet').value=d.start||2;");
  response->print("document.getElementById('endOctet').value=d.end||254;");
  response->print("document.getElementById('maxScans').value=d.maxScans||12;");
  response->print("document.getElementById('scanTimeout').value=d.timeout||1000;");
  response->print("document.getElementById('authEnabled').checked=d.authEnabled!==false;");
  response->print("document.getElementById('authUsername').value=d.authUser||'admin';updateRangePreview()}");
  response->print("else if(d.type==='devices'){var html='';for(var i=0;i<d.list.length;i++){");
  response->print("var dev=d.list[i];var sc=dev.state.indexOf('ON')>=0?'on':'off';");
  response->print("var dn=dev.devicename||'Unknown';var fn=dev.friendlyname||dn;");
  response->print("var mod=dev.module||'';");
  response->print("html+=\"<div class='device \"+sc+\"' id='device-\"+dev.ip.replace(/\\./g,'-')+\"'>\";");
  response->print("html+=\"<span class='remove-btn\"+(editButtonsVisible?\"\":\" hidden\")+\"' data-ip='\"+dev.ip+\"'>×</span>\";");
  response->print("html+=\"<div class='device-header' data-ip='\"+dev.ip+\"'>\"+fn+\"</div>\";");
  response->print("html+=\"<div class='device-name'>\"+dn+\"</div>\";");
  response->print("html+=\"<div class='device-ip' data-ip='\"+dev.ip+\"'>\"+dev.ip+\"</div>\";");
  response->print("if(mod)html+=\"<div class='device-module'>\"+mod+\"</div>\";");
  response->print("html+=\"<button class='toggle' data-ip='\"+dev.ip+\"'>Toggle</button>\";");
  response->print("html+=\"<button class='edit-btn\"+(editButtonsVisible?\"\":\" hidden\")+\"' data-ip='\"+dev.ip+\"' data-friendly='\"+fn.replace(/'/g,'&apos;')+\"' data-device='\"+dn.replace(/'/g,'&apos;')+\"'>Edit</button></div>\"}");
  response->print("document.getElementById('devices').innerHTML=html||'<p>No devices found</p>';");
  response->print("document.getElementById('scanBtn').disabled=false;");
  response->print("document.getElementById('progress').style.display='none';");
  response->print("document.getElementById('statusMsg').textContent='Status: Scan complete';attachDeviceHandlers()}");
  response->print("else if(d.type==='device_update'){var did='device-'+d.ip.replace(/\\./g,'-');");
  response->print("var ddiv=document.getElementById(did);if(ddiv){");
  response->print("var sc=d.state.indexOf('ON')>=0?'on':'off';ddiv.className='device '+sc}}");
  response->print("else if(d.type==='scan_progress'){document.getElementById('progress').style.display='block';");
  response->print("document.getElementById('currentIP').textContent=d.current_ip;");
  response->print("document.getElementById('progressFill').style.width=d.progress+'%';");
  response->print("document.getElementById('foundCount').textContent='Found: '+d.found+' devices';");
  response->print("document.getElementById('statusMsg').textContent='Status: Scanning... '+d.progress.toFixed(1)+'%'}");
  response->print("else if(d.type==='scan_start'){document.getElementById('scanBtn').disabled=true;");
  response->print("document.getElementById('devices').innerHTML='<p>Scanning...</p>';");
  response->print("document.getElementById('progress').style.display='block';");
  response->print("document.getElementById('statusMsg').textContent='Status: Starting scan...'}");
  response->print("else if(d.type==='download_progress'){updateDownloadProgress(d)}");
  response->print("else if(d.type==='download_complete'){");
  response->print("setTimeout(function(){document.getElementById('downloadPopover').classList.remove('show')},3000);");
  response->print("if(d.success){vibrate(200)}else{vibrate([100,50,100])}}");
  response->print("else if(d.type==='info')console.log('Info:',d.msg)};");
  
  response->print("ws.onerror=function(e){console.error('WS error:',e);wsConnected=false};");
  response->print("ws.onclose=function(){console.log('WS closed');wsConnected=false;setTimeout(connectWebSocket,2000)}}");
  
  response->print("function attachDeviceHandlers(){var ips=document.querySelectorAll('.device-ip');");
  response->print("for(var i=0;i<ips.length;i++)ips[i].addEventListener('click',function(){");
  response->print("vibrate(100);window.open('http://'+this.getAttribute('data-ip'),'_blank')});");
  response->print("var hdrs=document.querySelectorAll('.device-header');for(var i=0;i<hdrs.length;i++)");
  response->print("hdrs[i].addEventListener('click',function(){vibrate(100);");
  response->print("var ip=this.getAttribute('data-ip');");
  response->print("if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'toggle',ip:ip}))});");
  response->print("var btns=document.querySelectorAll('.toggle');for(var i=0;i<btns.length;i++)");
  response->print("btns[i].addEventListener('click',function(e){e.stopPropagation();vibrate(100);");
  response->print("var ip=this.getAttribute('data-ip');if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'toggle',ip:ip}))});");
  response->print("var editBtns=document.querySelectorAll('.edit-btn');for(var i=0;i<editBtns.length;i++)");
  response->print("editBtns[i].addEventListener('click',function(e){e.stopPropagation();vibrate(100);");
  response->print("editingIP=this.getAttribute('data-ip');");
  response->print("document.getElementById('editDeviceIP').value=editingIP;");
  response->print("document.getElementById('editFriendlyName').value=this.getAttribute('data-friendly');");
  response->print("document.getElementById('editDeviceName').value=this.getAttribute('data-device');");
  response->print("document.getElementById('editModal').style.display='block';");
  response->print("document.body.style.overflow='hidden'});");
  response->print("var removeBtns=document.querySelectorAll('.remove-btn');for(var i=0;i<removeBtns.length;i++)");
  response->print("removeBtns[i].addEventListener('click',function(e){e.stopPropagation();vibrate(100);");
  response->print("var ip=this.getAttribute('data-ip');");
  response->print("if(confirm('Remove device '+ip+'?')){");
  response->print("if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'remove',ip:ip}))}})}");
  
  response->print("function toggleEditButtons(){vibrate(100);editButtonsVisible=!editButtonsVisible;");
  response->print("saveEditButtonState();updateEditButtonsDisplay()}");
  
  response->print("function scan(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'scan'}));document.getElementById('scanBtn').disabled=true}");
  response->print("function allOn(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'all_on'}))}");
  response->print("function allOff(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'all_off'}))}");
  response->print("function openConfig(){vibrate(100);document.getElementById('configModal').style.display='block';");
  response->print("document.body.style.overflow='hidden';updateRangePreview()}");
  response->print("function closeConfig(){vibrate(100);document.getElementById('configModal').style.display='none';");
  response->print("document.body.style.overflow='auto'}");
  response->print("function closeEditModal(){vibrate(100);document.getElementById('editModal').style.display='none';");
  response->print("document.body.style.overflow='auto'}");
  response->print("function setPreset(s,st,e){vibrate(100);document.getElementById('subnetBase').value=s;");
  response->print("document.getElementById('startOctet').value=st;document.getElementById('endOctet').value=e;updateRangePreview()}");
  response->print("function updateRangePreview(){var s=document.getElementById('subnetBase').value;");
  response->print("var st=document.getElementById('startOctet').value;var e=document.getElementById('endOctet').value;");
  response->print("document.getElementById('rangePreview').textContent=s+'.'+st+' to '+s+'.'+e}");
  response->print("var downloadItems={};");
  response->print("function updateDownloadProgress(d){");
  response->print("var popover=document.getElementById('downloadPopover');");
  response->print("var container=document.getElementById('downloadItems');");
  response->print("if(!downloadItems[d.file]){");
  response->print("var itemHtml='<div id=\"dl-'+d.file+'\" class=\"download-item\">';");
  response->print("itemHtml+='<div class=\"download-file-name\">'+d.file+'</div>';");
  response->print("itemHtml+='<div class=\"download-status\" id=\"status-'+d.file+'\">Starting...</div>';");
  response->print("itemHtml+='<div class=\"download-progress-bar\"><div class=\"download-progress-fill\" id=\"progress-'+d.file+'\"></div></div>';");
  response->print("itemHtml+='</div>';");
  response->print("container.innerHTML+=itemHtml;");
  response->print("downloadItems[d.file]=true}");
  response->print("var statusEl=document.getElementById('status-'+d.file);");
  response->print("var progressEl=document.getElementById('progress-'+d.file);");
  response->print("if(d.status==='starting'){statusEl.textContent='Starting...';statusEl.className='download-status'}");
  response->print("else if(d.status==='downloading'){statusEl.textContent='Downloading: '+d.progress+'%';progressEl.style.width=d.progress+'%'}");
  response->print("else if(d.status==='complete'){statusEl.textContent='✓ Complete ('+d.bytes+' bytes)';");
  response->print("statusEl.className='download-status download-complete';progressEl.style.width='100%'}");
  response->print("else if(d.status==='error'){statusEl.textContent='✗ Error: '+d.message;");
  response->print("statusEl.className='download-status download-error';progressEl.style.width='0%'}}");
  response->print("function downloadFiles(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("if(confirm('Download timer.html and ace.html from GitHub?')){");
  response->print("downloadItems={};");
  response->print("document.getElementById('downloadItems').innerHTML='';");
  response->print("var popover=document.getElementById('downloadPopover');");
  response->print("popover.classList.add('show');");
  response->print("var btn=document.getElementById('downloadFilesBtn');");
  response->print("var rect=btn.getBoundingClientRect();");
  response->print("popover.style.top=(rect.bottom+10)+'px';");
  response->print("popover.style.left=rect.left+'px';");
  response->print("ws.send(JSON.stringify({action:'download_files'}))}}");
  response->print("function saveConfig(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("var s=document.getElementById('subnetBase').value;");
  response->print("var st=parseInt(document.getElementById('startOctet').value);");
  response->print("var e=parseInt(document.getElementById('endOctet').value);");
  response->print("var ms=parseInt(document.getElementById('maxScans').value);");
  response->print("var to=parseInt(document.getElementById('scanTimeout').value);");
  response->print("var authEn=document.getElementById('authEnabled').checked;");
  response->print("var authUser=document.getElementById('authUsername').value;");
  response->print("var authPass=document.getElementById('authPassword').value;");
  response->print("var authPassConf=document.getElementById('authPasswordConfirm').value;");
  response->print("if(!s||isNaN(st)||isNaN(e)){alert('Invalid input');return}");
  response->print("if(st<1||st>254||e<1||e>254||st>e){alert('Invalid range');return}");
  response->print("if(isNaN(ms)||ms<1||ms>20){alert('Parallel scans must be 1-20');return}");
  response->print("if(isNaN(to)||to<500||to>5000){alert('Timeout must be 500-5000ms');return}");
  response->print("if(!authUser){alert('Username cannot be empty');return}");
  response->print("if(authPass&&authPass!==authPassConf){alert('Passwords do not match');return}");
  response->print("var cfg={action:'save_config',subnet:s,start:st,end:e,maxScans:ms,timeout:to,authEnabled:authEn,authUser:authUser};");
  response->print("if(authPass)cfg.authPass=authPass;");
  response->print("ws.send(JSON.stringify(cfg));");
  response->print("closeConfig();alert('Configuration saved! If you changed auth settings, you may need to log in again.')}");
  
  response->print("function saveNames(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("var ip=document.getElementById('editDeviceIP').value;");
  response->print("var fn=document.getElementById('editFriendlyName').value;");
  response->print("var dn=document.getElementById('editDeviceName').value;");
  response->print("if(!fn||!dn){alert('Please fill in both names');return}");
  response->print("ws.send(JSON.stringify({action:'rename',ip:ip,friendlyname:fn,devicename:dn}));");
  response->print("closeEditModal();setTimeout(function(){ws.send(JSON.stringify({action:'refresh'}))},1000)}");
  
  response->print("loadEditButtonState();");
  response->print("connectWebSocket();");
  response->print("document.getElementById('scanBtn').addEventListener('click',scan);");
  response->print("document.getElementById('allOnBtn').addEventListener('click',allOn);");
  response->print("document.getElementById('allOffBtn').addEventListener('click',allOff);");
  response->print("document.getElementById('configBtn').addEventListener('click',openConfig);");
  response->print("document.getElementById('toggleEditBtn').addEventListener('click',toggleEditButtons);");
  response->print("document.getElementById('closeBtn').addEventListener('click',closeConfig);");
  response->print("document.getElementById('editCloseBtn').addEventListener('click',closeEditModal);");
  response->print("document.getElementById('saveBtn').addEventListener('click',saveConfig);");
  response->print("document.getElementById('saveNamesBtn').addEventListener('click',saveNames);");
  response->print("document.getElementById('downloadFilesBtn').addEventListener('click',downloadFiles);");
  
  response->print("var presets=[");
  response->print("{name:'192.168.1.x',subnet:'192.168.1',start:2,end:254},");
  response->print("{name:'192.168.0.x',subnet:'192.168.0',start:2,end:254},");
  response->print("{name:'192.168.2.x',subnet:'192.168.2',start:2,end:254},");
  response->print("{name:'10.0.0.x',subnet:'10.0.0',start:2,end:254},");
  response->print("{name:'10.0.1.x',subnet:'10.0.1',start:2,end:254},");
  response->print("{name:'10.10.100.x',subnet:'10.10.100',start:100,end:140}];");
  response->print("var pc=document.getElementById('presets');");
  response->print("for(var i=0;i<presets.length;i++){var btn=document.createElement('button');");
  response->print("btn.className='preset-btn';btn.textContent=presets[i].name;");
  response->print("btn.setAttribute('data-subnet',presets[i].subnet);");
  response->print("btn.setAttribute('data-start',presets[i].start);");
  response->print("btn.setAttribute('data-end',presets[i].end);");
  response->print("btn.addEventListener('click',function(){setPreset(this.getAttribute('data-subnet'),");
  response->print("this.getAttribute('data-start'),this.getAttribute('data-end'))});pc.appendChild(btn)}");
  
  response->print("document.getElementById('subnetBase').addEventListener('input',updateRangePreview);");
  response->print("document.getElementById('startOctet').addEventListener('input',updateRangePreview);");
  response->print("document.getElementById('endOctet').addEventListener('input',updateRangePreview);");
  response->print("window.onclick=function(e){var cm=document.getElementById('configModal');");
  response->print("var em=document.getElementById('editModal');");
  response->print("if(e.target==cm){closeConfig()}");
  response->print("if(e.target==em){closeEditModal()}}");
  response->print("</script></body></html>");
  
  request->send(response);
}

// Configuration Functions
void loadConfig() {
  preferences.begin("tasmota-scan", false);
  String savedSubnet = preferences.getString("subnet", "");
  if (savedSubnet.length() > 0) subnetBase = savedSubnet;
  startOctet = preferences.getInt("start", 100);
  endOctet = preferences.getInt("end", 140);
  maxParallelScans = preferences.getInt("maxScans", 12);
  scanTimeoutMs = preferences.getInt("timeout", 1000);
  
  // Load WiFi credentials
  wifi_ssid = preferences.getString("wifi_ssid", "");
  wifi_password = preferences.getString("wifi_pass", "");
  
  // Load auth credentials
  auth_username = preferences.getString("auth_user", "admin");
  auth_password = preferences.getString("auth_pass", "admin");
  auth_enabled = preferences.getBool("auth_enabled", true);
  
  preferences.end();
}

void saveConfig() {
  preferences.begin("tasmota-scan", false);
  preferences.putString("subnet", subnetBase);
  preferences.putInt("start", startOctet);
  preferences.putInt("end", endOctet);
  preferences.putInt("maxScans", maxParallelScans);
  preferences.putInt("timeout", scanTimeoutMs);
  preferences.end();
}

void saveWiFiConfig(String ssid, String password) {
  preferences.begin("tasmota-scan", false);
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_pass", password);
  preferences.end();
  
  wifi_ssid = ssid;
  wifi_password = password;
}

void saveAuthConfig(String username, String password, bool enabled) {
  preferences.begin("tasmota-scan", false);
  preferences.putString("auth_user", username);
  preferences.putString("auth_pass", password);
  preferences.putBool("auth_enabled", enabled);
  preferences.end();
  
  auth_username = username;
  auth_password = password;
  auth_enabled = enabled;
  
  Serial.println(F("Auth credentials updated"));
}

void loadDevicesFromFile() {
  Serial.println(F("Loading devices from /devices.json..."));
  
  File file = LittleFS.open("/devices.json", "r");
  if (!file) {
    Serial.println(F("No devices.json file found - will scan on first use"));
    return;
  }
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.print(F("Failed to parse devices.json: "));
    Serial.println(error.c_str());
    return;
  }
  
  JsonArray devicesArray = doc["devices"];
  if (!devicesArray) {
    Serial.println(F("No devices array in JSON"));
    return;
  }
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    tasmotaDevices.clear();
    
    for (JsonObject deviceObj : devicesArray) {
      DeviceInfo device;
      device.ip = deviceObj["ip"] | "";
      device.deviceName = deviceObj["devicename"] | "Unknown";
      device.friendlyName = deviceObj["friendlyname"] | "";
      device.module = deviceObj["module"] | "";
      device.cachedState = "";
      device.lastStateUpdate = 0;
      
      if (device.ip.length() > 0) {
        tasmotaDevices.push_back(device);
      }
    }
    
    xSemaphoreGive(ipListMutex);
  }
  
  Serial.print(F("Loaded "));
  Serial.print(tasmotaDevices.size());
  Serial.println(F(" devices from devices.json"));
}

void removeDevice(const String& ip) {
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    auto it = tasmotaDevices.begin();
    while (it != tasmotaDevices.end()) {
      if (it->ip == ip) {
        Serial.print(F("Removing device: "));
        Serial.print(ip);
        Serial.print(F(" ("));
        Serial.print(it->friendlyName);
        Serial.println(F(")"));
        it = tasmotaDevices.erase(it);
        break;
      } else {
        ++it;
      }
    }
    xSemaphoreGive(ipListMutex);
  }
  saveDevicesToFile();
}

String getDeviceName(const String& ip) {
  esp_task_wdt_reset();
  HTTPClient http;
  http.setTimeout(2000);
  http.begin("http://" + ip + "/cm?cmnd=DeviceName");
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    response.trim();
    int jsonStart = response.indexOf('{');
    if (jsonStart > 0 && jsonStart < 10) response = response.substring(jsonStart);
    int start = response.indexOf("\"DeviceName\":\"");
    if (start >= 0) {
      start += 14;
      int end = response.indexOf("\"", start);
      if (end > start) return response.substring(start, end);
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
  http.begin("http://" + ip + "/cm?cmnd=FriendlyName1");
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    response.trim();
    int jsonStart = response.indexOf('{');
    if (jsonStart > 0 && jsonStart < 10) response = response.substring(jsonStart);
    int start = response.indexOf("\"FriendlyName1\":\"");
    if (start >= 0) {
      start += 17;
      int end = response.indexOf("\"", start);
      if (end > start) return response.substring(start, end);
    }
  } else {
    http.end();
  }
  return "";
}

String getModule(const String& ip) {
  esp_task_wdt_reset();
  HTTPClient http;
  http.setTimeout(2000);
  http.begin("http://" + ip + "/cm?cmnd=Module");
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    response.trim();
    
    int jsonStart = response.indexOf('{');
    if (jsonStart > 0 && jsonStart < 10) response = response.substring(jsonStart);
    
    int moduleStart = response.indexOf("\"Module\":{\"");
    if (moduleStart >= 0) {
      int valueStart = response.indexOf(":\"", moduleStart + 11);
      if (valueStart >= 0) {
        valueStart += 2;
        int valueEnd = response.indexOf("\"", valueStart);
        if (valueEnd > valueStart) {
          return response.substring(valueStart, valueEnd);
        }
      }
    }
  } else {
    http.end();
  }
  return "";
}

bool checkTasmota(const String& ip) {
  esp_task_wdt_reset();
  
  Serial.print(F("Checking IP: "));
  Serial.println(ip);
  
  HTTPClient http;
  http.setTimeout(scanTimeoutMs);
  http.begin("http://" + ip + "/cm?cmnd=Power");
  int httpCode = http.GET();
  esp_task_wdt_reset();
  
  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    response.trim();
    int jsonStart = response.indexOf('{');
    if (jsonStart > 0 && jsonStart < 10) response = response.substring(jsonStart);
    
    bool isTasmota = (response.indexOf("\"POWER") >= 0 && 
            (response.indexOf("\"ON\"") >= 0 || response.indexOf("\"OFF\"") >= 0));
    
    if (isTasmota) {
      Serial.print(F("  ✓ Found Tasmota at: "));
      Serial.println(ip);
      Serial.print(F("    Response: "));
      Serial.println(response);
    } else {
      Serial.print(F("  ✗ Not Tasmota at: "));
      Serial.print(ip);
      Serial.print(F(" (response: "));
      Serial.print(response.substring(0, 50));
      Serial.println(F(")"));
    }
    
    return isTasmota;
  }
  http.end();
  
  if (httpCode > 0) {
    Serial.print(F("  HTTP error "));
    Serial.print(httpCode);
    Serial.print(F(" from "));
    Serial.println(ip);
  } else {
    Serial.print(F("  Connection failed to "));
    Serial.println(ip);
  }
  
  return false;
}

void scanRangeTask(void* parameter) {
  int* range = (int*)parameter;
  int start = range[0];
  int end = range[1];
  
  Serial.print(F("[Task] Scanning range: "));
  Serial.print(start);
  Serial.print(F(" to "));
  Serial.println(end);
  
  for (int i = start; i <= end; i++) {
    esp_task_wdt_reset();
    String ip = subnetBase + String(i);
    if (checkTasmota(ip)) {
      String deviceName = getDeviceName(ip);
      String friendlyName = getFriendlyName(ip);
      String module = getModule(ip);
      if (friendlyName.length() == 0) friendlyName = deviceName;
      
      Serial.print(F("  Device: "));
      Serial.print(deviceName);
      Serial.print(F(", Friendly: "));
      Serial.print(friendlyName);
      Serial.print(F(", Module: "));
      Serial.println(module);
      
      if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
        DeviceInfo device;
        device.ip = ip;
        device.deviceName = deviceName;
        device.friendlyName = friendlyName;
        device.module = module;
        device.cachedState = "";
        device.lastStateUpdate = 0;
        tasmotaDevices.push_back(device);
        xSemaphoreGive(ipListMutex);
      }
    }
    totalScannedIPs++;
    delay(10);
  }
  delete[] range;
  Serial.println(F("[Task] Range scan complete"));
  vTaskSuspend(NULL);
}

void parallelScanTask(void* parameter) {
  Serial.println(F("[ParallelScan] Starting parallel scan..."));
  Serial.print(F("[Memory] Free heap: "));
  Serial.print(ESP.getFreeHeap());
  Serial.println(F(" bytes"));
  
  esp_task_wdt_reset();
  isScanning = true;
  
  // Store existing devices before scan
  std::vector<DeviceInfo> existingDevices;
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    existingDevices = tasmotaDevices;  // Copy existing devices
    tasmotaDevices.clear();  // Clear for fresh scan
    xSemaphoreGive(ipListMutex);
  }
  
  totalScannedIPs = 0;
  int totalIPs = endOctet - startOctet + 1;
  int ipsPerTask = ceil((float)totalIPs / maxParallelScans);
  
  Serial.print(F("[ParallelScan] Total IPs: "));
  Serial.print(totalIPs);
  Serial.print(F(", IPs per task: "));
  Serial.println(ipsPerTask);
  
  TaskHandle_t* taskHandles = new TaskHandle_t[maxParallelScans];
  memset(taskHandles, 0, sizeof(TaskHandle_t) * maxParallelScans);
  
  for (int taskNum = 0; taskNum < maxParallelScans; taskNum++) {
    int taskStart = startOctet + (taskNum * ipsPerTask);
    int taskEnd = min(startOctet + ((taskNum + 1) * ipsPerTask) - 1, endOctet);
    if (taskStart > endOctet) break;
    
    int* range = new int[2];
    range[0] = taskStart;
    range[1] = taskEnd;
    
    char taskName[20];
    snprintf(taskName, sizeof(taskName), "Scan%d", taskNum);
    
    Serial.print(F("[ParallelScan] Creating task "));
    Serial.print(taskNum);
    Serial.print(F(" for IPs "));
    Serial.print(taskStart);
    Serial.print(F("-"));
    Serial.println(taskEnd);
    
    xTaskCreatePinnedToCore(scanRangeTask, taskName, 8192, (void*)range, 1, &taskHandles[taskNum], 0);
    esp_task_wdt_reset();
  }
  
  unsigned long lastUpdate = 0;
  while (totalScannedIPs < totalIPs) {
    esp_task_wdt_reset();
    delay(100);
    
    if (millis() - lastUpdate > 500) {
      float progress = (float)totalScannedIPs / totalIPs * 100.0;
      int currentIP = startOctet + totalScannedIPs;
      
      Serial.print(F("[ParallelScan] Progress: "));
      Serial.print(progress, 1);
      Serial.print(F("% ("));
      Serial.print(totalScannedIPs);
      Serial.print(F("/"));
      Serial.print(totalIPs);
      Serial.print(F(") - IP: "));
      Serial.print(subnetBase);
      Serial.println(currentIP);
      
      JsonDocument doc;
      doc["type"] = "scan_progress";
      doc["current_ip"] = String(subnetBase + String(currentIP));
      doc["progress"] = progress;
      xSemaphoreTake(ipListMutex, portMAX_DELAY);
      doc["found"] = tasmotaDevices.size();
      xSemaphoreGive(ipListMutex);
      
      String json;
      serializeJson(doc, json);
      broadcastToWebClients(json);
      lastUpdate = millis();
    }
    
    bool allDone = true;
    for (int i = 0; i < maxParallelScans; i++) {
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
  
  Serial.println(F("[ParallelScan] Cleaning up tasks..."));
  for (int i = 0; i < maxParallelScans; i++) {
    if (taskHandles[i] != NULL) vTaskDelete(taskHandles[i]);
  }
  
  delete[] taskHandles;
  
  delay(100);
  
  // Merge newly scanned devices with existing devices
  Serial.println(F("[ParallelScan] Merging with existing devices..."));
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    // Add back existing devices that weren't found in this scan
    for (auto& existingDevice : existingDevices) {
      bool found = false;
      for (auto& newDevice : tasmotaDevices) {
        if (newDevice.ip == existingDevice.ip) {
          found = true;
          // Update with new info but keep the device
          break;
        }
      }
      
      if (!found) {
        // Device wasn't found in scan, but keep it in the list
        Serial.print(F("  Keeping unfound device: "));
        Serial.print(existingDevice.ip);
        Serial.print(F(" ("));
        Serial.print(existingDevice.friendlyName);
        Serial.println(F(")"));
        tasmotaDevices.push_back(existingDevice);
      }
    }
    
    Serial.print(F("[ParallelScan] Total devices after merge: "));
    Serial.println(tasmotaDevices.size());
    xSemaphoreGive(ipListMutex);
  }
  
  isScanning = false;
  Serial.print(F("[ParallelScan] Scan complete. Total devices: "));
  Serial.println(tasmotaDevices.size());
  Serial.print(F("[Memory] Free heap after scan: "));
  Serial.print(ESP.getFreeHeap());
  Serial.println(F(" bytes"));
  
  broadcastDevices();
  vTaskDelete(NULL);
}

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

String getPowerState(const String& ip) {
  esp_task_wdt_reset();
  HTTPClient http;
  http.setTimeout(2000);
  http.begin("http://" + ip + "/cm?cmnd=Power");
  int code = http.GET();
  
  if (code == 200) {
    String response = http.getString();
    http.end();
    
    Serial.print(F("  Power response from "));
    Serial.print(ip);
    Serial.print(F(": "));
    Serial.println(response);
    
    if (response.indexOf("\"ON\"") >= 0) return "ON";
    if (response.indexOf("\"OFF\"") >= 0) return "OFF";
    
    return response.substring(0, 50);
  } else {
    http.end();
  }
  return "UNKNOWN";
}

void togglePower(const String& ip) {
  Serial.print(F("Toggling power for: "));
  Serial.println(ip);
  
  esp_task_wdt_reset();
  HTTPClient http;
  http.setTimeout(2000);
  http.begin("http://" + ip + "/cm?cmnd=Power%20TOGGLE");
  int code = http.GET();
  String response = http.getString();
  http.end();
  
  Serial.print(F("  Toggle response ("));
  Serial.print(code);
  Serial.print(F("): "));
  Serial.println(response);
  delay(200);
}

void setPowerAll(bool state) {
  Serial.print(F("Setting all lights to: "));
  Serial.println(state ? F("ON") : F("OFF"));
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (auto& device : tasmotaDevices) {
      esp_task_wdt_reset();
      HTTPClient http;
      http.setTimeout(2000);
      String cmd = state ? "Power%20ON" : "Power%20OFF";
      http.begin("http://" + device.ip + "/cm?cmnd=" + cmd);
      http.GET();
      http.end();
      
      device.cachedState = "";
      device.lastStateUpdate = 0;
      
      delay(50);
    }
    xSemaphoreGive(ipListMutex);
  }
  delay(200);
  broadcastDevices();
}

void saveDevicesToFile() {
  Serial.println(F("Saving devices to /devices.json..."));
  
  File file = LittleFS.open("/devices.json", "w");
  if (!file) {
    Serial.println(F("Failed to open devices.json for writing"));
    return;
  }
  
  JsonDocument doc;
  JsonArray devicesArray = doc["devices"].to<JsonArray>();
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (auto& device : tasmotaDevices) {
      JsonObject deviceObj = devicesArray.add<JsonObject>();
      deviceObj["ip"] = device.ip;
      deviceObj["devicename"] = device.deviceName;
      deviceObj["friendlyname"] = device.friendlyName;
      deviceObj["module"] = device.module;
    }
    xSemaphoreGive(ipListMutex);
  }
  
  doc["total"] = tasmotaDevices.size();
  doc["timestamp"] = millis();
  
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to devices.json"));
  } else {
    Serial.print(F("Saved "));
    Serial.print(tasmotaDevices.size());
    Serial.println(F(" devices to devices.json"));
  }
  
  file.close();
}

void broadcastDevices() {
  broadcastDevicesInstant();
  xTaskCreatePinnedToCore(updateDeviceStatesTask, "UpdateStates", 8192, NULL, 1, NULL, 1);
  saveDevicesToFile();
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  
  if (type == WS_EVT_CONNECT) {
    Serial.print(F("WebSocket client #"));
    Serial.print(client->id());
    Serial.print(F(" connected from "));
    Serial.println(client->remoteIP().toString());
    
    if (xSemaphoreTake(clientListMutex, portMAX_DELAY)) {
      webClients.push_back(client->id());
      xSemaphoreGive(clientListMutex);
    }
    client->text("{\"type\":\"info\",\"msg\":\"Connected\"}");
    broadcastDevices();
  }
  else if (type == WS_EVT_DISCONNECT) {
    Serial.print(F("WebSocket client #"));
    Serial.print(client->id());
    Serial.println(F(" disconnected"));
    
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
      
      Serial.print(F("WebSocket message: "));
      Serial.println(msg);
      
      JsonDocument doc;
      if (deserializeJson(doc, msg)) return;
      
      String action = doc["action"] | "";
      
      if (action == "get_config") {
        JsonDocument configDoc;
        configDoc["type"] = "config";
        configDoc["subnet"] = subnetBase;
        configDoc["start"] = startOctet;
        configDoc["end"] = endOctet;
        configDoc["maxScans"] = maxParallelScans;
        configDoc["timeout"] = scanTimeoutMs;
        configDoc["authEnabled"] = auth_enabled;
        configDoc["authUser"] = auth_username;
        String configJson;
        serializeJson(configDoc, configJson);
        client->text(configJson);
      }
      else if (action == "save_config") {
        String newSubnet = doc["subnet"] | "";
        int newStart = doc["start"] | startOctet;
        int newEnd = doc["end"] | endOctet;
        int newMaxScans = doc["maxScans"] | maxParallelScans;
        int newTimeout = doc["timeout"] | scanTimeoutMs;
        bool newAuthEnabled = doc["authEnabled"] | auth_enabled;
        String newAuthUser = doc["authUser"] | auth_username;
        String newAuthPass = doc["authPass"] | "";
        
        if (newSubnet.length() > 0 && newStart > 0 && newEnd > 0 && newStart <= newEnd &&
            newMaxScans >= 1 && newMaxScans <= 20 && newTimeout >= 500 && newTimeout <= 5000) {
          subnetBase = newSubnet;
          if (!subnetBase.endsWith(".")) subnetBase += ".";
          startOctet = newStart;
          endOctet = newEnd;
          maxParallelScans = newMaxScans;
          scanTimeoutMs = newTimeout;
          saveConfig();
          
          // Save auth settings
          if (newAuthUser.length() > 0) {
            if (newAuthPass.length() > 0) {
              // New password provided
              saveAuthConfig(newAuthUser, newAuthPass, newAuthEnabled);
            } else {
              // Keep existing password
              saveAuthConfig(newAuthUser, auth_password, newAuthEnabled);
            }
          }
          
          Serial.println(F("Configuration updated:"));
          Serial.print(F("  Subnet: "));
          Serial.println(subnetBase);
          Serial.print(F("  Range: "));
          Serial.print(startOctet);
          Serial.print(F(" - "));
          Serial.println(endOctet);
          Serial.print(F("  Max Parallel Scans: "));
          Serial.println(maxParallelScans);
          Serial.print(F("  Scan Timeout: "));
          Serial.print(scanTimeoutMs);
          Serial.println(F("ms"));
          Serial.print(F("  Auth Enabled: "));
          Serial.println(auth_enabled ? F("Yes") : F("No"));
        }
      }
      else if (action == "download_files") {
        Serial.println(F("Starting file downloads..."));
        
        bool timer_ok = downloadFileToLittleFS(
          "https://ldijkman.github.io/Swipe-ESP32-Timer-Switch/burn/timer.html",
          "timer.html",
          client
        );
        
        bool ace_ok = downloadFileToLittleFS(
          "https://ldijkman.github.io/Swipe-ESP32-Timer-Switch/burn/ace.html",
          "ace.html",
          client
        );
        
        JsonDocument statusDoc;
        statusDoc["type"] = "download_complete";
        if (timer_ok && ace_ok) {
          statusDoc["success"] = true;
          statusDoc["message"] = "All files downloaded successfully!";
        } else if (timer_ok) {
          statusDoc["success"] = false;
          statusDoc["message"] = "timer.html downloaded, but ace.html failed";
        } else if (ace_ok) {
          statusDoc["success"] = false;
          statusDoc["message"] = "ace.html downloaded, but timer.html failed";
        } else {
          statusDoc["success"] = false;
          statusDoc["message"] = "Download failed for both files";
        }
        
        String statusJson;
        serializeJson(statusDoc, statusJson);
        client->text(statusJson);
      }
      else if (action == "scan") {
        Serial.println(F("Received scan request"));
        if (!isScanning) {
          JsonDocument startDoc;
          startDoc["type"] = "scan_start";
          String startJson;
          serializeJson(startDoc, startJson);
          broadcastToWebClients(startJson);
          
          Serial.println(F("Starting parallel scan task..."));
          xTaskCreatePinnedToCore(parallelScanTask, "ParallelScan", 12288, NULL, 1, &scanTaskHandle, 1);
        } else {
          client->text("{\"type\":\"info\",\"msg\":\"Scan already in progress\"}");
        }
      }
      else if (action == "toggle") {
        String ip = doc["ip"] | "";
        if (ip.length() > 0) {
          togglePower(ip);
          delay(100);
          
          String newState = getPowerState(ip);
          
          if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
            for (auto& device : tasmotaDevices) {
              if (device.ip == ip) {
                device.cachedState = newState;
                device.lastStateUpdate = millis();
                break;
              }
            }
            xSemaphoreGive(ipListMutex);
          }
          
          JsonDocument updateDoc;
          updateDoc["type"] = "device_update";
          updateDoc["ip"] = ip;
          updateDoc["state"] = newState;
          String updateJson;
          serializeJson(updateDoc, updateJson);
          broadcastToWebClients(updateJson);
          
          Serial.print(F("Updated device "));
          Serial.print(ip);
          Serial.print(F(" -> "));
          Serial.println(newState);
        }
      }
      else if (action == "rename") {
        String ip = doc["ip"] | "";
        String friendlyName = doc["friendlyname"] | "";
        String deviceName = doc["devicename"] | "";
        
        if (ip.length() > 0 && friendlyName.length() > 0 && deviceName.length() > 0) {
          Serial.print(F("Renaming device "));
          Serial.print(ip);
          Serial.print(F(": FriendlyName='"));
          Serial.print(friendlyName);
          Serial.print(F("', DeviceName='"));
          Serial.print(deviceName);
          Serial.println(F("'"));
          
          esp_task_wdt_reset();
          HTTPClient http;
          http.setTimeout(2000);
          
          // Set FriendlyName
          String cmd1 = "FriendlyName1%20" + urlEncode(friendlyName);
          http.begin("http://" + ip + "/cm?cmnd=" + cmd1);
          int code1 = http.GET();
          String response1 = http.getString();
          http.end();
          Serial.print(F("  FriendlyName response ("));
          Serial.print(code1);
          Serial.print(F("): "));
          Serial.println(response1);
          
          delay(200);
          
          // Set DeviceName
          String cmd2 = "DeviceName%20" + urlEncode(deviceName);
          http.begin("http://" + ip + "/cm?cmnd=" + cmd2);
          int code2 = http.GET();
          String response2 = http.getString();
          http.end();
          Serial.print(F("  DeviceName response ("));
          Serial.print(code2);
          Serial.print(F("): "));
          Serial.println(response2);
          
          // Update cached device info
          if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
            for (auto& device : tasmotaDevices) {
              if (device.ip == ip) {
                device.friendlyName = friendlyName;
                device.deviceName = deviceName;
                break;
              }
            }
            xSemaphoreGive(ipListMutex);
          }
          
          delay(500);
          broadcastDevices();
        }
      }
      else if (action == "remove") {
        String ip = doc["ip"] | "";
        if (ip.length() > 0) {
          removeDevice(ip);
          broadcastDevices();
        }
      }
      else if (action == "refresh") {
        broadcastDevices();
      }
      else if (action == "all_on") setPowerAll(true);
      else if (action == "all_off") setPowerAll(false);
    }
  }
}

// Improv WiFi callbacks
void onImprovWiFiErrorCb(ImprovTypes::Error err) {
  Serial.print(F("Improv Error: "));
  Serial.println(err);
}

void onImprovWiFiConnectedCb(const char* ssid, const char* password) {
  Serial.print(F("Improv WiFi Connected: "));
  Serial.println(ssid);
  
  // Save credentials
  saveWiFiConfig(String(ssid), String(password));
  
  // Send success URL
  String url = "http://" + WiFi.localIP().toString();
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "Tasmota Scanner", "1.0.0", "Tasmota Network Scanner", url.c_str());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println(F("\n\n=== Tasmota Network Scanner ==="));
  
  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS Mount Failed"));
  } else {
    Serial.println(F("LittleFS Mounted Successfully"));
  }
  
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);
  
  Serial.println(F("Watchdog timer configured (30s timeout)"));
  
  loadConfig();
  
  // Initialize Improv WiFi
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "Tasmota Scanner", "1.0.0", "Tasmota Network Scanner");
  improvSerial.onImprovError(onImprovWiFiErrorCb);
  improvSerial.onImprovConnected(onImprovWiFiConnectedCb);
  
  ipListMutex = xSemaphoreCreateMutex();
  clientListMutex = xSemaphoreCreateMutex();
  
  // Load previously discovered devices from file
  loadDevicesFromFile();
  
  WiFi.mode(WIFI_STA);
  
  // Use saved WiFi credentials if available, otherwise use defaults
  const char* connect_ssid;
  const char* connect_password;
  
  if (wifi_ssid.length() > 0) {
    Serial.println(F("Using saved WiFi credentials"));
    connect_ssid = wifi_ssid.c_str();
    connect_password = wifi_password.c_str();
  } else {
    Serial.println(F("Using default WiFi credentials"));
    connect_ssid = default_ssid;
    connect_password = default_password;
  }
  
  WiFi.begin(connect_ssid, connect_password);
  
  Serial.print(F("Connecting to WiFi: "));
  Serial.println(connect_ssid);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
    esp_task_wdt_reset();
    if (millis() - startTime > 15000) {
      Serial.println(F("\nWiFi connection timeout!"));
      break;
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nWiFi connected!"));
    Serial.print(F("IP address: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("Signal strength: "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
  }
  
  local_IP = WiFi.localIP();
  
  Serial.println(F("\nNetwork Configuration:"));
  Serial.print(F("  Subnet base: "));
  Serial.println(subnetBase);
  Serial.print(F("  Scan range: "));
  Serial.print(subnetBase);
  Serial.print(startOctet);
  Serial.print(F(" to "));
  Serial.print(subnetBase);
  Serial.println(endOctet);
  Serial.print(F("  Total IPs to scan: "));
  Serial.println(endOctet - startOctet + 1);
  Serial.print(F("  Parallel scans: "));
  Serial.println(maxParallelScans);
  Serial.print(F("  Scan timeout: "));
  Serial.print(scanTimeoutMs);
  Serial.println(F("ms"));
  
  server.on("/", HTTP_GET, handleRoot);
  
  // Serve timer.html from LittleFS
  server.on("/timer.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (auth_enabled) {
      if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
        return request->requestAuthentication();
      }
    }
    if (LittleFS.exists("/timer.html")) {
      request->send(LittleFS, "/timer.html", "text/html");
    } else {
      request->send(404, "text/plain", "File not found. Please download files from Settings.");
    }
  });
  
  // Serve ace.html from LittleFS
  server.on("/ace.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (auth_enabled) {
      if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
        return request->requestAuthentication();
      }
    }
    if (LittleFS.exists("/ace.html")) {
      request->send(LittleFS, "/ace.html", "text/html");
    } else {
      request->send(404, "text/plain", "File not found. Please download files from Settings.");
    }
  });
  
  // List directory endpoint for ace.html
  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (auth_enabled) {
      if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
        return request->requestAuthentication();
      }
    }
    
    String path = "/";
    if (request->hasParam("dir")) {
      path = request->getParam("dir")->value();
    }
    
    File root = LittleFS.open(path);
    if (!root || !root.isDirectory()) {
      request->send(404, "application/json", "[]");
      return;
    }
    
    String output = "[";
    File file = root.openNextFile();
    bool first = true;
    
    while (file) {
      if (!first) output += ",";
      first = false;
      
      output += "{";
      output += "\"type\":\"" + String(file.isDirectory() ? "dir" : "file") + "\",";
      output += "\"name\":\"" + String(file.name()) + "\"";
      if (!file.isDirectory()) {
        output += ",\"size\":" + String(file.size());
      }
      output += "}";
      
      file = root.openNextFile();
    }
    
    output += "]";
    request->send(200, "application/json", output);
  });
  
  // Status endpoint for ace.html
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (auth_enabled) {
      if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
        return request->requestAuthentication();
      }
    }
    
    String output = "{";
    output += "\"type\":\"LittleFS\",";
    output += "\"isOk\":true,";
    output += "\"totalBytes\":" + String(LittleFS.totalBytes()) + ",";
    output += "\"usedBytes\":" + String(LittleFS.usedBytes());
    output += "}";
    
    request->send(200, "application/json", output);
  });
  
  // Edit endpoint - GET (read file)
  server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (auth_enabled) {
      if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
        return request->requestAuthentication();
      }
    }
    
    if (!request->hasParam("file")) {
      request->send(400, "text/plain", "Missing file parameter");
      return;
    }
    
    String path = request->getParam("file")->value();
    if (LittleFS.exists(path)) {
      request->send(LittleFS, path, "text/plain");
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });
  
  // Edit endpoint - POST (save file)
  server.on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (auth_enabled) {
      if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
        return request->requestAuthentication();
      }
    }
    request->send(200);
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File uploadFile;
    
    if (index == 0) {
      Serial.printf("Upload Start: %s\n", filename.c_str());
      if (!filename.startsWith("/")) filename = "/" + filename;
      uploadFile = LittleFS.open(filename, "w");
    }
    
    if (uploadFile) {
      uploadFile.write(data, len);
    }
    
    if (final) {
      if (uploadFile) {
        uploadFile.close();
      }
      Serial.printf("Upload Complete: %s (%u bytes)\n", filename.c_str(), index + len);
    }
  });
  
  // Edit endpoint - PUT (create file/rename)
  server.on("/edit", HTTP_PUT, [](AsyncWebServerRequest *request) {
    if (auth_enabled) {
      if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
        return request->requestAuthentication();
      }
    }
    
    if (!request->hasParam("path", true)) {
      request->send(400, "text/plain", "Missing path parameter");
      return;
    }
    
    String path = request->getParam("path", true)->value();
    
    // Check if this is a rename operation
    if (request->hasParam("src", true)) {
      String src = request->getParam("src", true)->value();
      if (LittleFS.exists(src)) {
        if (LittleFS.rename(src, path)) {
          request->send(200, "text/plain", "File renamed");
        } else {
          request->send(500, "text/plain", "Rename failed");
        }
      } else {
        request->send(404, "text/plain", "Source file not found");
      }
    } else {
      // Create new file
      File file = LittleFS.open(path, "w");
      if (file) {
        file.close();
        request->send(200, "text/plain", "File created");
      } else {
        request->send(500, "text/plain", "Failed to create file");
      }
    }
  });
  
  // Edit endpoint - DELETE (delete file)
  server.on("/edit", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    if (auth_enabled) {
      if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
        return request->requestAuthentication();
      }
    }
    
    if (!request->hasParam("path", true)) {
      request->send(400, "text/plain", "Missing path parameter");
      return;
    }
    
    String path = request->getParam("path", true)->value();
    
    if (LittleFS.exists(path)) {
      if (LittleFS.remove(path)) {
        request->send(200, "text/plain", "File deleted");
      } else {
        request->send(500, "text/plain", "Delete failed");
      }
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });
  
  // Serve any file from LittleFS
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (auth_enabled) {
      if (!request->authenticate(auth_username.c_str(), auth_password.c_str())) {
        return request->requestAuthentication();
      }
    }
    
    String path = request->url();
    if (LittleFS.exists(path)) {
      request->send(LittleFS, path);
    } else {
      request->send(404, "text/plain", "Not found");
    }
  });
  
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
  
  Serial.println(F("HTTP server started"));
  Serial.print(F("Open browser: http://"));
  Serial.println(WiFi.localIP().toString());
  Serial.println(F("======================================\n"));
  
  esp_task_wdt_reset();
}

void loop() {
  // Handle Improv WiFi serial communication
  improvSerial.handleSerial();
  
  esp_task_wdt_reset();
  ws.cleanupClients();
  delay(10);
}
