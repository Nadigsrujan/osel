# Complete Ubuntu Integration Guide

## Overview
This guide walks you through setting up the entire Edge-Cloud Migration System on Ubuntu, including:
- Core migration system (C)
- AI task (Python/OpenCV)
- Web Dashboard (Flask)
- CRIU Integration (Optional)

---

## Step 1: System Requirements

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install build tools
sudo apt install -y build-essential gcc make git

# Install Python and pip
sudo apt install -y python3 python3-pip python3-venv

# Install OpenCV dependencies
sudo apt install -y libopencv-dev python3-opencv

# Install Flask for dashboard
pip3 install flask opencv-python-headless

# (Optional) Install CRIU for advanced checkpointing
sudo apt install -y criu
```

---

## Step 2: Get the Project

### Option A: Clone from GitHub
```bash
cd ~
git clone https://github.com/Nadigsrujan/osel.git
cd osel
```

### Option B: Copy from Mac via SCP
```bash
# Run this on your Mac terminal:
scp -r /Users/nadigsrujan/Documents/osel user@<ubuntu-ip>:~/
```

### Option C: VirtualBox Shared Folder
1. In VirtualBox: Settings → Shared Folders → Add
2. Folder Path: `/Users/nadigsrujan/Documents/osel`
3. Mount point: `/mnt/osel`
4. Check "Auto-mount"
5. In Ubuntu:
```bash
sudo usermod -aG vboxsf $USER  # Add yourself to vboxsf group
# Log out and log back in
cp -r /mnt/osel ~/osel
cd ~/osel
```

---

## Step 3: Build the Project

```bash
cd ~/osel

# Make build script executable
chmod +x build_all.sh

# Build the C components
./build_all.sh
```

Expected output:
```
gcc -Iinclude -Wall -o edge_runtime ...
gcc -I../edge/include -Wall -o cloud_runtime ...
```

---

## Step 4: Verify the Input Files

```bash
# Check car detection model exists
ls -la tasks/cars.xml

# Check sample image exists
ls -la edge/input/car.jpg

# If missing, download sample image:
mkdir -p edge/input
wget -O edge/input/car.jpg "https://upload.wikimedia.org/wikipedia/commons/thumb/1/1c/2016_Honda_Civic_sedan_%28facelift%2C_red%29%2C_front_9.29.19.jpg/1280px-2016_Honda_Civic_sedan_%28facelift%2C_red%29%2C_front_9.29.19.jpg"
```

---

## Step 5: Run the Migration Demo (Terminal Mode)

### Terminal 1 - Start Cloud Runtime:
```bash
cd ~/osel
./cloud/cloud_runtime
```

You should see:
```
============================================
   CLOUD RUNTIME - Phase 2 (Network)
============================================

[WAIT] Waiting for task migration from Edge...
[NET] Listening for migrations on port 9090...
```

### Terminal 2 - Start Edge Runtime:
```bash
cd ~/osel
./edge/edge_runtime
```

You should see:
```
============================================
   EDGE TASK MANAGER - Phase 2 (Real HW)
============================================

[CONFIG] Cloud IP: 127.0.0.1
[TELEMETRY-REAL] CPU:15.2% | MEM:45.3% | BAT:-1% | TEMP:52.0°C
...
```

Watch the migration happen in real-time!

---

## Step 6: Run with Web Dashboard

### Start the Dashboard:
```bash
cd ~/osel
python3 dashboard/app.py
```

Output:
```
==================================================
  Edge-Cloud Migration Dashboard
  Open http://localhost:5050 in your browser
==================================================
```

### Open in Browser:
- If using VirtualBox: `http://localhost:5050`
- If accessing from Mac: `http://<ubuntu-ip>:5050`

### Use the Dashboard:
1. Click **"Start Task"** to begin
2. Watch the progress bar fill
3. See status change from **EDGE** → **CLOUD** → **EDGE**
4. Final result: "4 vehicles detected"

---

## Step 7: Verify Real Telemetry

On Ubuntu, you should see `[TELEMETRY-REAL]` instead of `[TELEMETRY-SIM]`:

```
[TELEMETRY-REAL] CPU:23.5% | MEM:62.1% | BAT:-1% | TEMP:48.0°C
```

This means it's reading from actual Linux `/proc` files:
- CPU: `/proc/loadavg`
- Memory: `/proc/meminfo`
- Temperature: `/sys/class/thermal/thermal_zone0/temp`

---

## Step 8: (Optional) CRIU Integration

### Install CRIU:
```bash
sudo apt install -y criu

# Verify
criu --version
sudo criu check
```

### Test CRIU Manually:
```bash
# Start a test process
python3 -c "import time; i=0
while True:
    print(f'Count: {i}')
    i += 1
    time.sleep(1)" &

# Get its PID
PID=$!
echo "PID is $PID"

# Wait a few seconds
sleep 5

# Checkpoint it (freezes the process)
sudo ./scripts/criu_checkpoint.sh $PID

# The process is now gone! But saved.

# Restore it (continues from where it left off)
sudo ./scripts/criu_restore.sh
```

---

## Step 9: Two-Machine Setup (Advanced)

### Machine 1 (Edge - e.g., Raspberry Pi):
```bash
# Get Cloud's IP address first
# Then run Edge pointing to Cloud
./edge/edge_runtime 192.168.1.100  # Replace with Cloud's IP
```

### Machine 2 (Cloud - e.g., Server):
```bash
# Run Cloud (listens on all interfaces)
./cloud/cloud_runtime
```

### Firewall:
```bash
# On Cloud machine, allow ports
sudo ufw allow 9090  # Migration port
sudo ufw allow 9091  # Return port
sudo ufw allow 5050  # Dashboard port
```

---

## Troubleshooting

### "No such file" errors:
```bash
# Make sure you're in the project directory
cd ~/osel
pwd  # Should show /home/<user>/osel
```

### "Permission denied":
```bash
chmod +x edge/edge_runtime cloud/cloud_runtime
chmod +x scripts/*.sh
```

### "ModuleNotFoundError: cv2":
```bash
pip3 install opencv-python-headless
```

### Dashboard not accessible from Mac:
```bash
# Check Ubuntu's IP
hostname -I

# Access from Mac browser:
# http://<ubuntu-ip>:5050
```

### CRIU "Operation not permitted":
```bash
# Must run with sudo
sudo ./scripts/criu_checkpoint.sh <PID>
```

---

## Quick Reference Commands

| Action | Command |
|--------|---------|
| Build project | `./build_all.sh` |
| Start Cloud | `./cloud/cloud_runtime` |
| Start Edge | `./edge/edge_runtime` |
| Start Dashboard | `python3 dashboard/app.py` |
| Clean state files | `rm -f /tmp/*.bin /tmp/car_detect_internal.json` |
| Kill all processes | `pkill -f cloud_runtime; pkill -f edge_runtime; pkill -f car_detect` |

---

## Project Structure

```
osel/
├── edge/
│   ├── src/
│   │   ├── edge_scheduler.c      # Main Edge logic
│   │   ├── telemetry_collector.c # CPU/Memory/Temp reading
│   │   └── network_transfer.c    # TCP communication
│   └── edge_runtime              # Compiled binary
├── cloud/
│   ├── src/
│   │   └── cloud_scheduler.c     # Main Cloud logic
│   └── cloud_runtime             # Compiled binary
├── tasks/
│   ├── car_detect.py             # AI task
│   └── cars.xml                  # ML model
├── dashboard/
│   ├── app.py                    # Flask server
│   └── templates/
│       └── dashboard.html        # Web UI
├── scripts/
│   ├── criu_checkpoint.sh        # CRIU freeze
│   └── criu_restore.sh           # CRIU resume
├── docs/
│   ├── PHASE2_SETUP.md
│   └── CRIU_INTEGRATION.md
└── build_all.sh                  # Build script
```

---

## Success Checklist

- [ ] Project builds without errors
- [ ] Cloud runtime starts and listens on port 9090
- [ ] Edge runtime starts and shows `[TELEMETRY-REAL]`
- [ ] Migration triggers when CPU > 80%
- [ ] Task completes with "4 vehicles detected"
- [ ] Dashboard shows real-time status
- [ ] (Optional) CRIU check passes

---

**Congratulations! Your Edge-Cloud Migration System is fully operational on Ubuntu!** 🎉
