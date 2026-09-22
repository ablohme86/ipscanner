# Network IP Scanner (Qt5 / C++)

A high-performance, modern network IP scanner built with **Qt 5** and **C++17**. Designed with a clean, responsive layout, dark/light theme support, and asynchronous multi-threaded scanning to rapidly scan subnets and discover active devices.

![Network IP Scanner Screenshot](screenshot_perfect.png)

---

## Key Features

- **Blazing-Fast Subnet Scanning**:
  - Multi-threaded worker pool (`QThreadPool` / `QRunnable`) capable of scanning entire `/24` subnets (254 hosts) in just a few seconds.
  - Configurable worker threads (1 to 128) and adjustable ICMP ping timeout (100 ms to 3000 ms).
  - Start, Pause, Resume, and Stop scan controls.

- **Comprehensive Device Identification**:
  - **IP Address**: Formatted IPv4 with natural numeric sorting (`10.10.10.2` sorts before `10.10.10.10`).
  - **Hostname**: Resolved asynchronously via reverse DNS (PTR), local mDNS (`.local`), and NetBIOS Name Service queries (UDP 137) for Windows/Samba workstations.
  - **Latency / Response Time**: Precision round-trip time in milliseconds with color-coded speed indicators (< 15 ms emerald green, < 60 ms warning amber, > 60 ms red).
  - **MAC Address & Manufacturer**: Automatic kernel ARP cache lookup (`/proc/net/arp`) and local interface hardware address resolution.
  - **Offline Vendor Identification**: Bundled offline IEEE OUI database identifying over 39,000 hardware manufacturers (Raspberry Pi, Apple, Philips, TP-Link, Intel, etc.) without requiring external internet access.
  - **Service & Port Detection**: Quick background probe for common services (HTTP, HTTPS, SSH, SMB, DNS, RDP, MySQL) during host discovery.
  - **User Notes**: Double-click the Comments column to annotate devices.

- **Built-in Power Tools**:
  - **Deep Port Scanner**: Scan any target IP across common top services, standard well-known ports (1-1024), database ports, web services, or custom port ranges with live progress.
  - **Wake-on-LAN (WOL)**: Send magic broadcast packets (UDP port 9/7) to remotely power on network equipment.
  - **Export Results**: Export discovered hosts to **CSV**, **JSON**, or aligned **Text** tables.
  - **Quick Actions**: One-click HTTP/HTTPS browser launcher, SSH connection launcher, interactive Ping, and clipboard helpers.
  - **Theme Toggle**: Switch between sleek Dark Mode and modern Light Mode.

---

## Architecture & Design

```
ipscanner2/
├── CMakeLists.txt             # Modern CMake build configuration (C++17, Qt5)
├── resources/
│   ├── resources.qrc          # Qt Resource bundle
│   ├── styles/
│   │   ├── dark.qss           # Modern dark slate theme stylesheet
│   │   └── light.qss          # Modern light theme stylesheet
│   └── data/
│       └── oui_fallback.txt   # Offline IEEE OUI MAC vendor database (39,850+ vendors)
├── src/
│   ├── main.cpp               # Application entry point, CLI parser & High-DPI setup
│   ├── core/
│   │   ├── HostItem.h/.cpp    # Host data entity & formatting
│   │   ├── ArpReader.h/.cpp   # Linux procfs ARP parser & local NIC mapper
│   │   └── MacVendorLookup.h/.cpp # OUI prefix lookup engine
│   ├── scanner/
│   │   ├── NetworkScanner.h/.cpp  # Subnet scan coordinator & batch dispatcher
│   │   ├── ScanWorker.h/.cpp      # Per-host asynchronous probe (Ping, NetBIOS, Ports)
│   │   ├── PortScanner.h/.cpp     # Multi-threaded TCP port scanner & service mapper
│   │   └── WakeOnLan.h/.cpp       # Magic packet UDP broadcast generator
│   ├── models/
│   │   ├── HostTableModel.h/.cpp  # QAbstractTableModel with natural sorting & roles
│   │   └── HostSortFilterProxyModel.h/.cpp # Real-time multi-column search & alive filter
│   └── ui/
│       ├── MainWindow.h/.cpp      # Main application window, control card & inspector
│       ├── PortScanDialog.h/.cpp  # Dedicated deep port scanner dialog
│       └── WolDialog.h/.cpp       # Dedicated Wake-on-LAN dialog
└── tests/
    └── test_scanner.cpp       # Unit test suite verifying core components
```

---

## Building and Running

### Prerequisites (Arch Linux / Ubuntu / Debian / Fedora)

- **C++ Compiler**: GCC 9+ or Clang with C++17 support
- **CMake**: 3.16 or newer
- **Qt5 Packages**: `qt5-base` (or `qtbase5-dev`), `Qt5Widgets`, `Qt5Network`, `Qt5Concurrent`

#### Install dependencies on Arch Linux:
```bash
sudo pacman -S base-devel cmake qt5-base
```

#### Install dependencies on Ubuntu / Debian:
```bash
sudo apt update
sudo apt install build-essential cmake qtbase5-dev libqt5network5
```

### Build Instructions

```bash
cd /home/alexander/Prosjekter/ipscanner2

# Configure CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile application (using all available cores)
cmake --build build -j$(nproc)
```

### Running the Application

```bash
# Launch from an interactive terminal (automatically launches Terminal UI)
./build/ipscanner

# Explicitly launch Terminal UI (TUI)
./build/ipscanner --tui

# Launch with custom palette theme (tokyo, dracula, gruvbox, green, cyan, amber, dark, light)
./build/ipscanner --tui --theme dracula

# Launch with automatic scan on launch
./build/ipscanner --auto-scan

# Force Graphical User Interface (GUI) from terminal
./build/ipscanner --gui

# Capture a screenshot from GUI after 5 seconds and exit
./build/ipscanner --gui --auto-scan --screenshot scan_result.png --exit-after 5
```

### Running the Unit Tests

```bash
cmake --build build --target test_scanner -j$(nproc)
./build/test_scanner
```

---

## Terminal UI (TUI) Keyboard Shortcuts

- `Space` / `S`: Start / Stop subnet reconnaissance
- `P`: Pause / Resume active scan
- `C`: Clear / Flush discovered targets matrix
- `R` / `I`: Configure network interface, IP range, threads, timeout
- `/` or `F`: Search / filter nodes in real-time
- `A`: Toggle alive-only / all nodes filter
- `D`: Deep Port Reconnaissance on selected node
- `W`: Wake-on-LAN magic packet transmission
- `G`: Interactive ICMP Ping probe on selected node
- `Y`: Copy selected node intel to system clipboard
- `E`: Export targets matrix to CSV, JSON, or TXT
- `T` / `Shift+T`: Cycle / Select retro cyber color theme
- `Up` / `Down`: Navigate host list (also `j`/`k`, `PageUp`/`PageDown`, `Home`/`End`)
- `?` / `H`: Help modal
- `Q` / `Ctrl+C`: Quit application cleanly

---

## GUI Keyboard Shortcuts

- `Ctrl+E`: Export scan results to CSV
- `Ctrl+P`: Open Deep Port Scanner dialog
- `Ctrl+W`: Open Wake-on-LAN dialog
- `Ctrl+T`: Toggle Dark / Light Theme
- `F5`: Refresh Network Interfaces
- `Ctrl+Q`: Exit Application
