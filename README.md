# Aviation Ground Control Communication System

CSCN74000 Group 4 sprint implementation in C++17 using a CLI aircraft client and a Dear ImGui ground-control server.

## Build

```bash
cmake -B build
cmake --build build
```

## Run

```bash
./build/ground_server
./build/aircraft_client
```

Optional server port:

```bash
./build/ground_server 5050
```

Optional client arguments:

```bash
./build/aircraft_client 127.0.0.1 5000
```

To connect to a custom server port, pass the same port to both programs:

```bash
./build/ground_server 5050
./build/aircraft_client 127.0.0.1 5050
```

## Repository Layout

- `common/`: shared packet definitions, socket helpers, and timestamped logging.
- `client/`: CLI aircraft client with handshake, telemetry, disconnect, and weather-map download flow.
- `server/`: socket/state-machine loop, weather-map generation, and Dear ImGui dashboard rendering.
- `third_party/imgui/`: bundled Dear ImGui sources and OpenGL/GLFW backends.
- `assets/`: generated weather-map bitmap location.

Logs are written to the current working directory with UTC timestamped filenames.
