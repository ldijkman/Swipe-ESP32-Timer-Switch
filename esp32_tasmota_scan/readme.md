



---


burn flash configure esp32 from browser tasmota scanner controller webpage server

Swipe-ESP32-Timer-Switch/docs/burn

https://ldijkman.github.io/Swipe-ESP32-Timer-Switch/burn/flash.html

https://www.youtube.com/watch?v=YH7AbhQ37dY

<div align="center">
  <a href="https://www.youtube.com/watch?v=YH7AbhQ37dY">
    <img src="https://img.youtube.com/vi/YH7AbhQ37dY/0.jpg" 
         alt="ESP32 Tasmota scanner controller install and configure from browser" 
         style="width:100%; max-width:640px;">
    <br><br>
    <strong>Watch the full demo (click the image)</strong>
  </a>
</div>



https://ldijkman.github.io/Swipe-ESP32-Timer-Switch/burn/flash.html

---








an esp32 scans for tasmota devices http url check http://   ip   /cm?cmnd=power)

scans network for tasmota http devices and show on webpage

toggle each tasmota device by click on status

switch all tasmota on / off

some quick claude ai generated code

works a bit

---

easy forward 1 esp32 on internet

to controll all tasmotas from a webpage

low power use esp32 webserver

maybe use duckdns no-ip?

esp32 could update ip?

---

by serving no cors webpage from an esp32

no problem with CORS 

getting status from each tasmota device

fetching status on webpage from htpp tasmota gives CORS problems

no status given to webpage

this way i can get status 

an middle man esp32 server

---

TIPs!

Tasmota Remota think a nice app well priced for android

Tasmota Control app for android (maybe apple)
Manfred, Carsten und Heike Grings GbR

HomeSwitch - Tasmota Control android app
Jooova

i think above all work nice from home network

but if from internet you have to forward all devices expose all to web

---




<img width="1024" height="768" alt="Screenshot from 2026-01-11 09-09-23" src="https://github.com/user-attachments/assets/5194087d-fe6f-4219-8499-da7f4516c590" />

<img width="1024" height="768" alt="tasmota esp32 parallel scan" src="https://github.com/user-attachments/assets/da817260-6219-4360-9d44-d086fb686f51" />

<img width="1024" height="768" alt="tasmota_parallel_scan_esp32" src="https://github.com/user-attachments/assets/1a919463-52df-4da3-a86a-b721174fb068" />


tasmotaremota

DeepSeek ESP32 Tasmota Scanner forward 1 controll all

tasmota iotorero pg01 smart plug 

https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/blob/main/esp32_tasmota_scan/DeepSeek_Tasmota.ino

an ESP32 scans for tasmota devices on local wifi network 

<img width="30%" height="30%"  src="https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/blob/main/esp32_tasmota_scan/Screenshot_20260120-055535_Chrome.jpg">

common used wifi router presettings and scan range settings

https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/blob/main/esp32_tasmota_scan/DeepSeek_Tasmota.ino

<img width="30%" height="30%"   src="https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/blob/main/esp32_tasmota_scan/Screenshot_20260120-055653_Chrome.jpg">

tasmota devicename and friendlyname is used from tasmota settings

https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/blob/main/esp32_tasmota_scan/DeepSeek_Tasmota.ino

<img width="80%" height="80%"   src="https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/blob/main/esp32_tasmota_scan/Screenshot_20260120-055603_Chrome.jpg">


---
---

personal 

http://10.10.100.118/cm?cmnd=status
- {"Status":{"Module":0,"DeviceName":"Grond","FriendlyName":["Kamer"],"Topic":"tasmota_640D80","ButtonTopic":"0","Power":"0","PowerLock":"0","PowerOnState":3,"LedState":1,"LedMask":"FFFF","SaveData":1,"SaveState":1,"SwitchTopic":"0","SwitchMode":[4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"ButtonRetain":0,"SwitchRetain":0,"SensorRetain":0,"PowerRetain":0,"InfoRetain":0,"StateRetain":0,"StatusRetain":0}}

http://10.10.100.118/cm?cmnd=status%207
- {"StatusTIM":{"UTC":"2026-01-25T10:59:06Z","Local":"2026-01-25T11:59:06","StartDST":"2026-03-29T02:00:00","EndDST":"2026-10-25T03:00:00","Timezone":"+01:00","Sunrise":"08:29","Sunset":"17:35"}}  

---

https://ifconfig.me/ip returns internet ip

https://www.duckdns.org/

https://www.athom.tech/tasmota


<img width="673" height="633" alt="athom_iotorero_tasmota_eu-plug" src="https://github.com/user-attachments/assets/2908ee3f-1190-4612-9f20-05b0cd0e01f7" />


---

added a footer to the webpage, loaded from github (if possible) https://ldijkman.github.io/Swipe-ESP32-Timer-Switch/burn/footer.html
```
function loadExternalFooter(){fetch('https://ldijkman.github.io/Swipe-ESP32-Timer-Switch/burn/footer.html').then(response=>response.text()).then(html=>{document.getElementById('footerContent').innerHTML=html}).catch(err=>{console.log('Could not load external footer, using default')});}loadExternalFooter()
```
maybe handy for notify when something new 

only on esp32 wroom wrover version for now

---

28 1 26 esp32 wroom wrover version

added download button timer.html ace.html

ace editor mobile friendly

swipe visual timeslots circle timer demo not connected yet

download files from github to littlefs


---

after scan write devices.json to littlefs

maybe for timer.html

'''
{
  "devices": [
    {
      "ip": "10.10.100.112",
      "devicename": "Kamer Grote Hanglamp",
      "friendlyname": "Kamer",
      "module": "ESP32C3"
    },
    {
      "ip": "10.10.100.104",
      "devicename": "Keuken Raam",
      "friendlyname": "Keuken",
      "module": "Sonoff Basic"
    },
    {
      "ip": "10.10.100.124",
      "devicename": "Keuken WCD",
      "friendlyname": "Keuken WCD",
      "module": "Athom Plug V2"
    },
    {
      "ip": "10.10.100.108",
      "devicename": "LED",
      "friendlyname": "Keuken",
      "module": "ESP32C3"
    },
    {
      "ip": "10.10.100.125",
      "devicename": "WCD",
      "friendlyname": "Televisie",
      "module": "Athom Plug V3"
    },
    {
      "ip": "10.10.100.109",
      "devicename": "kamer",
      "friendlyname": "grond",
      "module": "Sonoff Basic"
    },
    {
      "ip": "10.10.100.114",
      "devicename": "Plafon LED",
      "friendlyname": "VoorKamer",
      "module": "Sonoff Basic"
    },
    {
      "ip": "10.10.100.122",
      "devicename": "WCD LED oost",
      "friendlyname": "VoorKamer",
      "module": "Athom Plug V3"
    },
    {
      "ip": "10.10.100.118",
      "devicename": "Grond",
      "friendlyname": "Kamer",
      "module": "Sonoff Basic R4"
    },
    {
      "ip": "10.10.100.123",
      "devicename": "WCD LED west",
      "friendlyname": "VoorKamer",
      "module": "Athom Plug V2"
    },
    {
      "ip": "10.10.100.111",
      "devicename": "Kamer Raam",
      "friendlyname": "Kamer",
      "module": "Sonoff Basic"
    }
  ],
  "total": 11,
  "timestamp": 7572695
}

'''



# ESP32 Tasmota Network Scanner

A powerful web-based network scanner and controller for Tasmota devices, running on ESP32 with a modern, responsive interface.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP32-green.svg)
![Framework](https://img.shields.io/badge/framework-Arduino-teal.svg)

## 📋 Table of Contents

- [Features](#features)
- [What It Does](#what-it-does)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Installation](#installation)
- [Configuration](#configuration)
- [Usage](#usage)
- [Web Interface](#web-interface)
- [API Endpoints](#api-endpoints)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)
- [License](#license)

## ✨ Features

### Network Scanning
- **Fast Parallel Scanning**: Scan up to 254 IP addresses simultaneously with configurable parallel tasks (1-20)
- **Customizable Ranges**: Define custom subnet bases and IP ranges
- **Quick Presets**: Pre-configured network ranges for common setups (192.168.1.x, 10.0.0.x, etc.)
- **Smart Detection**: Automatically identifies Tasmota devices on your network
- **Device Persistence**: Maintains device list across reboots and scans - newly discovered devices are added while existing ones are preserved

### Device Management
- **Real-time Control**: Toggle individual devices or control all devices at once
- **Live Status Updates**: WebSocket-based real-time device state monitoring
- **Device Information**: Display device names, friendly names, IP addresses, and module types
- **Name Editing**: Rename devices directly from the web interface
- **Device Removal**: Remove offline or unwanted devices from the list
- **Cached States**: Fast initial page load with background state updates

### User Interface
- **Responsive Design**: Works on desktop, tablet, and mobile devices
- **Dark Theme**: Easy-on-the-eyes dark interface
- **Visual Feedback**: Color-coded device states (green=ON, red=OFF)
- **Haptic Feedback**: Vibration support on mobile devices
- **Toggle Edit Mode**: Show/hide edit and remove buttons to prevent accidental changes
- **Progress Tracking**: Real-time scan progress with visual indicators

### Security
- **HTTP Authentication**: Optional username/password protection
- **Configurable Access**: Enable/disable authentication as needed
- **Secure Configuration**: Password fields with confirmation for changes

### Storage & Files
- **LittleFS Integration**: Local file storage on ESP32
- **File Downloads**: Download timer.html and ace.html from GitHub
- **File Editor**: Built-in ACE editor support for editing files
- **Persistent Storage**: Device list saved to JSON and reloaded on boot

### Network Configuration
- **Improv WiFi**: Easy WiFi setup via serial connection
- **WiFi Credentials Storage**: Saves and remembers WiFi settings
- **Flexible Timeouts**: Adjustable scan timeout (500-5000ms)
- **Network Diagnostics**: View signal strength and connection status

## 🎯 What It Does

### Primary Functions

1. **Network Discovery**
   - Scans your local network to find all Tasmota devices
   - Uses HTTP requests to detect devices running Tasmota firmware
   - Retrieves device information (DeviceName, FriendlyName, Module type)
   - Stores discovered devices persistently across reboots

2. **Device Control**
   - Toggle individual devices ON/OFF via the web interface
   - Control all devices simultaneously with "All ON" and "All OFF" buttons
   - Real-time status updates via WebSocket connection
   - Click device headers or toggle buttons to switch states

3. **Device Management**
   - Edit device friendly names and device names
   - Remove devices from the list
   - View device details (IP address, module type, current state)
   - Persistent device storage - devices remain in list even when offline

4. **Web Interface**
   - Access via web browser at `http://<ESP32-IP>`
   - Responsive design works on all screen sizes
   - Real-time updates without page refresh
   - Visual indicators for device states

5. **Configuration**
   - Configure network scan ranges
   - Adjust performance settings (parallel scans, timeouts)
   - Set up authentication
   - Download additional tools (timer, ACE editor)

### How It Works

1. **On Boot:**
   - ESP32 connects to WiFi (using saved or default credentials)
   - Loads configuration from preferences
   - Loads previously discovered devices from `/devices.json`
   - Starts web server on port 80
   - Devices from previous scans are immediately available

2. **During Scan:**
   - Creates multiple parallel tasks to scan IP ranges
   - Each task checks IPs for Tasmota devices
   - Discovered devices are added to the list
   - Previously known devices are kept even if not found
   - Progress updates sent to web interface in real-time
   - Results saved to LittleFS

3. **Device Control:**
   - User clicks device in web interface
   - WebSocket message sent to ESP32
   - ESP32 sends HTTP command to Tasmota device
   - Device responds with new state
   - ESP32 updates all connected clients

4. **State Management:**
   - Initial page load shows cached states (instant)
   - Background task updates states from actual devices
   - Updates broadcast to all connected clients
   - States cached for fast subsequent loads

## 🔧 Hardware Requirements

- **ESP32 Development Board** (any variant with WiFi)
  - Recommended: ESP32-WROOM-32, ESP32-DevKitC, or similar
  - Minimum 4MB flash (for LittleFS storage)
- **USB Cable** for programming and power
- **Stable 5V Power Supply** (especially for WiFi operations)

## 📦 Software Requirements

### Arduino IDE Setup

1. **Arduino IDE** (v1.8.19 or newer) or **PlatformIO**

2. **ESP32 Board Support:**
   - Add to Board Manager URLs: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Install "esp32" by Espressif Systems

3. **Required Libraries:**
```
   - WiFi (included with ESP32 core)
   - ESPAsyncWebServer (v3.x recommended)
   - AsyncTCP
   - HTTPClient (included with ESP32 core)
   - ArduinoJson (v6.x or v7.x)
   - Preferences (included with ESP32 core)
   - ImprovWiFiLibrary
   - LittleFS (included with ESP32 core)
```

### Installing Libraries

**Via Arduino Library Manager:**
1. Sketch → Include Library → Manage Libraries
2. Search and install:
   - `ArduinoJson` by Benoit Blanchon
   - `ImprovWiFiLibrary`

**Manual Installation (ESPAsyncWebServer & AsyncTCP):**
1. Download [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
2. Download [AsyncTCP](https://github.com/me-no-dev/AsyncTCP)
3. Extract to Arduino libraries folder

## 📥 Installation

### Step 1: Download Code
```bash
git clone https://github.com/ldijkman/Swipe-ESP32-Timer-Switch.git
cd Swipe-ESP32-Timer-Switch/esp32_tasmota_scan
```

### Step 2: Configure WiFi (Optional)
Edit the code to add default WiFi credentials:
```cpp
const char* default_ssid = "YourWiFiSSID";
const char* default_password = "YourWiFiPassword";
```

### Step 3: Upload to ESP32
1. Connect ESP32 via USB
2. Select correct board and port in Arduino IDE
3. Click Upload
4. Monitor serial output (115200 baud)

### Step 4: Find ESP32 IP Address
Check serial monitor for:
```
WiFi connected!
IP address: 192.168.1.XXX
```

### Step 5: Access Web Interface
Open browser and navigate to `http://192.168.1.XXX`

## ⚙️ Configuration

### Network Settings

**Via Web Interface:**
1. Click "Settings" button
2. Choose a preset or enter custom range:
   - **Subnet Base**: e.g., `192.168.1` or `10.0.0`
   - **Start IP**: Last octet (e.g., `2`)
   - **End IP**: Last octet (e.g., `254`)
3. Adjust scan settings:
   - **Parallel Scans**: 1-20 (more = faster but uses more memory)
   - **Timeout**: 500-5000ms (lower = faster but may miss devices)
4. Click "Save and Apply"

### Security Settings

**Enable Authentication:**
1. Open Settings
2. Check "Enable Login"
3. Set username and password
4. Confirm password
5. Click "Save and Apply"

**Default Credentials:**
- Username: `admin`
- Password: `admin`

### WiFi Configuration

**Method 1: Code (before upload)**
```cpp
const char* default_ssid = "YourNetwork";
const char* default_password = "YourPassword";
```

**Method 2: Improv WiFi (after upload)**
- Use Improv WiFi compatible tool
- Connect via serial
- Send WiFi credentials
- Credentials saved automatically

## 🖥️ Usage

### Scanning for Devices

1. Open web interface
2. Click "Scan Network"
3. Watch progress bar
4. Devices appear as they're found
5. Scan completes automatically

**Note:** New scans add newly found devices while keeping existing ones in the list.

### Controlling Devices

**Individual Control:**
- Click device header (colored bar with name) to toggle
- Click "Toggle" button to toggle
- Click IP address to open device web interface

**Bulk Control:**
- "All ON" - Turn on all devices
- "All OFF" - Turn off all devices

### Editing Devices

1. Click "Toggle Edit" to show edit/remove buttons
2. Click "Edit" on a device
3. Change Friendly Name and/or Device Name
4. Click "Save Names"
5. Changes are sent to the Tasmota device and cached locally

### Removing Devices

1. Enable edit mode ("Toggle Edit")
2. Click "×" button on device
3. Confirm removal
4. Device removed from list and saved

### Downloading Tools

1. Open Settings
2. Click "Download Timer & Ace Files"
3. Progress shown in popover
4. Access via:
   - Timer: `http://<ESP32-IP>/timer.html`
   - ACE Editor: `http://<ESP32-IP>/ace.html`

## 🌐 Web Interface

### Main Page Components

- **Header**: Title and navigation
- **Control Panel**: Main action buttons
  - Scan Network
  - Settings
  - All ON / All OFF
  - Toggle Edit
- **Progress Section**: Real-time scan feedback
- **Devices Grid**: Responsive grid of discovered devices
- **File Links**: Access to timer and ACE editor
- **Footer**: GitHub link and external content

### Device Card Layout
```
┌─────────────────────────┐
│ [×]  Friendly Name      │ ← Header (green=ON, red=OFF)
│ Device Name             │
│ 192.168.1.100          │ ← Clickable IP
│ Module: Sonoff Basic    │
│ [Toggle] [Edit]        │
└─────────────────────────┘
```

### Mobile Features

- Touch-optimized buttons
- Haptic feedback (vibration)
- Responsive layout
- Swipe-friendly interface

## 🔌 API Endpoints

### WebSocket (`ws://IP/ws`)

**Messages from Client:**
```javascript
// Get configuration
{"action": "get_config"}

// Save configuration
{"action": "save_config", "subnet": "192.168.1", "start": 2, "end": 254, ...}

// Start scan
{"action": "scan"}

// Toggle device
{"action": "toggle", "ip": "192.168.1.100"}

// Rename device
{"action": "rename", "ip": "192.168.1.100", "friendlyname": "Living Room", "devicename": "sonoff1"}

// Remove device
{"action": "remove", "ip": "192.168.1.100"}

// Control all devices
{"action": "all_on"}
{"action": "all_off"}

// Refresh device list
{"action": "refresh"}

// Download files
{"action": "download_files"}
```

**Messages from Server:**
```javascript
// Configuration data
{"type": "config", "subnet": "...", "start": 2, ...}

// Device list
{"type": "devices", "list": [{ip: "...", devicename: "...", ...}]}

// Single device update
{"type": "device_update", "ip": "...", "state": "ON"}

// Scan progress
{"type": "scan_progress", "current_ip": "...", "progress": 45.5, "found": 3}

// Scan started
{"type": "scan_start"}

// Download progress
{"type": "download_progress", "file": "timer.html", "status": "downloading", "progress": 50}

// Download complete
{"type": "download_complete", "success": true, "message": "..."}

// Info message
{"type": "info", "msg": "..."}
```

### HTTP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Main web interface |
| `/timer.html` | GET | Timer tool (if downloaded) |
| `/ace.html` | GET | ACE editor (if downloaded) |
| `/list` | GET | List files in LittleFS |
| `/status` | GET | LittleFS status (total/used bytes) |
| `/edit` | GET | Read file content |
| `/edit` | POST | Upload/save file |
| `/edit` | PUT | Create/rename file |
| `/edit` | DELETE | Delete file |

## 🐛 Troubleshooting

### ESP32 Won't Connect to WiFi

**Solutions:**
1. Check WiFi credentials in code
2. Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)
3. Move ESP32 closer to router
4. Check serial monitor for error messages
5. Try Improv WiFi setup

### No Devices Found

**Solutions:**
1. Verify Tasmota devices are on same network
2. Check IP range matches your network
3. Increase scan timeout in settings
4. Reduce parallel scans (may be too aggressive)
5. Ensure Tasmota devices are powered on
6. Check firewall isn't blocking HTTP requests

### Web Interface Not Loading

**Solutions:**
1. Verify ESP32 IP address
2. Try `http://` explicitly (not `https://`)
3. Clear browser cache
4. Try different browser
5. Check authentication settings
6. Restart ESP32

### Compilation Errors

**ESPAsyncWebServer const error:**
Edit `ESPAsyncWebServer.h` line ~1479:
```cpp
// Change this:
tcp_state state() { return static_cast<tcp_state>(_server.status()); }

// To this:
tcp_state state() const { return static_cast<tcp_state>(_server.status()); }
```

**ArduinoJson errors:**
- For v6: Use `DynamicJsonDocument`
- For v7: Use `JsonDocument`
- Update all instances in code

### Memory Issues

**Solutions:**
1. Reduce `maxParallelScans` (try 6-8)
2. Increase scan timeout
3. Use ESP32 with more RAM
4. Reduce number of devices in scan range

### Devices Not Persisting After Reboot

**Check:**
1. LittleFS mounted successfully (check serial)
2. `/devices.json` file exists (use ACE editor)
3. File permissions/storage space
4. No corruption in JSON file

## 📝 File Structure
```
esp32_tasmota_scan/
├── esp32_tasmota_scan.ino    # Main Arduino sketch
├── README.md                  # This file
└── LittleFS/                  # Files stored on ESP32
    ├── devices.json           # Discovered devices (auto-generated)
    ├── timer.html            # Timer tool (optional download)
    └── ace.html              # ACE editor (optional download)
```

## 🔄 Device Persistence Behavior

**On Boot:**
- Loads `/devices.json` from LittleFS
- Displays all previously known devices
- States shown as cached (may be stale)
- Background task updates states from actual devices

**After Scan:**
- New devices added to list
- Existing devices updated with latest info
- Offline devices kept in list (not removed)
- Updated list saved to `/devices.json`

**Manual Removal:**
- Click × button in edit mode
- Device removed from list
- Change saved to `/devices.json`
- Device won't reappear unless scanned again

## 🤝 Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🙏 Acknowledgments

- Based on Tasmota firmware
- Uses ESPAsyncWebServer by me-no-dev
- ArduinoJson by Benoit Blanchon
- Improv WiFi Library

## 📧 Support

- **Issues**: [GitHub Issues](https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/issues)
- **Discussions**: [GitHub Discussions](https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/discussions)
- **Repository**: [ESP32 Tasmota Scanner](https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/tree/main/esp32_tasmota_scan)

## 🔖 Version History

### v1.0.0 (Current)
- Initial release
- Parallel network scanning
- Device persistence across reboots
- WebSocket-based real-time updates
- Device management (edit, remove)
- Authentication support
- LittleFS file storage
- Improv WiFi support
- Responsive web interface
- Mobile-friendly design

---

**Made with ❤️ for the Tasmota community**






---

not made for home assistant configurators
