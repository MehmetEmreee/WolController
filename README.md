# WoL Controller

[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino_Core_3.x-00979D.svg?logo=arduino)](https://github.com/espressif/arduino-esp32)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-E7352C.svg?logo=espressif)](https://www.espressif.com/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

*For Turkish version, [scroll down](#wol-controller-türkçe)* / *Türkçe sürüm için [aşağı kaydırın](#wol-controller-türkçe)*

Production-grade, asynchronous C++ firmware for the **WT32-ETH01** (ESP32 + LAN8720A) module. It provides a highly reliable, dual-stack network bridge allowing you to power on your PC (via Wake-on-LAN) and monitor its status completely remotely using a **Telegram Bot**.

Because it implements two network interfaces simultaneously:
- **Wi-Fi Interface (DHCP)**: Connects to the Internet to communicate with the Telegram API.
- **Ethernet Interface (Static)**: Plugs directly into your PC (isolated LAN) to send the WoL Magic Packet and perform ICMP Ping checks.

## ✨ Features

- **Telegram Bot Integration:** Control everything via an interactive Telegram chat. Long-polling implemented with exponential backoff for internet drops.
- **Dual-Stack Network:** Event-driven network manager handling Wi-Fi (`STA`) and Ethernet (`LAN8720A`) simultaneously.
- **Automated Watchdog:** Configurable state-machine ping watchdog. If your PC drops off the Ethernet network (power cut, crash), the ESP32 can automatically send a Wake-on-LAN recovery packet!
- **Over-The-Air (OTA) Updates:** Flash new firmware wirelessly instead of connecting USB adapters.
- **Remote Log Server:** Telnet into port `23` (`telnet <ESP_IP> 23`) from your local network to read live serial logs with a built-in 4KB circular history buffer.
- **Zero Heap Allocations (Hot Path):** Extensive use of stack buffers (`StaticJsonDocument<N>`) and bounded `snprintf` to prevent heap fragmentation.
- **Security Check:** A strict chat ID whitelist drops and logs any unauthorized Telegram commands. Access credentials are kept exclusively in ignored header files.

## 📦 Hardware Requirements

1. **WT32-ETH01** Development Board.
2. Standard Ethernet RJ45 patch cable.
3. 5V Power Supply or USB-to-TTL serial adapter (for initial flashing).

## 🛠️ Installation & Setup

### 1. Configure Secrets

We keep all sensitive data out of version control.
1. Copy the example credentials template:
   ```bash
   cp credentials.example.h credentials.h
   ```
2. Open `credentials.h` and fill in your actual data:
   - Your Wi-Fi `SSID` and `PASSWORD`.
   - Your Telegram `@BotFather` `BOT_TOKEN`.
   - Your Telegram `ALLOWED_CHAT_ID` (Use [@userinfobot](https://t.me/userinfobot) to find yours).
   - Your Target PC's Ethernet `TARGET_MAC` address.
   - A secret `OTA_PASSWORD`.

### 2. Required Libraries

Make sure you have the following libraries installed in the Arduino IDE or PlatformIO:
- **ArduinoJson** (v6.x)
- **ESP32Ping** (by dvarrel)
- ESP32 Core **3.x** Toolkit

### 3. Compilation & Flashing

Select the **"WT32-ETH01"** board from the ESP32 Arduino Core board manager.
Upload via standard serial connection the very first time. Subsequent updates can be performed via OTA (Network Port).

## 🤖 Telegram Commands

Once running, send these commands to your Bot:

| Command | Description |
|---|---|
| `/start` or `/help` | 📋 Show the main usage menu. |
| `/wake` | ⏳ Instantly sends the IEEE 802.3 Wake-on-LAN magic packet. |
| `/status` | 📡 Sends an ICMP ping to the target PC and returns 🟢 ONLINE / 🔴 OFFLINE with latency. |
| `/watchdog on` | 🐕 Activates the automatic ping monitor & auto-recovery WoL system. |
| `/watchdog off` | 🛑 Disables the automated watchdog. |
| `/info` | ℹ️ Shows uptime, IP addresses, RSSI, free heap, OTA status, and watchdog state. |
| `/reboot` | 🔄 Restarts the ESP32 microcontroller securely. |

---
---

# WoL Controller (Türkçe)

**WT32-ETH01** (ESP32 + LAN8720A) modülü için asenkron, tam korumalı, üretim seviyesi C++ yazılımı. Bu yazılım, bilgisayarınızı **Telegram Bot** üzerinden uzaktan açmanıza (Wake-on-LAN) ve anlık durumunu ping atarak izlemenize olanak sağlayan çift köprülü (dual-stack) oldukça kararlı bir sistem kurar.

Sistem aynı anda iki ayrı ağ donanımını kontrol eder:
- **Wi-Fi Ağı (DHCP):** Sadece cihazın internete çıkıp Telegram API'si ile haberleşmesi içindir.
- **Ethernet Ağı (StatikIP):** Doğrudan hedeflenen bilgisayara (İzole-LAN) doğrudan veya mevcut switch üzerinden bağlanarak ICMP Ping kontrolü yapar ve WoL "Sihirli Paket" gönderir.

## ✨ Özellikler

- **Telegram Bot Entegrasyonu:** İnteraktif sohbet üzerinden kontrol. İnternet kesilirse bile *exponential backoff* algoritmasıyla log düşerek bot kendisini sürekli çevrimiçi tutmaya çalışır.
- **Çift-Yönlü Altyapı:** Wi-Fi ve Kablolu LAN ağının eşzamanlı ve event-driven (olay örgülü) yönetimi.
- **Otomatik Watchdog:** Ayarlanabilir "Durum Makineli (state-machine)" ping tabanlı koruyucu sistem. Bilgisayara giden ping istekleri timeouta düşerse (örneğin elektrik gidip gelmesi veya Windows mavi ekran atması durumunda) sistem PC'yi kurtarmak için otomatik WoL paketleri atar!
- **Over-The-Air (OTA):** Firmware güncellemenizi artık USB bağlamadan kablosuz olarak atın.
- **Uzak Log Sunucusu:** Herhangi bir cihaz üzerinden kablosuz olarak ağ üzerindeki ESP loglarını okuyabilirsiniz (`telnet <ESP_IP> 23`). Giriş yapıldığında son 4KB lık log geçmişi size telnet üzerinden yansıtılır.
- **Heap Temizliği İşlemleri:** Tüm log veya network döngü yüklerinde `StaticJsonDocument<N>` gibi RAM'e işlenip biten anlık bellek işlemleri kullanılarak sistem haftalarca çökmeden stabil çalıştırılır.
- **Güvenlik Mimarisi:** Botunuzla konuşan kişi eğer whitelist listesinde *(ALLOWED_CHAT_ID)* yoksa bot işlemi tamamen düşürür, asla komut okumaz ve yapılan işlemi size bir Brute Force/Hack girişimi olarak loglar. Ve anahtarlarınız da `.gitignore` dosyası aracılığıyla repoya dahil edilmez.

## 📦 Gerekli Donanımlar

1. **WT32-ETH01** Geliştirme Kartı.
2. Basit kısa bir ethernet RJ45 patch kablosu.
3. 5V Güç Adaptörü veya ESP'ye ilk kurulum için USB-to-TTL cihazı.

## 🛠️ Kurulum

### 1. Parolaların Eklenmesi

Bilgilerin GIT/Github üzerinde başkaları tarafından görülmemesi için şu adımları izleyin:
1. Öncelikle taslak dosyanızı normal dosyaya çevirin:
   ```bash
   cp credentials.example.h credentials.h
   ```
2. Şimdi `credentials.h` açın ve kısımları kendi ağ bilgilerinizle girin:
   - Wi-Fi için `SSID` ve `PASSWORD`.
   - Telegram için `@BotFather` 'dan aldığınız `BOT_TOKEN` anahtarı.
   - Telegram kendi ID adresiniz `ALLOWED_CHAT_ID` numaranız ([@userinfobot](https://t.me/userinfobot) botundan alabilirsiniz).
   - Bilgisayarınızın Ethernet `TARGET_MAC` adresi.
   - Güvenli Telnet veya Update atmanız için OTA Şifreniz.

### 2. Kütüphaneler

Aşağıdaki kütüphanelerin Arduino IDE veya PlatformIO da kurulu olmasına emin olun:
- **ArduinoJson** (v6.x sürümü olmalı, v7 değil).
- **ESP32Ping** (dvarrel ait olan paket).
- ESP32 Boards Core **3.x** versiyonu.

### 3. Derleme & Upload

ESP32 kartları arasından **"WT32-ETH01"** kartını seçip, kodunuzu ilk kez TTL aracılığıyla flashlayın. Sonraki güncellemelerinizi kablosuz (OTA) ağ portundan yollayabilirsiniz.

## 🤖 Telegram Komutları

Çalıştığında bot üzerinden yollayabileceğiniz komutlar:

| Komut | Açıklama |
|---|---|
| `/start` ya da `/help` | 📋 Ana kullanım menüsünü gösterir. |
| `/wake` | ⏳ O an doğrudan IEEE 802.3 Wake-on-LAN Sihirli Paketi atar. |
| `/status` | 📡 Hedef bilgisayara ICMP ağ ping'i yollar ve 🟢 ÇEVRİMİÇİ / 🔴 ÇEVRİMDIŞI şeklinde ağ gecikme hızına göre (Ping - MS) sonuç yazar. |
| `/watchdog on` | 🐕 Watchdog'u (Zamanlı Ping Monitorü + Otomatik PC Geri Kalkış) görevini başlatır. |
| `/watchdog off` | 🛑 Otomatik Watchdog devriyesini kapatır. |
| `/info` | ℹ️ Modülün çalışma süresi (Uptime), IP adresleri, Wifi gücü, Boş Belleği ve Watchdog durumu gibi mühendislik modüllerini yazar. |
| `/reboot` | 🔄 Modülü yeniden bailatır. |

---
*C++17 mimarisi ile temiz olarak oluşturulmuştur. Geliştirici kimlikleri (sırları) repoya dahil değildir.*
