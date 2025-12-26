# Predictive OS-Level Task Migration System

Developing a zero-downtime migration framework for Edge-Cloud orchestration.

## Features
- **Predictive Analytics**: Decision engine predicting resource depletion.
- **State Capture**: Low-level C library for process state serialization.
- **Kernel Integration**: Custom module for telemetry exporting.
- **Cloud Resumption**: Seamless handover mechanism.

## Quick Start (Simulation Mode)

If you are not on a Linux system, the system will run in **Simulation Mode** using standard user-space POSIX APIs.

1. **Build all components**:
   ```bash
   chmod +x scripts/build_all.sh
   ./scripts/build_all.sh
   ```

2. **Run Cloud Runtime** (In one terminal):
   ```bash
   ./cloud/cloud_runtime
   ```

3. **Run Edge Runtime** (In another terminal):
   ```bash
   ./edge/edge_runtime
   ```

4. **Observe**: Once the Edge runtime predicts a failure (simulated randomly or based on thresholds), it will save state to `/tmp/task_state.bin`. The Cloud runtime will detect this file and resume the task automatically.

## Real Deployment (Raspberry Pi / Linux Server)
1. Install kernel headers: `sudo apt install linux-headers-$(uname -r)`
2. Build as above.
3. Load kernel module: `sudo insmod kernel/migration_kmodule.ko`
4. Install services: `sudo cp edge/daemon/*.service /etc/systemd/system/`
5. Enable services: `sudo systemctl enable edge-runtime`
