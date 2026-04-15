# Systream Demo — S-Core COM Getting-Started Example

> **Who this is for:** Engineers who are new to the Eclipse S-Core COM (LoLa)
> middleware and want a minimal, fully-annotated working example to run on a
> Linux host before integrating LoLa into their own application.

---

## What This Demo Does

Two processes exchange a simple `SensorData` struct (temperature + pressure +
sequence number) over **zero-copy shared memory** using the S-Core COM LoLa
binding.

```
┌────────────────────────────────┐        ┌──────────────────────────────────┐
│         demo_server            │        │          demo_client              │
│  (Skeleton / Publisher)        │        │    (Proxy / Subscriber)           │
│                                │        │                                   │
│  SensorSkeleton::Create()      │        │  SensorProxy::FindService()       │
│  skeleton.OfferService()       │        │  SensorProxy::Create()            │
│       │                        │        │  proxy.sensor_reading_            │
│       ▼  every 500 ms          │  SHM   │      .SetReceiveHandler(cb)       │
│  sensor_reading_.Allocate()    │───────▶│      .Subscribe(5)                │
│  *sample = {temp, pres, …}     │        │      .GetNewSamples(process_fn)   │
│  sensor_reading_.Send(sample)  │        │      .Unsubscribe()               │
│       │                        │        │                                   │
│  StopOfferService()            │        │  Prints latency for each sample   │
└────────────────────────────────┘        └──────────────────────────────────┘
```

**Key concepts demonstrated:**
- `Allocate()` + `Send()` — zero-copy publish to shared memory
- `SetReceiveHandler()` — event-driven receive callback
- `FindService()` — service discovery across processes
- `Subscribe()` / `GetNewSamples()` / `Unsubscribe()` — full proxy lifecycle
- `mw_com_config.json` structure — all fields explained inline

---

## File Locations

```
communication/
└── score/mw/com/example/systream_demo/
    ├── BUILD                        ← Bazel build targets
    ├── README.md                    ← This file
    ├── demo_types.h                 ← SensorData struct + typed Skeleton/Proxy
    ├── demo_server.cpp              ← Skeleton (publisher) — fully annotated
    ├── demo_client.cpp              ← Proxy (subscriber) — fully annotated
    └── etc/
        ├── mw_com_config.json       ← Runtime service configuration
        └── logging.json             ← Log level configuration
```

---

## Prerequisites

| Requirement | Version | Check |
|---|---|---|
| Ubuntu | 24.04 LTS | `lsb_release -a` |
| Bazel (via Bazelisk) | 8.3.0 | `bazel version` |
| Python | 3.10+ | `python3 --version` |
| POSIX shared memory | any | `ls /dev/shm` |

**Install Bazelisk** (manages Bazel version automatically):
```bash
curl -Lo /usr/local/bin/bazel \
  https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64
chmod +x /usr/local/bin/bazel
bazel version   # prints Bazel 8.3.0
```

---

## Step-by-Step: Build and Run

### Step 0 — Clone the repository
```bash
git clone https://github.com/eclipse-score/communication.git
cd communication
```

### Step 1 — One-time Linux sandbox setup
This script must be run once per machine (or after a reboot on some systems)
to allow Bazel's build sandbox to use user namespaces.

```bash
bash actions/unblock_user_namespace_for_linux_sandbox/action_callable.sh
# Enter sudo password when prompted
# Expected last line: "Success: '/usr/bin/linux-sandbox "/bin/true"' succeeded."
```

### Step 2 — Build both binaries
```bash
# Build the server (Skeleton)
bazel build //score/mw/com/example/systream_demo:demo_server

# Build the client (Proxy)
bazel build //score/mw/com/example/systream_demo:demo_client
```

Or build both at once:
```bash
bazel build //score/mw/com/example/systream_demo/...
```

Output binaries are placed at:
```
bazel-bin/score/mw/com/example/systream_demo/demo_server
bazel-bin/score/mw/com/example/systream_demo/demo_client
```

### Step 3 — Run the server (Terminal 1)
The server **must start first** — it creates the shared memory and registers
with the service discovery directory before the client can find it.

```bash
./bazel-bin/score/mw/com/example/systream_demo/demo_server \
  --service_instance_manifest \
  score/mw/com/example/systream_demo/etc/mw_com_config.json \
  --cycle-time 500 \
  --num-cycles 20
```

Expected output:
```
[demo_server] Publishing SensorData every 500 ms
[demo_server] Will send 20 samples then exit

[demo_server] Sending sample #0  temp=25.00 °C  pres=1013.00 hPa
[demo_server] Sending sample #1  temp=25.50 °C  pres=1012.87 hPa
...
```

### Step 4 — Run the client (Terminal 2)
Start this **after** the server is running and printing samples.

```bash
./bazel-bin/score/mw/com/example/systream_demo/demo_client \
  --service_instance_manifest \
  score/mw/com/example/systream_demo/etc/mw_com_config.json \
  --num-samples 20
```

Expected output:
```
[demo_client] Searching for SensorService...
[demo_client] SensorService found!
[demo_client] Subscribed. Waiting for sensor data...

[demo_client] Received #0  temp=25.00 °C  pres=1013.00 hPa  latency=1 ms
[demo_client] Received #1  temp=25.50 °C  pres=1012.87 hPa  latency=0 ms
...

[demo_client] ─── Reception Summary ───
  Total received : 20
  Missed samples : 0
  Loss rate      : 0 %
```

---

## Verify Shared Memory Is Active

While both processes are running, open a third terminal and inspect the SHM:

```bash
# List all LoLa shared memory regions
ls -lh /dev/shm/score_lola_*

# Expected:
#  score_lola_9001_1_data      <- event data ring buffer (serviceId=9001, instanceId=1)
#  score_lola_9001_1_control   <- subscriber slot tracking

# Check service discovery socket
ls /tmp/score_lola_service_discovery/
```

---

## Configuration Reference: mw_com_config.json

```
score/mw/com/example/systream_demo/etc/mw_com_config.json
```

| Field | Value | Meaning |
|---|---|---|
| `serviceTypeName` | `/systream/demo/SensorService` | Unique service type name (reverse-DNS style) |
| `serviceId` | `9001` | Numeric ID used in SHM file names — must be unique per ECU |
| `eventName` | `sensor_reading` | Must match the string in `demo_types.h` exactly |
| `eventId` | `1` | Numeric event ID within the service |
| `instanceSpecifier` | `systream/demo/SensorService` | The string used in C++ `InstanceSpecifier::Create()` |
| `instanceId` | `1` | Numeric instance ID |
| `asil-level` | `QM` | Quality Management (non-safety). Use `B` for ASIL-B |
| `shm-size` | `65536` | Total SHM data region in bytes. `sizeof(SensorData) × numberOfSampleSlots × ~3` |
| `control-qm-shm-size` | `32768` | Control SHM for subscriber tracking |
| `numberOfSampleSlots` | `10` | Ring buffer depth — how many samples are buffered before oldest is overwritten |
| `maxSubscribers` | `5` | Maximum simultaneous Proxy processes |

**SHM size formula:**
```
shm-size = sizeof(SensorData) × numberOfSampleSlots × 3
         = 32 bytes          × 10                   × 3  =  960 bytes  (rounded up to 65536)
```
Always round up generously — undersized SHM causes `Allocate()` failures.

---

## Common Errors and Fixes

| Error | Cause | Fix |
|---|---|---|
| `Invalid InstanceSpecifier` | String in code ≠ `instanceSpecifier` in JSON | They must be identical — check for typos and case |
| `OfferService() failed: Binding information invalid` | JSON not found or has schema error | Check `--service_instance_manifest` path is correct |
| `Allocate() failed` | Ring buffer full | Increase `numberOfSampleSlots` in JSON or slow down publishing |
| `ls /dev/shm/score_lola_*` shows nothing | `OfferService()` not called yet, or failed | Check server terminal for errors |
| `Service not found` after 10+ retries | Server crashed before `OfferService()` | Restart server and check its output |
| Bazel sandbox error | Forgot the unblock script | Run Step 1 again |

---

## Extending the Demo

### Add a second event
In `demo_types.h`, add another event member:
```cpp
template <typename Trait>
class SensorInterface : public Trait::Base {
  public:
    using Trait::Base::Base;
    typename Trait::template Event<SensorData> sensor_reading_{*this, "sensor_reading"};
    typename Trait::template Event<SensorData> gps_position_{*this, "gps_position"};  // NEW
};
```
Then add to `mw_com_config.json`:
```json
"events": [
  { "eventName": "sensor_reading", "eventId": 1 },
  { "eventName": "gps_position",   "eventId": 2 }
]
```

### Run indefinitely
```bash
./demo_server --service_instance_manifest etc/mw_com_config.json --cycle-time 100 --num-cycles 0 &
./demo_client --service_instance_manifest etc/mw_com_config.json --num-samples 0
```

### Multiple subscribers
Start multiple client processes in separate terminals — they all receive the
same data simultaneously. The `maxSubscribers` field in the config limits how
many can connect.

---

## Build for aarch64 Linux (e.g. Jetson Nano)

```bash
# Cross-compile from x86_64 Linux
bazel build \
  --config=linux_aarch64_score_gcc_12_2_0_posix \
  //score/mw/com/example/systream_demo/...

# Copy to target hardware
scp bazel-bin/score/mw/com/example/systream_demo/demo_server  user@jetson:/home/user/
scp bazel-bin/score/mw/com/example/systream_demo/demo_client  user@jetson:/home/user/
scp score/mw/com/example/systream_demo/etc/mw_com_config.json user@jetson:/home/user/

# On the Jetson (same run commands as above, using local paths)
./demo_server --service_instance_manifest mw_com_config.json --cycle-time 500 --num-cycles 20
```

---

*Systream Tech India Pvt. Ltd. — SDV Platform Software — April 2026*
