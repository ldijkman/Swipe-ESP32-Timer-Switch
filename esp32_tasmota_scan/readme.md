



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

added a footer to the webpage, loaded from github (if possible)

maybe handy for notify when something new 

only on esp32 wroom wrover version for now
