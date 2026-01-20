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
#include <Preferences.h>

// WiFi credentials
const char* ssid = "wifi";
const char* password = "pass";

// Network configuration
IPAddress local_IP;
String subnetBase = "10.10.100.";
int startOctet   = 100;
int endOctet     = 140;

#define MAX_PARALLEL_SCANS 12
#define SCAN_TIMEOUT_MS 1000
#define STATE_CACHE_DURATION 5000  // Cache states for 5 seconds

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;

struct DeviceInfo {
  String ip;
  String deviceName;
  String friendlyName;
  String lastKnownState;      // ADDED: Cache the state
  unsigned long lastStateUpdate;  // ADDED: Timestamp of last update
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
void refreshAllStatesTask(void* parameter);

// Serve HTML page
void handleRoot(AsyncWebServerRequest *request) {
  Serial.println("Serving root page...");
  
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  
  if (!response) {
    Serial.println("ERROR: Failed to create response stream");
    request->send(500, "text/plain", "Failed to create response");
    return;
  }
  
  Serial.printf("Free heap before HTML: %d bytes\n", ESP.getFreeHeap());
  
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
  response->print(".device{padding:8px;background:#666;border-radius:6px}");
  response->print(".device-header{width:100%;padding:6px;border-radius:4px 4px 0 0;margin:-8px -8px 6px -8px;font-weight:bold;font-size:14px;box-sizing:border-box}");
  response->print(".on .device-header{background:#4CAF50}");
  response->print(".off .device-header{background:#f44336}");
  response->print(".device-name{font-size:13px;margin:2px 0;color:#fff}");
  response->print(".device-ip{font-size:11px;margin:2px 0;color:#4da6ff;cursor:pointer;text-decoration:underline}");
  response->print(".device-ip:hover{color:#80c1ff}");
  response->print("button{padding:6px 16px;font-size:14px;margin:4px;border:none;border-radius:4px;cursor:pointer}");
  response->print(".scan{background:#2196F3;color:white;padding:8px 20px;font-size:16px}");
  response->print(".scan:disabled{background:#666;cursor:not-allowed}");
  response->print(".all-on{background:#4CAF50;color:white;padding:8px 20px;font-size:16px}");
  response->print(".all-off{background:#f44336;color:white;padding:8px 20px;font-size:16px}");
  response->print(".config-btn{background:#FF9800;color:white;padding:8px 20px;font-size:16px}");
  response->print(".toggle{width:100%;background:#555;color:white;margin-top:4px}");
  response->print(".toggle:hover{background:#777}");
  response->print(".progress-section{margin:15px;padding:10px;background:#333;border-radius:5px}");
  response->print(".progress-bar{width:100%;height:18px;background:#444;border-radius:10px;overflow:hidden}");
  response->print(".progress-fill{height:100%;background:#0f8;width:0%;transition:width 0.3s}");
  response->print(".progress-section p{margin:8px 0;font-size:14px}");
  response->print(".modal{display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;background:rgba(0,0,0,0.8)}");
  response->print(".modal-content{background:#222;margin:5% auto;padding:20px;border:1px solid #888;width:90%;max-width:600px;border-radius:10px;color:#eee}");
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
  response->print("</style></head><body>");
  
  response->print("<h1>Tasmota Lights Controller</h1>");
  response->print("<div class='control-panel'>");
  response->print("<button class='scan' id='scanBtn'>Scan Network</button>");
  response->print("<button class='all-on' id='allOnBtn'>All ON</button>");
  response->print("<button class='all-off' id='allOffBtn'>All OFF</button>");
  response->print("<button class='config-btn' id='configBtn'>Settings</button>");
  response->print("</div>");
  
  response->print("<div id='progress' class='progress-section' style='display:none'>");
  response->print("<p>Scanning: <span id='currentIP'>-</span></p>");
  response->print("<div class='progress-bar'><div class='progress-fill' id='progressFill'></div></div>");
  response->print("<p id='foundCount'>Found: 0 devices</p>");
  response->print("<p id='statusMsg'>Status: Idle</p>");
  response->print("</div>");
  
  response->print("<div class='devices-grid' id='devices'></div>");
  
  response->print("<div class='footer'>");
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
  response->print("<button class='save-btn' id='saveBtn'>Save and Apply</button>");
  response->print("</div></div>");
  
  // JavaScript
  response->print("<script>");
  response->print("var ws,wsConnected=false;");
  response->print("function vibrate(d){d=d||100;if(navigator.vibrate)navigator.vibrate(d)}");
  response->print("function connectWebSocket(){console.log('Connecting to WebSocket...');");
  response->print("ws=new WebSocket('ws://'+location.hostname+'/ws');");
  response->print("console.log('WebSocket URL: ws://'+location.hostname+'/ws');");
  response->print("ws.onopen=function(){console.log('WebSocket Connected Successfully');wsConnected=true;");
  response->print("document.getElementById('scanBtn').disabled=false;");
  response->print("ws.send(JSON.stringify({action:'get_config'}))};");
  
  response->print("ws.onmessage=function(e){console.log('Received:',e.data);var d=JSON.parse(e.data);");
  response->print("if(d.type==='config'){document.getElementById('subnetBase').value=d.subnet||'';");
  response->print("document.getElementById('startOctet').value=d.start||2;");
  response->print("document.getElementById('endOctet').value=d.end||254;updateRangePreview()}");
  response->print("else if(d.type==='devices'){var html='';for(var i=0;i<d.list.length;i++){");
  response->print("var dev=d.list[i];var sc=dev.state.indexOf('ON')>=0?'on':'off';");
  response->print("var dn=dev.devicename||'Unknown';var fn=dev.friendlyname||dn;");
  response->print("html+=\"<div class='device \"+sc+\"' id='device-\"+dev.ip.replace(/\\./g,'-')+\"'>\";");
  response->print("html+=\"<div class='device-header'>\"+fn+\"</div>\";");
  response->print("html+=\"<div class='device-name'>\"+dn+\"</div>\";");
  response->print("html+=\"<div class='device-ip' data-ip='\"+dev.ip+\"'>\"+dev.ip+\"</div>\";");
  response->print("html+=\"<button class='toggle' data-ip='\"+dev.ip+\"'>Toggle</button></div>\"}");
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
  response->print("else if(d.type==='info')console.log('Info:',d.msg)};");
  
  response->print("ws.onerror=function(e){console.error('WebSocket Error:',e);wsConnected=false;");
  response->print("console.error('WebSocket failed to connect. Check that ESP32 is running and accessible.')};");
  response->print("ws.onclose=function(e){console.log('WebSocket closed. Code:',e.code,'Reason:',e.reason);");
  response->print("wsConnected=false;setTimeout(connectWebSocket,2000)}}");
  
  response->print("function attachDeviceHandlers(){var ips=document.querySelectorAll('.device-ip');");
  response->print("for(var i=0;i<ips.length;i++)ips[i].addEventListener('click',function(){");
  response->print("vibrate(100);window.open('http://'+this.getAttribute('data-ip'),'_blank')});");
  response->print("var btns=document.querySelectorAll('.toggle');for(var i=0;i<btns.length;i++)");
  response->print("btns[i].addEventListener('click',function(){vibrate(100);");
  response->print("var ip=this.getAttribute('data-ip');if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'toggle',ip:ip}))})}");
  
  response->print("function scan(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'scan'}));document.getElementById('scanBtn').disabled=true}");
  response->print("function allOn(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'all_on'}))}");
  response->print("function allOff(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("ws.send(JSON.stringify({action:'all_off'}))}");
  response->print("function openConfig(){vibrate(100);document.getElementById('configModal').style.display='block';updateRangePreview()}");
  response->print("function closeConfig(){vibrate(100);document.getElementById('configModal').style.display='none'}");
  response->print("function setPreset(s,st,e){vibrate(100);document.getElementById('subnetBase').value=s;");
  response->print("document.getElementById('startOctet').value=st;document.getElementById('endOctet').value=e;updateRangePreview()}");
  response->print("function updateRangePreview(){var s=document.getElementById('subnetBase').value;");
  response->print("var st=document.getElementById('startOctet').value;var e=document.getElementById('endOctet').value;");
  response->print("document.getElementById('rangePreview').textContent=s+'.'+st+' to '+s+'.'+e}");
  response->print("function saveConfig(){vibrate(100);if(!wsConnected){alert('Not connected!');return}");
  response->print("var s=document.getElementById('subnetBase').value;");
  response->print("var st=parseInt(document.getElementById('startOctet').value);");
  response->print("var e=parseInt(document.getElementById('endOctet').value);");
  response->print("if(!s||isNaN(st)||isNaN(e)){alert('Invalid input');return}");
  response->print("if(st<1||st>254||e<1||e>254||st>e){alert('Invalid range');return}");
  response->print("ws.send(JSON.stringify({action:'save_config',subnet:s,start:st,end:e}));");
  response->print("closeConfig();alert('Configuration saved!')}");
  
  response->print("connectWebSocket();");
  response->print("document.getElementById('scanBtn').addEventListener('click',scan);");
  response->print("document.getElementById('allOnBtn').addEventListener('click',allOn);");
  response->print("document.getElementById('allOffBtn').addEventListener('click',allOff);");
  response->print("document.getElementById('configBtn').addEventListener('click',openConfig);");
  response->print("document.getElementById('closeBtn').addEventListener('click',closeConfig);");
  response->print("document.getElementById('saveBtn').addEventListener('click',saveConfig);");
  
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
  response->print("window.onclick=function(e){var m=document.getElementById('configModal');");
  response->print("if(e.target==m)closeConfig()}");
  response->print("</script></body></html>");
  
  Serial.printf("Free heap before send: %d bytes\n", ESP.getFreeHeap());
  request->send(response);
  Serial.println("Root page sent successfully");
  Serial.printf("Free heap after send: %d bytes\n", ESP.getFreeHeap());
}

// Configuration Functions
void loadConfig() {
  preferences.begin("tasmota-scan", false);
  String savedSubnet = preferences.getString("subnet", "");
  if (savedSubnet.length() > 0) subnetBase = savedSubnet;
  startOctet = preferences.getInt("start", 100);
  endOctet = preferences.getInt("end", 140);
  preferences.end();
}

void saveConfig() {
  preferences.begin("tasmota-scan", false);
  preferences.putString("subnet", subnetBase);
  preferences.putInt("start", startOctet);
  preferences.putInt("end", endOctet);
  preferences.end();
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

bool checkTasmota(const String& ip) {
  esp_task_wdt_reset();
  
  Serial.printf("Checking IP: %s\n", ip.c_str());
  
  HTTPClient http;
  http.setTimeout(SCAN_TIMEOUT_MS);
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
      Serial.printf("  ✓ Found Tasmota at: %s\n", ip.c_str());
      Serial.printf("    Response: %s\n", response.c_str());
    } else {
      Serial.printf("  ✗ Not Tasmota at: %s (response: %s)\n", ip.c_str(), response.substring(0, 50).c_str());
    }
    
    return isTasmota;
  }
  http.end();
  
  if (httpCode > 0) {
    Serial.printf("  HTTP error %d from %s\n", httpCode, ip.c_str());
  } else {
    Serial.printf("  Connection failed to %s\n", ip.c_str());
  }
  
  return false;
}

void scanRangeTask(void* parameter) {
  int* range = (int*)parameter;
  int start = range[0];
  int end = range[1];
  
  Serial.printf("[Task] Scanning range: %d to %d\n", start, end);
  
  for (int i = start; i <= end; i++) {
    esp_task_wdt_reset();
    String ip = subnetBase + String(i);
    if (checkTasmota(ip)) {
      String deviceName = getDeviceName(ip);
      String friendlyName = getFriendlyName(ip);
      if (friendlyName.length() == 0) friendlyName = deviceName;
      
      // MODIFIED: Get initial state during scan
      String initialState = "UNKNOWN";
      HTTPClient http;
      http.setTimeout(1000);
      http.begin("http://" + ip + "/cm?cmnd=Power");
      if (http.GET() == 200) {
        String response = http.getString();
        if (response.indexOf("\"ON\"") >= 0) initialState = "ON";
        else if (response.indexOf("\"OFF\"") >= 0) initialState = "OFF";
      }
      http.end();
      
      Serial.printf("  Device: %s, Friendly: %s, State: %s\n", 
                    deviceName.c_str(), friendlyName.c_str(), initialState.c_str());
      
      if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
        DeviceInfo device;
        device.ip = ip;
        device.deviceName = deviceName;
        device.friendlyName = friendlyName;
        device.lastKnownState = initialState;  // ADDED
        device.lastStateUpdate = millis();      // ADDED
        tasmotaDevices.push_back(device);
        xSemaphoreGive(ipListMutex);
      }
    }
    totalScannedIPs++;
    delay(10);
  }
  delete[] range;
  Serial.printf("[Task] Range scan complete\n");
  vTaskSuspend(NULL);
}

void parallelScanTask(void* parameter) {
  Serial.println("[ParallelScan] Starting parallel scan...");
  Serial.printf("[Memory] Free heap: %d bytes\n", ESP.getFreeHeap());
  
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
    esp_task_wdt_reset();
  }
  
  unsigned long lastUpdate = 0;
  while (totalScannedIPs < totalIPs) {
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
      broadcastToWebClients(json);
      lastUpdate = millis();
    }
    
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
  
  Serial.println("[ParallelScan] Cleaning up tasks...");
  for (int i = 0; i < MAX_PARALLEL_SCANS; i++) {
    if (taskHandles[i] != NULL) vTaskDelete(taskHandles[i]);
  }
  
  delay(100);
  isScanning = false;
  Serial.printf("[ParallelScan] Scan complete. Found %d devices\n", tasmotaDevices.size());
  Serial.printf("[Memory] Free heap after scan: %d bytes\n", ESP.getFreeHeap());
  
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

// MODIFIED: Smart state fetching with cache
String getPowerState(const String& ip, bool forceRefresh = false) {
  esp_task_wdt_reset();
  
  // Check cache first (unless force refresh)
  if (!forceRefresh && xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (auto& device : tasmotaDevices) {
      if (device.ip == ip) {
        unsigned long age = millis() - device.lastStateUpdate;
        if (age < STATE_CACHE_DURATION && device.lastKnownState != "UNKNOWN") {
          String cachedState = device.lastKnownState;
          xSemaphoreGive(ipListMutex);
          Serial.printf("  Using cached state for %s: %s (age: %lums)\n", 
                       ip.c_str(), cachedState.c_str(), age);
          return cachedState;
        }
        break;
      }
    }
    xSemaphoreGive(ipListMutex);
  }
  
  // Fetch fresh state
  HTTPClient http;
  http.setTimeout(2000);
  http.begin("http://" + ip + "/cm?cmnd=Power");
  int code = http.GET();
  
  String state = "UNKNOWN";
  if (code == 200) {
    String response = http.getString();
    http.end();
    
    Serial.printf("  Power response from %s: %s\n", ip.c_str(), response.c_str());
    
    if (response.indexOf("\"ON\"") >= 0) state = "ON";
    else if (response.indexOf("\"OFF\"") >= 0) state = "OFF";
    
    // Update cache
    if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
      for (auto& device : tasmotaDevices) {
        if (device.ip == ip) {
          device.lastKnownState = state;
          device.lastStateUpdate = millis();
          break;
        }
      }
      xSemaphoreGive(ipListMutex);
    }
  } else {
    http.end();
  }
  
  return state;
}

void togglePower(const String& ip) {
  Serial.printf("Toggling power for: %s\n", ip.c_str());
  
  esp_task_wdt_reset();
  HTTPClient http;
  http.setTimeout(2000);
  http.begin("http://" + ip + "/cm?cmnd=Power%20TOGGLE");
  int code = http.GET();
  String response = http.getString();
  http.end();
  
  Serial.printf("  Toggle response (%d): %s\n", code, response.c_str());
  
  // ADDED: Update cache immediately based on toggle response
  if (code == 200) {
    String newState = "UNKNOWN";
    if (response.indexOf("\"ON\"") >= 0) newState = "ON";
    else if (response.indexOf("\"OFF\"") >= 0) newState = "OFF";
    
    if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
      for (auto& device : tasmotaDevices) {
        if (device.ip == ip) {
          device.lastKnownState = newState;
          device.lastStateUpdate = millis();
          break;
        }
      }
      xSemaphoreGive(ipListMutex);
    }
  }
  
  delay(200);
}

void setPowerAll(bool state) {
  Serial.printf("Setting all lights to: %s\n", state ? "ON" : "OFF");
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (auto& device : tasmotaDevices) {
      esp_task_wdt_reset();
      HTTPClient http;
      http.setTimeout(2000);
      String cmd = state ? "Power%20ON" : "Power%20OFF";
      http.begin("http://" + device.ip + "/cm?cmnd=" + cmd);
      http.GET();
      http.end();
      
      // ADDED: Update cache
      device.lastKnownState = state ? "ON" : "OFF";
      device.lastStateUpdate = millis();
      
      delay(50);
    }
    xSemaphoreGive(ipListMutex);
  }
  delay(200);
  broadcastDevices();
}

// ADDED: Background task to refresh all device states
void refreshAllStatesTask(void* parameter) {
  Serial.println("[RefreshStates] Starting background status refresh...");
  
  esp_task_wdt_reset();
  delay(500); // Small delay to let initial cached broadcast complete
  
  std::vector<String> deviceIPs;
  
  // Get list of device IPs
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (const auto& device : tasmotaDevices) {
      deviceIPs.push_back(device.ip);
    }
    xSemaphoreGive(ipListMutex);
  }
  
  // Refresh each device state
  bool anyChanged = false;
  for (const String& ip : deviceIPs) {
    esp_task_wdt_reset();
    
    String oldState = "";
    if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
      for (const auto& device : tasmotaDevices) {
        if (device.ip == ip) {
          oldState = device.lastKnownState;
          break;
        }
      }
      xSemaphoreGive(ipListMutex);
    }
    
    // Force refresh from device
    String newState = getPowerState(ip, true);
    
    // Check if state changed
    if (newState != oldState && newState != "UNKNOWN") {
      anyChanged = true;
      Serial.printf("[RefreshStates] State changed for %s: %s -> %s\n", 
                   ip.c_str(), oldState.c_str(), newState.c_str());
      
      // Broadcast individual update
      JsonDocument updateDoc;
      updateDoc["type"] = "device_update";
      updateDoc["ip"] = ip;
      updateDoc["state"] = newState;
      String updateJson;
      serializeJson(updateDoc, updateJson);
      broadcastToWebClients(updateJson);
    }
    
    delay(100); // Small delay between requests to avoid overwhelming network
  }
  
  Serial.printf("[RefreshStates] Background refresh complete. Changed: %s\n", 
               anyChanged ? "YES" : "NO");
  
  vTaskDelete(NULL);
}

// MODIFIED: Fast broadcast using cached states
void broadcastDevices() {
  Serial.println("Broadcasting devices to WebSocket clients (using cache)");
  
  esp_task_wdt_reset();
  JsonDocument doc;
  JsonArray list = doc["list"].to<JsonArray>();
  
  if (xSemaphoreTake(ipListMutex, portMAX_DELAY)) {
    for (auto& device : tasmotaDevices) {
      JsonObject dev = list.add<JsonObject>();
      dev["ip"] = device.ip;
      dev["devicename"] = device.deviceName;
      dev["friendlyname"] = device.friendlyName;
      
      // Use cached state (no HTTP request!)
      String state = device.lastKnownState;
      dev["state"] = state;
      
      Serial.printf("  Device: %s (%s) -> %s (cached)\n", 
                   device.friendlyName.c_str(), device.ip.c_str(), state.c_str());
      esp_task_wdt_reset();
    }
    xSemaphoreGive(ipListMutex);
  }
  
  doc["type"] = "devices";
  String json;
  serializeJson(doc, json);
  broadcastToWebClients(json);
  
  Serial.printf("Broadcast complete. Total devices: %d\n", tasmotaDevices.size());
  esp_task_wdt_reset();
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  
  Serial.printf("[WS Event] Type: %d from client #%u\n", type, client ? client->id() : 0);
  
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", 
                  client->id(), client->remoteIP().toString().c_str());
    
    if (xSemaphoreTake(clientListMutex, portMAX_DELAY)) {
      webClients.push_back(client->id());
      xSemaphoreGive(clientListMutex);
    }
    client->text("{\"type\":\"info\",\"msg\":\"Connected\"}");
    
    // MODIFIED: Instant response using cache
    broadcastDevices();
    
    // ADDED: Trigger background status refresh
    if (!isScanning) {
      xTaskCreatePinnedToCore(refreshAllStatesTask, "RefreshStates", 8192, NULL, 1, NULL, 1);
    }
  }
  else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    
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
      if (deserializeJson(doc, msg)) return;
      
      String action = doc["action"] | "";
      
      if (action == "get_config") {
        JsonDocument configDoc;
        configDoc["type"] = "config";
        configDoc["subnet"] = subnetBase;
        configDoc["start"] = startOctet;
        configDoc["end"] = endOctet;
        String configJson;
        serializeJson(configDoc, configJson);
        client->text(configJson);
      }
      else if (action == "save_config") {
        String newSubnet = doc["subnet"] | "";
        int newStart = doc["start"] | startOctet;
        int newEnd = doc["end"] | endOctet;
        
        if (newSubnet.length() > 0 && newStart > 0 && newEnd > 0 && newStart <= newEnd) {
          subnetBase = newSubnet;
          if (!subnetBase.endsWith(".")) subnetBase += ".";
          startOctet = newStart;
          endOctet = newEnd;
          saveConfig();
          
          Serial.println("Configuration updated:");
          Serial.printf("  Subnet: %s\n", subnetBase.c_str());
          Serial.printf("  Range: %d - %d\n", startOctet, endOctet);
        }
      }
      else if (action == "scan") {
        Serial.println("Received scan request");
        if (!isScanning) {
          JsonDocument startDoc;
          startDoc["type"] = "scan_start";
          String startJson;
          serializeJson(startDoc, startJson);
          broadcastToWebClients(startJson);
          
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
          delay(100);
          
          // MODIFIED: Force refresh for this specific device
          String newState = getPowerState(ip, true);
          
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
      else if (action == "all_on") setPowerAll(true);
      else if (action == "all_off") setPowerAll(false);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== Tasmota Network Scanner (FAST RELOAD) ===");
  
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);
  
  Serial.println("Watchdog timer configured (30s timeout)");
  
  loadConfig();
  
  ipListMutex = xSemaphoreCreateMutex();
  clientListMutex = xSemaphoreCreateMutex();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.printf("Connecting to WiFi: %s", ssid);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    esp_task_wdt_reset();
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
  
  Serial.printf("\nNetwork Configuration:\n");
  Serial.printf("  Subnet base: %s\n", subnetBase.c_str());
  Serial.printf("  Scan range: %s%d to %s%d\n", subnetBase.c_str(), startOctet, subnetBase.c_str(), endOctet);
  Serial.printf("  Total IPs to scan: %d\n", endOctet - startOctet + 1);
  Serial.printf("  Parallel scans: %d\n", MAX_PARALLEL_SCANS);
  Serial.printf("  State cache duration: %dms\n", STATE_CACHE_DURATION);
  
  // IMPORTANT: Add WebSocket handler FIRST, before other routes
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  Serial.println("WebSocket handler registered at /ws");
  
  // Then add HTTP routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(204); // No content
  });
  
  server.begin();
  
  Serial.println("HTTP server started");
  Serial.printf("WebSocket enabled: %s\n", ws.enabled() ? "YES" : "NO");
  Serial.printf("WebSocket path: %s\n", ws.url());
  Serial.println("Open browser: http://" + WiFi.localIP().toString());
  Serial.println("======================================\n");
  
  esp_task_wdt_reset();
}

void loop() {
  esp_task_wdt_reset();
  ws.cleanupClients();
  
  static unsigned long lastLog = 0;
  if (millis() - lastLog > 10000) { // Every 10 seconds
    Serial.printf("[Loop] WS clients: %d, Free heap: %d\n", ws.count(), ESP.getFreeHeap());
    lastLog = millis();
  }
  
  delay(10);
}
