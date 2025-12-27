# Phase 2: Real Hardware Integration Setup

## Overview
This phase adds:
1. **Real Hardware Telemetry** - Reads CPU, Memory, Battery, Temperature from Linux `/proc` and `/sys`
2. **TCP Network Transfer** - Real socket-based migration between Edge and Cloud

---

## Quick Test (Same Machine - macOS)

Works exactly as before, but now shows `[TELEMETRY-SIM]` to indicate simulation mode:

```bash
# Terminal 1
./cloud/cloud_runtime

# Terminal 2 (in a new terminal)
./edge/edge_runtime
```

---

## Ubuntu VM Setup (Real Hardware Mode)

### Step 1: Copy Project to Ubuntu VM

Option A - Using shared folders:
```bash
# In VirtualBox, set up a shared folder pointing to /Users/nadigsrujan/Documents/osel
```

Option B - Using SCP:
```bash
# From Mac terminal
scp -r /Users/nadigsrujan/Documents/osel user@<ubuntu-vm-ip>:~/
```

Option C - Using Git:
```bash
# In Ubuntu VM
git clone https://github.com/Nadigsrujan/osel.git
cd osel
```

### Step 2: Install Dependencies in Ubuntu

```bash
# Update packages
sudo apt update

# Install build tools
sudo apt install -y build-essential gcc make

# Install Python and OpenCV
sudo apt install -y python3 python3-pip
pip3 install opencv-python-headless
```

### Step 3: Build the Project

```bash
cd ~/osel
chmod +x build_all.sh
./build_all.sh
```

### Step 4: Run on Ubuntu (Real Telemetry!)

```bash
# Terminal 1 - Cloud
./cloud/cloud_runtime

# Terminal 2 - Edge
./edge/edge_runtime
```

You should see `[TELEMETRY-REAL]` instead of `[TELEMETRY-SIM]`:
```
[TELEMETRY-REAL] CPU:45.2% | MEM:62.4% | BAT:-1% | TEMP:52.0°C
```

---

## Two Machine Setup (Full Production)

### Machine 1: Edge Device (e.g., Raspberry Pi or Laptop)
```bash
# Get the Cloud machine's IP first
# Then run Edge with Cloud IP as argument:
./edge/edge_runtime 192.168.1.100
```

### Machine 2: Cloud Server
```bash
./cloud/cloud_runtime
```

### Network Configuration
- **Port 9090**: Edge → Cloud (migration)
- **Port 9091**: Cloud → Edge (return)

Make sure these ports are open in firewall:
```bash
sudo ufw allow 9090
sudo ufw allow 9091
```

---

## Telemetry Sources (Linux)

| Metric | File Path |
|--------|-----------|
| CPU Load | `/proc/loadavg` |
| Memory | `/proc/meminfo` |
| Battery | `/sys/class/power_supply/BAT0/capacity` |
| Temperature | `/sys/class/thermal/thermal_zone0/temp` |

---

## Migration Thresholds

| Metric | Threshold | Action |
|--------|-----------|--------|
| CPU Load | > 80% | Migrate |
| Battery | < 15% | Migrate |
| Temperature | > 80°C | Migrate |
| Progress | > 75% | Don't migrate (finish locally) |

---

## Troubleshooting

### "Connection refused" error
- Make sure Cloud is running FIRST before Edge
- Check firewall: `sudo ufw status`

### "No battery" (-1%)
- Normal on desktops/VMs without battery
- System will still work using CPU/temp thresholds

### Build errors on Ubuntu
```bash
# Install missing headers
sudo apt install -y linux-headers-$(uname -r)
```
