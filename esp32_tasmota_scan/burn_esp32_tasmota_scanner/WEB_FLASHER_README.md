# ESP32 Tasmota Scanner - Web Flasher

Flash your ESP32 directly from your browser using ESP Web Tools!

## 📁 Files Needed

To create a web flasher, you need these files:

### 1. HTML Flash Page
- `flash.html` - The web page with install button

### 2. Manifest File
- `manifest.json` - Tells ESP Web Tools which files to flash

### 3. Binary Files (you need to build these)
- `bootloader.bin` - ESP32 bootloader
- `partitions.bin` - Partition table
- `boot_app0.bin` - Boot application
- `tasmota_scanner.bin` - Your compiled firmware

## 🔨 How to Build Binary Files

### Option 1: Arduino IDE

1. Open `tasmota_final.ino` in Arduino IDE
2. Select **Tools → Board → ESP32 Dev Module**
3. Select **Sketch → Export Compiled Binary**
4. Find the files in your sketch folder:
   - `tasmota_final.ino.bootloader.bin` → rename to `bootloader.bin`
   - `tasmota_final.ino.partitions.bin` → rename to `partitions.bin`
   - `tasmota_final.ino.bin` → rename to `tasmota_scanner.bin`
5. Get `boot_app0.bin` from:
   - Windows: `C:\Users\[YourName]\AppData\Local\Arduino15\packages\esp32\hardware\esp32\[version]\tools\partitions\boot_app0.bin`
   - Mac: `~/Library/Arduino15/packages/esp32/hardware/esp32/[version]/tools/partitions/boot_app0.bin`
   - Linux: `~/.arduino15/packages/esp32/hardware/esp32/[version]/tools/partitions/boot_app0.bin`

### Option 2: PlatformIO

1. Create `platformio.ini`:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps = 
    me-no-dev/ESP Async WebServer@^1.2.3
    me-no-dev/AsyncTCP@^1.1.1
    bblanchon/ArduinoJson@^7.0.0
    khoih-prog/ImprovWiFiLibrary@^1.0.1
```

2. Build the project:
```bash
pio run
```

3. Find binaries in `.pio/build/esp32dev/`:
   - `bootloader.bin`
   - `partitions.bin`
   - `firmware.bin` → rename to `tasmota_scanner.bin`
   - `boot_app0.bin` (in tools folder)

## 📤 Deployment

### GitHub Pages (Free & Easy)

1. Create a new repository on GitHub
2. Upload these files:
   ```
   your-repo/
   ├── flash.html
   ├── manifest.json
   ├── bootloader.bin
   ├── partitions.bin
   ├── boot_app0.bin
   └── tasmota_scanner.bin
   ```
3. Go to **Settings → Pages**
4. Select **Source: main branch**
5. Your flasher will be at: `https://yourusername.github.io/your-repo/flash.html`

### Alternative Hosting

You can also host on:
- **Netlify** (drag & drop folder)
- **Vercel** (git push to deploy)
- **Your own web server** (just upload files)

## 🌐 Usage

1. Visit your deployed `flash.html` page
2. Click "Install ESP32 Tasmota Scanner"
3. Select your ESP32's COM port
4. Wait for flashing to complete
5. Configure Wi-Fi using Improv (https://www.improv-wifi.com)
6. Access the web interface at the IP shown in serial monitor

## 📋 Browser Requirements

- ✅ Chrome (Desktop & Android)
- ✅ Edge (Desktop)
- ✅ Opera (Desktop)
- ❌ Firefox (not supported)
- ❌ Safari (not supported)

## 🔧 File Sizes

Typical binary sizes:
- `bootloader.bin`: ~24 KB
- `partitions.bin`: ~3 KB
- `boot_app0.bin`: ~4 KB
- `tasmota_scanner.bin`: ~1.2 MB (varies)

## 📝 Customization

### Change Default Wi-Fi Credentials

Edit in `tasmota_final.ino` before building:
```cpp
const char* default_ssid = "YourWiFiName";
const char* default_password = "YourPassword";
```

### Change Network Range

Edit in `tasmota_final.ino`:
```cpp
String subnetBase = "192.168.1.";
int startOctet = 2;
int endOctet = 254;
```

### Adjust Scan Performance

In web interface Settings or edit defaults:
```cpp
int maxParallelScans = 12;  // 1-20
int scanTimeoutMs = 1000;    // 500-5000ms
```

## 🐛 Troubleshooting

### "No port selected"
- Make sure ESP32 is plugged in via USB
- Try a different USB cable (some are charging-only)
- Install CP210x or CH340 drivers if needed

### "Permission denied"
- Close Arduino IDE, PlatformIO, or any serial monitor
- On Linux, add your user to `dialout` group

### "Failed to connect"
- Press and hold BOOT button during flashing
- Try a different USB port
- Reset ESP32 before flashing

### Page doesn't load
- Make sure you're using HTTPS (required for Web Serial)
- GitHub Pages automatically uses HTTPS
- Check browser console for errors

## 🔗 Links

- [ESP Web Tools Documentation](https://esphome.github.io/esp-web-tools/)
- [Improv Wi-Fi](https://www.improv-wifi.com/)
- [GitHub Repository](https://github.com/ldijkman/Swipe-ESP32-Timer-Switch/tree/main/esp32_tasmota_scan)

## 📄 License

Same as parent project - see main repository
