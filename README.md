# Wraith - ESP32

> Portable WiFi and Bluetooth security assessment platform running directly on ESP32 with a web SPA interface accessible from any phone or laptop. No extra software, no special OS - just flash and use.

**Version: v5.0** | [Install](#installation) | [Usage](#usage) | [Attacks](#attack-modules) | [Architecture](#technical-architecture)

---


## Table of Contents

1. [What is Wraith](#what-is-wraith)
2. [How it Works](#how-it-works)
3. [Supported Hardware](#supported-hardware)
4. [Installation](#installation)
5. [Usage](#usage)
6. [Web Interface](#web-interface)
7. [Recon Modules](#recon-modules)
8. [Attack Modules](#attack-modules)
9. [Advanced Capture](#advanced-capture)
10. [Bluetooth](#bluetooth)
11. [Keyboard Shortcuts](#keyboard-shortcuts)
12. [Technical Architecture](#technical-architecture)
13. [Legal Notice](#legal-notice)

---

## What is Wraith

Wraith turns an ESP32 microcontroller into a full wireless security audit tool. The device operates as an **autonomous Access Point**: on boot it creates its own WiFi network (`Wraith`) and hosts a complete web application accessible at `192.168.4.1`.

### Why ESP32?

The ESP32 has dual 240 MHz cores, integrated 802.11 b/g/n WiFi and Bluetooth Classic + BLE. The chip supports **promiscuous mode** (capture all packets in the air), **raw 802.11 frame injection** (send forged packets), and can operate in multiple modes simultaneously (AP + Station + Promiscuous).

### Use Cases

- Security auditing on your own networks or with explicit written authorization
- Verifying vulnerabilities before real attackers do
- Wireless penetration testing (WiFi pentest)
- Security education and network research
- WPA2 handshake capture for password strength analysis
- Detecting vulnerable networks (active WPS, open networks, PMF disabled)

---

## How it Works

```
                    [ESP32]
                   /       \
    WiFi Radio              WiFi Radio
    (AP Mode)               (Station/Monitor Mode)
         |                        |
   Wraith              Promiscuous
   192.168.4.1                (packet capture)
         |                        |
   [User Browser]            [802.11 Packets]
         |                    EAPOL, Beacons,
   Web Interface              Probes, Data...
   (SPA in LittleFS)
```

The ESP32 operates in two radio modes simultaneously:
1. **AP (Access Point)** - creates the `Wraith` network for the user to connect and access the control panel
2. **Monitor/Promiscuous** - uses the same radio to capture or inject packets on target networks

The web interface is a **Single Page Application (SPA)** stored in the ESP32's LittleFS filesystem. No external dependencies - all HTML, CSS and JavaScript are served by the device itself.

Browser-to-ESP32 communication uses:
- **HTTP REST** (`/api/*`) - for commands and queries
- **WebSocket** (`/ws`) - for real-time logs and continuous updates

---

## Supported Hardware

| Chip | Board | Flash | RAM | Notes |
|------|-------|-------|-----|-------|
| **ESP32-S3 N16R8** | WROOM-1 N16R8 44-pin | **16MB** | **8MB PSRAM** | **Recommended - best performance** |
| **ESP32-D0WD-V3** | DevKit v1 / WROOM-32 | 4MB | 520KB SRAM | Supported, baseline hardware |
| **ESP32-S3** | DevKitC-1 | 4MB | 512KB SRAM | Supported - native USB OTG |
| **ESP32-C3** | DevKitM-1 | 4MB | 400KB SRAM | Supported - RISC-V, no BT Classic |

### ESP32-S3 N16R8 - Recommended Hardware

The **ESP32-S3 WROOM-1 N16R8** (available on Shopee for R$13-27) is the ideal hardware for Wraith:

| | ESP32 D0WD-V3 | ESP32-S3 N16R8 |
|---|---|---|
| **CPU** | Xtensa LX6 Dual Core 240MHz | **Xtensa LX7 Dual Core 240MHz** |
| **Flash** | 4MB | **16MB** (4x more) |
| **SRAM** | 520KB | 512KB + 16KB RTC SRAM |
| **PSRAM** | - | **8MB OPI PSRAM** |
| **Total RAM** | 520KB | **~8.5MB effective** |
| **GPIOs** | 34 | **45 GPIOs** |
| **USB** | Via CH340 (UART) | **Native USB OTG** + CH343P serial |
| **BLE** | 4.2 | **5.0** (better BLE Spam range) |
| **Security** | AES, SHA | **AES, SHA, RSA, ECC, RNG + Secure Boot** |
| **Partitions** | huge_app.csv (3MB app) | **partitions_16mb.csv (5MB app + 6MB LittleFS)** |

**Why 16MB + 8MB PSRAM changes everything:**
- Much larger packet capture buffers in RAM (8MB vs 520KB)
- Larger capture logs stored directly on device
- Ability to store small wordlists in LittleFS (6MB available)
- LX7 CPU is ~37% faster than LX6 per clock cycle
- Native Secure Boot and Flash Encryption

**Board components:**

```
[ESP32-S3 N16R8 - 44 pins, 57.15mm x 27.94mm]

  Left side:              Right side:
  3.3V, 3.3V, RST         GND, TX, RX
  GPIO 4,5,6,7,15,16      GPIO 1,2,42,41,40,39
  GPIO 17,18,8,3,46       GPIO 38,37,36,35,0
  GPIO 9,10,11,12,13,14   GPIO 45,48,47,21,20,19
  5V                      GND, GND

  Module: ESP32-S3-WROOM-1-N16R8
  RGB LED: WS2812 (GPIO 48)
  USB OTG: left USB-C (native USB)
  USB Serial: right USB-C (CH343P)
  Buttons: RST (GPIO 38), BOOT (GPIO 0)
```

![Pinout ESP32-S3 N16R8](docs/hardware/esp32-s3-n16r8/01-pinout.jpg)

| Components | Dimensions |
|---|---|
| ![Components](docs/hardware/esp32-s3-n16r8/02-diagrama-componentes.jpg) | ![Dimensions](docs/hardware/esp32-s3-n16r8/03-dimensoes-n16r8.jpg) |

---

## Installation

### Requirements

- Python 3.8+
- PlatformIO Core
- Git
- USB cable (USB-C)

### Step 1 - Install PlatformIO

**Windows:**
```powershell
pip install platformio
pio --version
```

**Linux/macOS:**
```bash
pip3 install platformio
pio --version
```

> **Alternative:** install the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension for VS Code.

### Step 2 - Clone the Repository

```bash
git clone https://github.com/Ghost-Bx1/Ghost-Raptor-Mobile-ESP32.git Wraith
cd Wraith
```

**Project structure:**
```
Wraith/
+-- src/
|   +-- main.cpp              # Main loop, WiFi/BLE setup
|   +-- config.h              # Passwords, limits, pins
|   +-- wifi/
|   |   +-- scanner.h         # WiFi scanner
|   |   +-- deauth.h          # Deauth + assocFlood + reconnectBlock
|   |   +-- evil_twin.h       # Evil Twin + captive portal
|   |   +-- sniffer.h         # EAPOL/handshake/PMKID capture
|   |   +-- pmkid_fisher.h    # PMKID without clients (Auth+Assoc inject)
|   |   +-- karma.h           # Karma + MANA attack
|   |   +-- beacon_flood.h    # Beacon flood + channel hop + hidden
|   |   +-- hidden_scanner.h  # Hidden SSID revelation
|   |   +-- wifite_mode.h     # Auto-attack orchestrator
|   |   +-- wps_scanner.h     # WPS PIN/Pixie Dust detection
|   +-- bluetooth/            # BLE Spam (Apple/Samsung/Windows)
|   +-- web/server.h          # ESPAsyncWebServer + REST + WebSocket
+-- data/
|   +-- index.html            # Complete SPA (~3000 lines)
+-- platformio.ini            # Build config per board
+-- partitions_16mb.csv       # Partition table for N16R8
```

### Step 3 - Connect the ESP32

**ESP32-S3 N16R8:**
- Use the **right USB-C** port (CH343P serial) to flash
- Windows installs driver automatically (CH343P)
- Linux: check `ls /dev/ttyACM*` or `/dev/ttyUSB*`

**If the ESP32 is not detected:**
- ESP32 original: install [CH340 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
- ESP32-S3 N16R8: install [CH343P driver](https://www.wch-ic.com/downloads/CH343SER_EXE.html)

### Step 4 - Flash Firmware

```bash
# ESP32-S3 N16R8 - 16MB Flash + 8MB PSRAM (RECOMMENDED)
pio run --target upload -e esp32-s3-n16r8

# ESP32 original (WROOM-32, DevKit v1)
pio run --target upload -e esp32dev

# ESP32-S3 generic (4MB Flash)
pio run --target upload -e esp32-s3

# ESP32-C3
pio run --target upload -e esp32-c3
```

**Specify port manually:**
```bash
pio run --target upload -e esp32-s3-n16r8 --upload-port COM3
# Linux:
pio run --target upload -e esp32-s3-n16r8 --upload-port /dev/ttyUSB0
```

**Manual bootloader mode** (if upload fails):
1. Hold the **BOOT** button
2. Press and release **RST**
3. Release **BOOT**
4. Run the upload command again

**Linux serial permissions:**
```bash
sudo usermod -a -G dialout $USER
# Log out and log back in
```

### Step 5 - Flash Filesystem (Web Interface)

The LittleFS filesystem contains `data/index.html` with the entire web interface. **This step is required** - without it the panel will not load.

```bash
# ESP32-S3 N16R8 (6MB LittleFS available)
pio run --target uploadfs -e esp32-s3-n16r8

# ESP32 original
pio run --target uploadfs -e esp32dev
```

> **Note:** Firmware and Filesystem are separate uploads. If you modify `data/index.html`, run `uploadfs` again. If you modify C++ code in `src/`, run `upload` again.

### Step 6 - First Access

After upload completes, the ESP32 reboots automatically:

1. Wait ~3 seconds for the ESP32 to initialize
2. On your phone or laptop, open **WiFi settings**
3. Connect to: **`Wraith`**
4. WiFi password: **`wraith123`**
5. Open browser and navigate to: **`http://192.168.4.1`**
6. Web panel password: **`wraith`**

> **Tip:** You can also access via **`http://wraith.local`** (mDNS) instead of typing the IP.

### Troubleshooting

| Problem | Cause | Solution |
|---|---|---|
| Upload fails: "No serial data received" | ESP32 not in bootloader | Hold BOOT + press RST |
| COM port not detected | Driver not installed | Install CH340 (ESP32) or CH343P (S3) |
| Blank web panel / 404 | Filesystem not uploaded | Run `uploadfs` |
| "COM3 port does not exist" | Port changed or disconnected | Check Device Manager |
| Upload OK but Wraith WiFi not found | Firmware error | Check serial: `pio device monitor` |
| Page loads but no data | Session cookie expired | Log in again |

**View real-time logs:**
```bash
pio device monitor --port COM3 --baud 115200
```

---

## Usage

### Basic Workflow

```
1. Connect to Wraith WiFi
         |
2. Open http://192.168.4.1
         |
3. Log in (password: wraith)
         |
4. Run WiFi Scanner
         |
5. Select target network in the table
         |
6. Choose the desired attack
         |
7. Monitor results in the panel
```

### Auto-Login

The panel saves a session token in the browser's `localStorage` for **30 minutes**. Revisiting `192.168.4.1` within that period auto-logs you in without re-entering the password.

### Select a Target

1. Go to **WiFi Scanner** and click **Scan**
2. In the results table, click the **Attack** button next to a network
3. The target is saved (SSID, BSSID, channel) and pre-filled in all attack modules
4. The target row is highlighted in red
5. **Quick Attack** at the top of the scanner activates Deauth + handshake capture in one click

---

## Web Interface

### Topbar

| Element | Description |
|---|---|
| **Attack badges** | DEAUTH / SNIFFER / EVIL TWIN / BEACON / KARMA / NUCLEAR - pulse when active |
| **RAM** | Free heap memory on ESP32 in real time |
| **CH** | Current WiFi channel of the sniffer |
| **Uptime** | Device uptime |
| **Sound** | Toggle panel alert sounds |
| **Stealth** | Stealth mode - reduces visible transmissions |
| **Theme** | Toggle light/dark |
| **Password** | Change access password |
| **Logout** | End session |

### Sidebar

Organized by category:
- **Recon**: WiFi Scanner, Packet Sniffer, Probe Requests, Hidden Scanner
- **WiFi Attacks**: Auto-Attack, LED Panel, Nuclear, Deauth, Evil Twin, Beacon Flood, Karma
- **Advanced Capture**: PMKID Fisher, WPS Scanner, Target Intel
- **Bluetooth**: BLE Scanner, BLE Spam, BT Classic
- **Hardware**: Radio 2
- **System**: Logs, System Info

Counters beside menu items update in real time (e.g., number of handshakes, captured credentials).

### Live Console

At the bottom of the screen is the WebSocket log console. Each message has a severity level with color:
- **Gray** - information
- **Yellow** - warning
- **Red** - error
- **Green** - success/capture
- **Cyan** - important data

The green/red dot beside the title shows WebSocket connection status. If it drops, the panel reconnects automatically.

---

## Recon Modules

### WiFi Scanner

Performs a passive 802.11 scan of all nearby networks.

**Collected data per network:**

| Field | Description |
|-------|-------------|
| **SSID** | Network name (empty = hidden network) |
| **BSSID** | AP MAC address |
| **Channel** | 1-13 (operating frequency) |
| **RSSI** | Signal strength in dBm (-45 = strong, -85 = weak) |
| **Distance** | Estimated via Log-Distance Path Loss model (n=2.7 indoor) |
| **Vendor** | AP manufacturer via OUI table (~70 vendors) |
| **Auth** | Security type (Open, WEP, WPA, WPA2, WPA3, WPS) |
| **Clients** | Associated devices detected by the sniffer |

**Channel Map:** visual distribution of networks per channel (1-13). Click a bar to filter the table. Click again to clear the filter.

**Export CSV:** exports the full table to CSV for spreadsheet analysis.

**Auto-Scan:** triggers a new scan every 30 seconds automatically.

---

### Packet Sniffer

Puts the WiFi radio in **promiscuous mode**: all 802.11 packets in the air are captured, including from networks the device is not associated with.

**How it works:**
1. `esp_wifi_set_promiscuous(true)` activates promiscuous mode
2. `esp_wifi_set_promiscuous_rx_cb()` registers a callback for each received packet
3. The firmware parses the 802.11 header of each frame and categorizes by type

**Captured frame types:**

| Type | Description |
|------|-------------|
| **Management** | Beacons, probe requests/responses, auth, assoc, deauth, disassoc |
| **Data** | Data frames from devices associated to APs |
| **EAPOL** | WPA2 authentication protocol - 4-way handshake |
| **PMKID** | Session key identifier - capture without full handshake |

**Channel Hopping:** with channel set to `0`, the sniffer jumps between channels every `hop_interval` ms (default: 200ms).

**WPA2 Handshake:**

```
Client                           AP (Access Point)
   |                                   |
   |<------- M1: ANonce (AP nonce) ----|
   |--- M2: SNonce + MIC (PTK) ------->|  <- most important (crackable)
   |<------- M3: GTK encrypted --------|
   |--- M4: Confirmation -------------->|
```

- **M1+M2** (state `CRACKABLE`) - sufficient for cracking with hashcat format 22000
- **M1+M2+M3+M4** (state `COMPLETE`) - full handshake, compatible with aircrack-ng

**Cracking the handshake:**
```bash
# Export from panel: "Export Hashcat" button
hashcat -m 22000 handshakes.txt wordlist.txt --force
# or
aircrack-ng -w wordlist.txt capture.pcap
```

**Export options:**
- **PCAP** - capture in Wireshark/tcpdump format for offline analysis
- **Hashcat** - hashes in hashcat-compatible format for cracking

---

### Probe Requests

WiFi devices continuously search for known networks by sending **probe requests** with the SSIDs of networks in their history.

**What they reveal:**
- List of WiFi networks the device has visited in the past
- Device MAC address (enables tracking)
- Device presence in the environment
- Behavioral patterns (where the user goes regularly)

The panel collects and displays: source MAC, vendor, list of searched SSIDs, RSSI and estimated distance.

---

### Hidden Scanner

Hidden SSID networks do not broadcast beacons with the network name, but still respond to **directed probe requests**.

**Revelation technique:**
1. The firmware collects BSSIDs of APs with empty SSID during scan
2. For each BSSID, sends probe requests with null SSID (directed probe)
3. If the AP responds with the real SSID, it has been revealed

> **Myth busted:** hiding the SSID is NOT an effective security measure. Any basic tool reveals hidden SSIDs by capturing probe requests from associated clients or directed probes.

---

### Target Intel

Detailed fingerprinting of devices associated to a monitored network in promiscuous mode.

**Collected data:**
- MAC address and device vendor
- DNS queries made (unencrypted UDP port 53 packets)
- HTTP credentials captured in unencrypted traffic
- HTTP User-Agents (device OS and browser)
- Activity timestamps and patterns

**Device fingerprinting via probe IEs:**

| IE Tag | Capability | Device type |
|--------|-----------|-------------|
| 45 | HT Capabilities | 802.11n |
| 191 | VHT Capabilities | 802.11ac |
| 255+35 | HE Capabilities | 802.11ax (Wi-Fi 6) |
| 221 (OUI 00:17:F2) | Vendor Specific | Apple |
| 221 (OUI 00:50:F2 type 4) | Vendor Specific | Windows |

**MAC randomization detection:** if bit U/L (bit 1 of first octet) = 1, the MAC is locally administered (privacy/randomized).

---

## Attack Modules

> **WARNING:** Use only on networks you own or have explicit written authorization to test. Unauthorized use is a crime.

For complete technical documentation of each attack module, see [ATTACKS.md](ATTACKS.md).

### Deauth Attack

Forcefully disconnects any device from a WiFi network by injecting forged 802.11 deauthentication frames. Works against networks without PMF (Protected Management Frames).

**Techniques:** Deauth AP->Client, Deauth Client->AP, Disassociation, 16 Reason Code rotation, Null Data Frame (PM=1), SA Query Flood, Auth Flood.

**Intensity modes:**

| Mode | Packets/cycle | Use |
|------|--------------|-----|
| Soft | 64 | Initial test, less interference |
| Normal | 256 | General use |
| Aggressive | 512 | Maximum impact, PMF bypass attempt |

---

### Evil Twin (Fake Network + Captive Portal)

Creates an identical copy of a real WiFi network - same name, same security type (but no password). Combined with Deauth to force clients to connect to the fake AP, then presents a phishing page requesting the WiFi password.

**Attack flow:**
```
1. Recon: scan and identify target (SSID, BSSID, channel, RSSI)
2. Deauth: kick all clients off the real network
3. Clone AP: broadcast beacons with same SSID but no password
4. DNS capture: respond to all domains with our IP
5. Captive portal: redirect all traffic to fake password page
```

**Available portal templates:** WiFi Reconnect, ISP/Provider, Router Admin.

---

### Beacon Flood

Creates hundreds of fake WiFi networks simultaneously, filling the WiFi list of nearby devices with non-existent networks.

**Modes:**

| Option | Description |
|--------|-------------|
| Channel Hop | Hops channels 1-13 to spread fake networks |
| Hidden Mode | Broadcasts beacons with hidden SSID |
| WPA3 Mode | Includes WPA3 security fields to fool advanced scanners |

Each fake SSID gets a unique deterministic MAC generated via FNV-1a hash.

---

### Karma Attack

Exploits the automatic reconnection behavior of devices searching for known networks. Wraith listens for probe requests and responds with a matching fake AP.

**Standard Karma:** responds only to received probe requests.

**MANA mode:** also actively broadcasts beacons with all previously seen SSIDs, increasing capture rate against devices not actively probing.

The Probe Response uses the exact IEs (Information Elements) from the real AP if previously captured by the AP Clone module, making it indistinguishable from the real network.

Ref: Dominic White & Ian de Villiers, SensePost - DEF CON 22, 2014.

---

### Nuclear Auto-Attack

Activates all attack modules simultaneously to capture the maximum information from all networks in range:

| Phase | Active module | Captures |
|-------|--------------|----------|
| 1 | Packet Sniffer with channel hopping | EAPOL handshakes and PMKIDs from all nearby networks |
| 2 | Deauth Attack | Forces client reconnections (triggers handshakes) |
| 3 | PMKID Fisher | Attempts PMKID capture per AP without waiting for clients |
| 4 | Karma Attack | Captures devices reconnecting to known networks |

---

### Auto-Attack / Wifite Mode

Automates the audit process network by network, using three techniques in sequence:

```
For EACH WPA2 network found:

  ATTEMPT 1 - PMKID Fisher (default: 30s)
  - Try to capture PMKID without needing a connected client
  - If captured: save and move to NEXT network
  - If not in 30s: go to attempt 2

  ATTEMPT 2 - Deauth + Handshake (default: 60s)
  - Send deauth to kick clients, capture WPA2 handshake on reconnect
  - If captured: save and move to NEXT network
  - If not in 60s: go to attempt 3

  ATTEMPT 3 - Evil Twin (default: 120s)
  - Clone network, take down real AP, show captive portal
  - If password captured: save and move to NEXT network
  - If not in 120s: mark as failed and move on
```

**Adaptive timeouts by RSSI:**

| Signal | PMKID timeout | Max retries |
|--------|--------------|-------------|
| > -55 dBm (strong) | 15s | 3x deauth |
| > -70 dBm (medium) | 30s | 2x deauth |
| <= -70 dBm (weak) | 45s | 1x deauth |

Deauth escalation: starts with SOFT (64 packets), upgrades to HARD (512 packets) on retry.

After all targets are exhausted, Karma Attack activates automatically to capture any reconnecting devices.

---

### LED Panel Attack

Detects and exploits LED controllers using unauthenticated protocols.

| Protocol | Port | Used by |
|----------|------|---------|
| CL (Color Light) | UDP 9980 | Generic Chinese controllers |
| WLED | UDP 5005 | Open source WLED platform (DIY projects) |

---

## Advanced Capture

### PMKID Fisher

Captures the PMKID from the first handshake frame without requiring any client to be connected. The ESP32 initiates an Auth+Assoc sequence to trigger the AP to include the PMKID in its Association Response.

```
[ESP32] --(Authentication Request)-----------> [AP]
[AP] <--(Authentication Response)------------- [ESP32]
[ESP32] --(Association Request)--------------> [AP]
[AP] <--(Association Response + RSNIE/PMKID)-- [ESP32]
         ^------ PMKID extracted here! -------^
```

**PMKID derivation:**
```
PMK  = PBKDF2(HMAC-SHA1, password, SSID, 4096, 256)
PMKID = HMAC-SHA1-128(PMK, "PMK Name" || BSSID || STA_MAC)
```

**Cracking with hashcat:**
```bash
hashcat -m 22000 pmkids.txt /usr/share/wordlists/rockyou.txt
hashcat -m 22000 pmkids.txt /usr/share/wordlists/rockyou.txt --rules-file best64.rule
```

**Advantage over traditional handshake:** does not require any associated client, faster (1 attempt per AP), works without deauth.

---

### WPS Scanner

WPS (Wi-Fi Protected Setup) vulnerabilities:

| Vulnerability | Description |
|--------------|-------------|
| **PIN Brute Force** | 8-digit PIN verified in 2 halves = only ~11,000 combinations |
| **Pixie Dust** | Offline attack against weak WPS implementations |
| **PBC** | 120s window after pressing the button is exploitable |

The scanner detects APs with WPS active, WPS version (1.0 vulnerable / 2.0 partially fixed), and whether PIN is enabled.

---

## Bluetooth

### BLE Scanner

Scans the Bluetooth Low Energy spectrum and enumerates:
- Device name (GAP Advertisement Data)
- MAC address (public or random)
- RSSI and estimated distance
- Manufacturer (Company ID from advertisement)
- Announced GATT services (UUIDs)

### BLE Spam

Floods the air with fake BLE advertisements imitating popular devices, triggering pairing notifications on nearby devices:

| Type | Affected devices |
|------|-----------------|
| **Apple** | iPhone, iPad, MacBook (AirPods, AirTag, Keyboard notifications) |
| **Android** | Fast Pair notifications |
| **Samsung** | Quick Share |
| **Windows** | Swift Pair |

### BT Classic

Detects nearby Bluetooth Classic (BR/EDR) devices in discoverable mode. Shows name, device class and RSSI.

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Alt+S` | Start WiFi scan |
| `Alt+D` | Go to Deauth |
| `Alt+E` | Go to Evil Twin |
| `Alt+L` | Go to Logs |
| `Ctrl+K` | Clear console |

---

## Technical Architecture

### Project Structure

```
Wraith/
+-- src/
|   +-- main.cpp              # Setup, FreeRTOS tasks, main loop
|   +-- config.h              # Constants: AP credentials, ports, limits
|   +-- web/
|   |   +-- server.h          # ESPAsyncWebServer + all /api/* routes
|   +-- wifi/
|   |   +-- scanner.h         # 802.11 scan, OUI lookup (~70 vendors)
|   |   +-- sniffer.h         # Promiscuous mode, EAPOL/PMKID parser
|   |   +-- deauth.h          # Raw 802.11 injection - frame builder
|   |   +-- evil_twin.h       # AP clone + DHCP/DNS + captive portal
|   |   +-- beacon_flood.h    # Beacon spam
|   |   +-- karma.h           # MANA/Karma probe response
|   |   +-- pmkid_fisher.h    # PMKID via forced Auth+Assoc
|   |   +-- wps_scanner.h     # WPS detection
|   |   +-- target_intel.h    # Device fingerprinting
|   |   +-- hidden_scanner.h  # Hidden SSID revelation
|   |   +-- ap_clone.h        # Beacon capture + IE mirroring + CSA
|   |   +-- wifite_mode.h     # Auto-attack state machine
|   |   +-- radio2.h          # Second radio via UART2
|   +-- bluetooth/
|   |   +-- ble_scanner.h     # BLE scan + GATT enumeration
|   |   +-- ble_spam.h        # NimBLE advertising flood
|   |   +-- bt_classic.h      # BT Classic detection
|   +-- utils/
|       +-- logger.h          # Log system with levels (0-4)
|       +-- capture_store.h   # LittleFS persistence (.hc22000, creds.json)
|       +-- oui.h             # OUI table (40+ vendors)
|
+-- data/
|   +-- index.html            # SPA frontend (2800+ lines, zero external deps)
|
+-- platformio.ini            # PlatformIO config (4 targets)
+-- partitions_16mb.csv       # Partition table for N16R8 (5MB app + 6MB FS)
+-- ATTACKS.md                # Deep technical documentation for all attacks
+-- WIRING.md                 # Hardware wiring guide
```

### Task Model (FreeRTOS)

The ESP32 uses FreeRTOS to run tasks in parallel across both cores:

```
Core 0 (Protocol CPU):             Core 1 (App CPU):
- attackTask (priority 5)          - bleTask (priority 4)
  - deauth loop                      - BLE scanner/spam
  - beacon flood
  - karma probe response
  - pmkid fisher
  - wifite state machine
  - nuclear coordinator

- snifferTask (priority 4)
  - promiscuous mode
  - frame parser
  - EAPOL/PMKID extractor
```

ESPAsyncWebServer operates non-blocking via interrupt callbacks, serving the web interface without interfering with running attacks.

### REST API

All endpoints follow the pattern `/api/<module>/<action>`:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | General status (active attacks, counters, heap) |
| `/api/login` | POST | Authenticate with password |
| `/api/logout` | POST | End session |
| `/api/scan` | GET | Start WiFi scan |
| `/api/scan/results` | GET | Last scan results (JSON) |
| `/api/sniffer/start` | POST | Start sniffer |
| `/api/sniffer/stop` | POST | Stop sniffer |
| `/api/sniffer/handshakes` | GET | List of captured handshakes |
| `/api/sniffer/pmkids` | GET | List of captured PMKIDs |
| `/api/sniffer/states` | GET | State of each AP+client pair |
| `/api/deauth/start` | POST | Start deauth (params: bssid, channel, client, mode) |
| `/api/deauth/stop` | POST | Stop deauth |
| `/api/eviltwin/start` | POST | Start Evil Twin (params: ssid, bssid, channel) |
| `/api/eviltwin/stop` | POST | Stop Evil Twin |
| `/api/eviltwin/creds` | GET | Credentials captured by portal |
| `/api/wifite/start` | POST | Start Auto-Attack |
| `/api/wifite/stop` | POST | Stop Auto-Attack |
| `/api/wifite/status` | GET | Auto-Attack status and progress |
| `/api/nuclear/start` | POST | Start Nuclear Attack |
| `/api/nuclear/stop` | POST | Stop Nuclear Attack |
| `/api/ble/scan` | POST | Start BLE scan |
| `/api/blespam/start` | POST | Start BLE spam |
| `/api/export/handshakes` | GET | Download handshakes.txt |
| `/api/export/pmkids` | GET | Download pmkids.txt (hashcat -m 22000 format) |
| `/api/export/pcap` | GET | Download capture.pcap (Wireshark format) |
| `/api/reboot` | POST | Reboot the ESP32 |

### Capture File Format (LittleFS)

Captures are persisted to LittleFS and survive reboots:

```
/captures/
  hs_BSSID.hc22000      # WPA2 handshakes (hashcat format)
  pmkid_BSSID.hc22000   # PMKIDs (hashcat -m 22000)
  creds.json            # Evil Twin credentials
  log.jsonl             # Event log (JSONL)
```

**hc22000 format:**
```
WPA*02*<MIC>*<AP_MAC>*<STA_MAC>*<SSID_hex>*<ANonce>*<SNonce>*<EAPOL>
```

A handshake with M1+M2 is sufficient for cracking with `hashcat -m 22000` - no need for the full 4-way handshake.

### Frontend (SPA)

The web interface is a single HTML file of 2800+ lines with no external dependencies. All state lives in JavaScript variables on the client.

```javascript
// Status polling every 1s
setInterval(pollStatus, 1000);

// Permanent WebSocket connection at ws://192.168.4.1/ws
// Auto-reconnect with backoff on drop

// Auto-login via localStorage token
// Token expires in 30 minutes
```

---

## Legal Notice

This tool is intended **exclusively** for educational purposes and security testing on networks you own or have explicit written authorization to test.

Unauthorized use on third-party networks violates:
- **Brazil:** Law 12.737/2012 (Lei Carolina Dieckmann) - computer crimes
- **USA:** Computer Fraud and Abuse Act (CFAA)
- **EU:** Directive 2013/40/EU on attacks against information systems

The authors are not responsible for any misuse of this tool. Use responsibly.

---

## License

MIT - see [LICENSE](LICENSE)

---

*Wraith v5.0 - ESP32 Wireless Security Research Platform*
