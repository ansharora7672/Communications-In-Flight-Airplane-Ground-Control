# CSCN74000 Group 4 — Agent Build Spec
## Aviation Ground Control Communication System
**Sprint 2 Implementation Guide — For AI Coding Agent**

---

## 0. Context & Constraints

- Language: **C++17**
- Build system: **CMake 3.20+** (single CMakeLists.txt, no Visual Studio solution files)
- Cross-platform: Windows (MSVC or MinGW), macOS (clang), Linux (gcc) — no platform-specific APIs in application code
- Socket abstraction: one thin `socket_utils.h` header that wraps Winsock2 on Windows and POSIX sockets on Mac/Linux using `#ifdef _WIN32`
- GUI: **Dear ImGui** (source files bundled in repo) + **GLFW** (via CMake FetchContent) for the **server only**
- Client: **CLI only** (structured console output)
- Dependencies fetched automatically at build time — user only needs CMake and a C++ compiler
- No Qt, no ncurses, no external package managers required

---

## 1. Repository Structure

```
/
├── CMakeLists.txt
├── README.md
├── common/
│   ├── socket_utils.h        # Cross-platform socket abstraction
│   ├── packet.h              # All packet structs and types
│   └── logger.h / logger.cpp # Shared logging logic
├── client/
│   └── main.cpp              # Aircraft client (CLI)
├── server/
│   ├── main.cpp              # Ground control server (ImGui dashboard)
│   ├── state_machine.h/.cpp  # State machine logic
│   └── imgui_dashboard.h/.cpp# ImGui rendering logic
├── assets/
│   └── weather_map.bmp       # Generated on first server run if not present
└── third_party/
    └── imgui/                # ImGui source files copied directly into repo
        ├── imgui.h / imgui.cpp
        ├── imgui_impl_glfw.h/.cpp
        ├── imgui_impl_opengl3.h/.cpp
        └── (other imgui source files)
```

---

## 2. CMake Build Instructions

### CMakeLists.txt outline (agent should implement this fully):

```cmake
cmake_minimum_required(VERSION 3.20)
project(AviationComms CXX)
set(CMAKE_CXX_STANDARD 17)

# Fetch GLFW for ImGui backend
include(FetchContent)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)

# ImGui sources (bundled in repo under third_party/imgui)
set(IMGUI_SOURCES
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/imgui_impl_glfw.cpp
  third_party/imgui/imgui_impl_opengl3.cpp
)

# Common library
add_library(common STATIC common/logger.cpp)
target_include_directories(common PUBLIC common/)

# Client executable
add_executable(aircraft_client client/main.cpp)
target_link_libraries(aircraft_client common)
if(WIN32)
  target_link_libraries(aircraft_client ws2_32)
endif()

# Server executable
add_executable(ground_server
  server/main.cpp
  server/state_machine.cpp
  server/imgui_dashboard.cpp
  ${IMGUI_SOURCES}
)
target_include_directories(ground_server PRIVATE third_party/imgui server/)
target_link_libraries(ground_server common glfw OpenGL::GL)
if(WIN32)
  target_link_libraries(ground_server ws2_32)
endif()
```

### How to build (works on all platforms):
```bash
cmake -B build
cmake --build build
# Binaries at: build/aircraft_client and build/ground_server
```

---

## 3. Cross-Platform Socket Abstraction

**File:** `common/socket_utils.h`

This is the ONLY place platform-specific code lives. Everywhere else in the codebase uses the types and functions defined here.

```cpp
#pragma once

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using SocketHandle = SOCKET;
  #define INVALID_SOCK INVALID_SOCKET
  #define SOCK_ERR SOCKET_ERROR
  inline void initSockets() {
      WSADATA wsa;
      WSAStartup(MAKEWORD(2,2), &wsa);
  }
  inline void cleanupSockets() { WSACleanup(); }
  inline void closeSocket(SocketHandle s) { closesocket(s); }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  using SocketHandle = int;
  #define INVALID_SOCK -1
  #define SOCK_ERR -1
  inline void initSockets() {}
  inline void cleanupSockets() {}
  inline void closeSocket(SocketHandle s) { close(s); }
#endif
```

---

## 4. Packet Definition

**File:** `common/packet.h`

This satisfies: REQ-SYS-010, REQ-PKT-010 through REQ-PKT-070, REQ-SYS-030

```cpp
#pragma once
#include <cstdint>
#include <cstdlib>

// Packet type identifiers
enum class PacketType : uint8_t {
    HANDSHAKE_REQUEST  = 0x01,
    HANDSHAKE_ACK      = 0x02,
    HANDSHAKE_FAIL     = 0x03,
    TELEMETRY          = 0x04,
    LARGE_FILE         = 0x05,
    DISCONNECT         = 0x06
};

// Fixed-size header — sent before every packet
#pragma pack(push, 1)
struct PacketHeader {
    PacketType  packet_type;
    char        aircraft_id[16];   // e.g. "AC-001"
    uint32_t    sequence_number;
    uint32_t    payload_size;      // bytes to follow
    uint32_t    checksum;          // simple sum of payload bytes
};
#pragma pack(pop)

// Telemetry payload — structured flight data
struct TelemetryPayload {
    float   latitude;
    float   longitude;
    float   altitude;    // feet
    float   speed;       // knots
    float   heading;     // degrees
};

// Large file payload — dynamically allocated raw buffer
// THIS is the dynamically allocated element required by REQ-SYS-030
struct LargeFilePayload {
    uint8_t* data;       // malloc'd by sender, freed after send/receive
    uint32_t size;

    LargeFilePayload() : data(nullptr), size(0) {}
    ~LargeFilePayload() { free(data); data = nullptr; }

    // Non-copyable — must be moved or used in place
    LargeFilePayload(const LargeFilePayload&) = delete;
    LargeFilePayload& operator=(const LargeFilePayload&) = delete;
};

// Checksum utility
inline uint32_t computeChecksum(const uint8_t* data, uint32_t size) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; i++) sum += data[i];
    return sum;
}
```

---

## 5. State Machine

**Files:** `server/state_machine.h` + `server/state_machine.cpp`

Satisfies: REQ-STM-010, REQ-STM-020, REQ-STM-030, REQ-STM-040, REQ-SYS-060

### States:
```
STATE_DISCONNECTED
STATE_HANDSHAKE_PENDING
STATE_CONNECTED
STATE_TELEMETRY
STATE_LARGE_FILE_TRANSFER
STATE_FAULT
```

### Allowed transitions (enforce strictly — reject anything else and log it):
```
DISCONNECTED        → HANDSHAKE_PENDING      (client connects)
HANDSHAKE_PENDING   → CONNECTED              (handshake ACK sent)
HANDSHAKE_PENDING   → DISCONNECTED           (handshake failed)
CONNECTED           → TELEMETRY              (telemetry packets begin)
CONNECTED           → LARGE_FILE_TRANSFER    (file transfer initiated)
TELEMETRY           → CONNECTED              (telemetry paused/stopped)
TELEMETRY           → FAULT                  (error during telemetry)
LARGE_FILE_TRANSFER → CONNECTED              (transfer complete)
LARGE_FILE_TRANSFER → FAULT                  (transfer failed/incomplete)
ANY active state    → FAULT                  (network crash, timeout, bad checksum)
FAULT               → DISCONNECTED           (auto-reset after logging)
CONNECTED           → DISCONNECTED           (client disconnects cleanly)
```

### StateMachine class interface:
```cpp
class StateMachine {
public:
    enum class State {
        DISCONNECTED, HANDSHAKE_PENDING, CONNECTED,
        TELEMETRY, LARGE_FILE_TRANSFER, FAULT
    };

    State getState() const;
    bool  transition(State next);   // returns false and logs if transition illegal
    std::string stateToString(State s) const;

private:
    State current = State::DISCONNECTED;
    bool  isAllowed(State from, State to) const;
};
```

---

## 6. Logger

**Files:** `common/logger.h` + `common/logger.cpp`

Satisfies: REQ-SYS-050, REQ-LOG-010, REQ-LOG-030, REQ-LOG-040, REQ-LOG-050, REQ-LOG-060

### Log file naming:
```
YYYYMMDD_HHMMSS_aircraft_comms.log   ← client
YYYYMMDD_HHMMSS_groundctrl_comms.log ← server
YYYYMMDD_HHMMSS_blackbox.log         ← fault events only (server)
```
New file created on each application start. Never overwritten.

### Log entry format (plain text, one line per entry):
```
[2025-03-17 14:30:22 UTC] [TX] [TELEMETRY] Aircraft=AC-001 Seq=0042 PayloadSize=20
[2025-03-17 14:30:22 UTC] [RX] [HANDSHAKE_REQUEST] Aircraft=AC-001 Seq=0001 PayloadSize=0
```

### Black box entry format (fault events):
```
[2025-03-17 14:31:05 UTC] [FAULT] Cause=ChecksumMismatch State=STATE_TELEMETRY Seq=0089
```

### Logger class interface:
```cpp
class Logger {
public:
    Logger(const std::string& role);  // "aircraft" or "groundctrl"
    void logPacket(const std::string& direction, const PacketHeader& hdr);
    void logFault(const std::string& cause, const std::string& state, uint32_t seq);
    void logInfo(const std::string& message);

private:
    std::ofstream commsLog;
    std::ofstream blackBoxLog;
};
```

---

## 7. Aircraft Client (CLI)

**File:** `client/main.cpp`

Satisfies: REQ-CLT-010 through REQ-CLT-070, REQ-SYS-040, REQ-SYS-080, REQ-COM-010/020/030

### Behaviour flow:
1. Start → print ASCII header showing "AIRCRAFT CLIENT — AC-001"
2. Prompt user: `[1] Connect to Ground Control  [Q] Quit`
3. On connect: initiate TCP connection to `localhost:5000` (configurable via arg)
4. Send `HANDSHAKE_REQUEST` packet
5. Wait for `HANDSHAKE_ACK` — if fail, print error, return to menu
6. On success: print `[CONNECTED] Handshake verified. Channel secure.`
7. Begin telemetry loop: every 1 second, generate simulated telemetry (slightly randomised values), pack into `TelemetryPayload`, send, print to console:
   ```
   [TX] SEQ=0042 | LAT=43.4643 LON=-80.5204 ALT=35000ft SPD=480kts HDG=270°
   ```
8. Menu while connected: `[W] Request Weather Map  [D] Disconnect`
9. On `W`: send `LARGE_FILE` request trigger, receive BMP from server, save as `received_weather_map.bmp`, print progress:
   ```
   [RX] Receiving weather map... 1024/1048576 bytes
   [RX] Weather map saved: received_weather_map.bmp (1.0 MB)
   ```
10. On `D` or server disconnect: send `DISCONNECT` packet, close socket, return to menu
11. On any socket error: print `[FAULT] Connection lost. Returning to main menu.`, log to file

### Visual indicators (CLI — satisfies REQ-CLT-070):
- Connection status line at top of each menu render: `Status: CONNECTED | State: TELEMETRY | Aircraft: AC-001`
- Use ANSI colour codes: green for connected, red for fault, yellow for pending
  - Wrap in `#ifdef _WIN32` check — on Windows use `SetConsoleTextAttribute` instead, or just skip colour if not supported

---

## 8. Ground Control Server (ImGui Dashboard)

**Files:** `server/main.cpp`, `server/imgui_dashboard.h/.cpp`

Satisfies: REQ-SVR-010 through REQ-SVR-080, REQ-SYS-040, REQ-SYS-060, REQ-SYS-070, REQ-SYS-080

### Server startup behaviour:
1. On first run: check if `assets/weather_map.bmp` exists. If not, **generate it** (see Section 9)
2. Bind TCP socket on port 5000, start listening
3. Open GLFW window titled `"Ground Control — CSCN74000"`
4. Enter ImGui render loop — server runs in a background thread handling socket I/O

### ImGui Dashboard Layout:
```
┌─────────────────────────────────────────────────────────┐
│  GROUND CONTROL OPERATOR DASHBOARD                       │
├──────────────────┬──────────────────────────────────────┤
│ CONNECTION       │  LIVE TELEMETRY                      │
│ Status: CONNECTED│  Aircraft ID : AC-001                │
│ State:  TELEMETRY│  Latitude    : 43.4643               │
│ Aircraft: AC-001 │  Longitude   : -80.5204              │
│                  │  Altitude    : 35,000 ft             │
│ [Send Weather Map│  Speed       : 480 kts               │
│  to Aircraft]    │  Heading     : 270°                  │
│ [Disconnect]     │                                      │
├──────────────────┴──────────────────────────────────────┤
│ COMMUNICATION LOG (last 20 entries)                      │
│ [14:30:22] RX TELEMETRY Seq=42 AC-001                   │
│ [14:30:21] RX TELEMETRY Seq=41 AC-001                   │
│ ...                                                      │
└─────────────────────────────────────────────────────────┘
```

### Colour-coded state indicator (satisfies REQ-SVR-080):
- DISCONNECTED: grey
- HANDSHAKE_PENDING: yellow
- CONNECTED / TELEMETRY: green
- LARGE_FILE_TRANSFER: blue
- FAULT: red

### Server socket logic (runs in std::thread):
1. Accept client connection → transition to `HANDSHAKE_PENDING`
2. Receive `HANDSHAKE_REQUEST` → validate aircraft ID → send `HANDSHAKE_ACK` → transition to `CONNECTED`
3. In `CONNECTED`/`TELEMETRY`: receive telemetry packets in loop, update dashboard data
4. On "Send Weather Map" button click: transition to `LARGE_FILE_TRANSFER`, read BMP, send in chunks, transition back to `CONNECTED`
5. On any recv error: transition to `FAULT`, log to black box, transition to `DISCONNECTED`
6. State machine enforces all transitions — any illegal transition logs and is rejected

---

## 9. BMP File Generator

**Implemented inside `server/main.cpp` as a standalone function, called once on startup if file missing.**

Satisfies: REQ-SYS-070, REQ-SVR-060, REQ-CLT-040, REQ-PKT-050

### What it generates:
A 1024×1024 pixel 24-bit BMP (= exactly 3,145,782 bytes ≈ 3MB) filled with a blue-to-green gradient simulating a weather radar image.

```cpp
void generateWeatherMap(const std::string& path) {
    // BMP file header + DIB header (54 bytes total)
    // Then raw RGB pixel data: 1024 * 1024 * 3 bytes
    // Gradient: R=0, G=row*255/1024, B=col*255/1024
    // This produces a teal gradient that looks like a radar map
}
```

No external image libraries needed. Pure C++ file I/O.

---

## 10. Large File Transfer Protocol

Satisfies: REQ-SYS-070, REQ-PKT-050, US013

### Send side (server):
1. Read entire BMP into `LargeFilePayload.data` (malloc)
2. Set header: `packet_type = LARGE_FILE`, `payload_size = file_size`
3. Send header first (fixed size, one `send()` call)
4. Send payload in **4096-byte chunks** in a loop
5. Free `LargeFilePayload.data` after send
6. Log the transfer (REQ-LOG-030)

### Receive side (client):
1. Receive header
2. `malloc(header.payload_size)` for receive buffer
3. Loop `recv()` until all `payload_size` bytes received
4. Verify checksum
5. Write to `received_weather_map.bmp`
6. Free buffer
7. Log the transfer

---

## 11. Handshake Protocol

Satisfies: REQ-SYS-080, REQ-COM-020, REQ-CLT-020, REQ-SVR-020

```
CLIENT                          SERVER
  |                               |
  |--- HANDSHAKE_REQUEST -------->|  (header only, no payload)
  |                               |  Server validates aircraft_id format
  |<-- HANDSHAKE_ACK -------------|  (or HANDSHAKE_FAIL)
  |                               |
  Both transition to STATE_CONNECTED
  (or back to STATE_DISCONNECTED on fail)
```

Aircraft ID validation: must be non-empty and match pattern `AC-[0-9]{3}`. Reject anything else with `HANDSHAKE_FAIL`.

---

## 12. Simulated Telemetry Values

Client generates these on a timer (every 1 second) with small random drift to simulate real flight:

```cpp
struct FlightSim {
    float lat     = 43.4643f;   // Waterloo, ON area
    float lon     = -80.5204f;
    float alt     = 35000.0f;   // cruising altitude
    float speed   = 480.0f;     // knots
    float heading = 270.0f;     // westbound

    void tick() {
        lat   += ((rand() % 10) - 5) * 0.0001f;
        lon   += ((rand() % 10) - 5) * 0.0001f;
        alt   += ((rand() % 10) - 5) * 1.0f;
        speed += ((rand() % 6)  - 3) * 0.5f;
    }
};
```

---

## 13. Requirement Traceability Checklist

| Requirement | Satisfied By |
|---|---|
| REQ-SYS-001 | Two executables: `aircraft_client` + `ground_server` |
| REQ-SYS-010 | `PacketHeader` + typed payload structs in `packet.h` |
| REQ-SYS-020 | C++17, `#pragma pack` for struct serialization |
| REQ-SYS-030 | `LargeFilePayload.data` is `malloc`'d dynamically |
| REQ-SYS-040 | Client=CLI, Server=ImGui dashboard |
| REQ-SYS-050 | `Logger` class writes all TX/RX to timestamped log files |
| REQ-SYS-060 | `StateMachine` class with 6 states and enforced transitions |
| REQ-SYS-070 | BMP generator + chunked file transfer from server to client |
| REQ-SYS-080 | Handshake exchange before any data flows |
| REQ-CLT-010 | CLI with structured output, distinct from server ImGui |
| REQ-CLT-020 | Client won't send telemetry until `HANDSHAKE_ACK` received |
| REQ-CLT-030 | Telemetry loop runs every 1s while in `STATE_TELEMETRY` |
| REQ-CLT-040 | Client receives BMP, saves locally, logs transfer |
| REQ-CLT-050 | Console prints every TX packet with values |
| REQ-CLT-060 | `LargeFilePayload` destructor + explicit frees on fault/exit |
| REQ-CLT-070 | ANSI colour status line in CLI |
| REQ-SVR-010 | ImGui dashboard, distinct from client CLI |
| REQ-SVR-020 | Server rejects all packets until handshake complete |
| REQ-SVR-030 | Server recv loop parses telemetry structs, updates dashboard |
| REQ-SVR-040 | ImGui panel shows aircraft ID, state, live telemetry values |
| REQ-SVR-050 | State machine blocks command dispatch before `STATE_CONNECTED` |
| REQ-SVR-060 | Server reads BMP from disk, sends via `LARGE_FILE` packet |
| REQ-SVR-070 | On recv error: `STATE_FAULT` → log → free → `STATE_DISCONNECTED` |
| REQ-SVR-080 | ImGui state indicator goes red + alert if telemetry stops |
| REQ-STM-010 | 6-state machine controls all socket logic on server |
| REQ-STM-020 | `isAllowed()` rejects and logs illegal transitions |
| REQ-STM-030 | Any recv error from any active state → `STATE_FAULT` |
| REQ-STM-040 | After logging in `STATE_FAULT` → auto-reset to `STATE_DISCONNECTED` |
| REQ-PKT-010 | Fixed `PacketHeader` + payload section |
| REQ-PKT-020 | Header has type, aircraft ID, sequence number, payload size |
| REQ-PKT-030 | Payload buffer is `malloc`'d using `payload_size` from header |
| REQ-PKT-040 | `TelemetryPayload` struct with lat/lon/alt/speed/heading |
| REQ-PKT-050 | `LargeFilePayload` buffer supports ≥1MB |
| REQ-PKT-060 | Receiver reads exactly `header.payload_size` bytes |
| REQ-PKT-070 | `checksum` field in header, computed over payload bytes |
| REQ-COM-010 | TCP socket on port 5000, connected before any packets |
| REQ-COM-020 | Both sides enforce handshake before leaving HANDSHAKE_PENDING |
| REQ-COM-030 | TCP/IP used throughout (not UDP) |
| REQ-COM-040 | `recv()` failure in active state → `STATE_FAULT` + log |
| REQ-COM-050 | After fault reset, new connection can be made without restart |
| REQ-LOG-010 | Both client and server instantiate `Logger` on startup |
| REQ-LOG-030 | Every log entry has UTC timestamp, direction, type, ID, seq, size |
| REQ-LOG-040 | Plain text `.log` files, human readable |
| REQ-LOG-050 | Fault events written to separate `blackbox.log` before state transition |
| REQ-LOG-060 | Log filenames include UTC timestamp prefix |

---

## 14. What the Agent Should NOT Do

- Do not use Qt, wxWidgets, or any other GUI framework besides ImGui + GLFW
- Do not use `std::thread` unsafe patterns — protect shared telemetry data between socket thread and render thread with a `std::mutex`
- Do not hardcode Windows-only paths (`C:\`, backslashes) — use relative paths only
- Do not use `system()` calls
- Do not use `gets()`, `scanf` with unbounded strings, or other unsafe C I/O
- Do not implement a UDP fallback — TCP only per REQ-COM-030
- Do not add unit tests — that is a separate phase (Week 13)

---

## 15. How to Run

```bash
# Terminal 1 — start server first
./build/ground_server

# Terminal 2 — start client
./build/aircraft_client

# Or with custom server IP/port:
./build/aircraft_client 192.168.1.10 5000
```

Server opens ImGui window and listens. Client prompts for connection. Logs written to current working directory.

---
 **Source control, traceability, and reversibility**

The agent must keep all work under source control at all times. Before making changes, ensure the working directory is in a valid git repository. Make small, logically grouped commits with clear commit messages describing what changed and why. After each commit, append an entry to a changelog file (for example, `AGENT_CHANGELOG.md`) that includes: timestamp, commit hash, files changed, and a short summary of the action performed. Do not leave significant uncommitted changes unless explicitly requested. Prefer reversible changes, and when modifying or removing existing behavior, preserve enough history in commits and logs so the user can easily inspect, audit, and roll back any step.