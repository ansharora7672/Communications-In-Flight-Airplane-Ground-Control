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

Optional client arguments:

```bash
./build/aircraft_client 127.0.0.1 5000
```

Logs are written to the current working directory with UTC timestamped filenames.
