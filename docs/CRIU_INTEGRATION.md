# CRIU Integration Guide

## What is CRIU?
CRIU (Checkpoint/Restore In Userspace) is a Linux tool that can:
- **Freeze** a running process completely (including memory, CPU state, file descriptors)
- **Save** it to disk as image files
- **Restore** it on the same or different machine

This is more powerful than our current state-saving because it captures EVERYTHING.

---

## Installation (Ubuntu)

```bash
# Install CRIU
sudo apt update
sudo apt install -y criu

# Verify installation
criu --version

# Check kernel support
sudo criu check
```

---

## Basic Usage

### Checkpoint (Freeze and Save)
```bash
# Get PID of your Python process
ps aux | grep car_detect

# Checkpoint the process (saves to ./checkpoint_dir/)
sudo criu dump -t <PID> -D checkpoint_dir --shell-job
```

### Restore (Resume)
```bash
# Restore the process
sudo criu restore -D checkpoint_dir --shell-job
```

---

## Integration with Our System

### Modified Migration Flow:

**Before (Current):**
1. Kill Python process
2. Save JSON state file
3. Transfer state file
4. Restart Python with saved state

**After (CRIU):**
1. Freeze Python process with CRIU dump
2. Transfer CRIU checkpoint images
3. Restore Python process with CRIU restore
4. Process continues exactly where it left off

---

## Automated CRIU Scripts

### criu_migrate.sh (Run on Edge)
```bash
#!/bin/bash
PID=$1
CLOUD_IP=$2
CHECKPOINT_DIR="/tmp/criu_checkpoint"

# Create checkpoint
mkdir -p $CHECKPOINT_DIR
sudo criu dump -t $PID -D $CHECKPOINT_DIR --shell-job --tcp-established

# Transfer to cloud
tar -czf /tmp/checkpoint.tar.gz -C $CHECKPOINT_DIR .
scp /tmp/checkpoint.tar.gz user@$CLOUD_IP:/tmp/

# Signal cloud to restore
ssh user@$CLOUD_IP "cd /tmp && tar -xzf checkpoint.tar.gz -C criu_checkpoint && sudo criu restore -D criu_checkpoint --shell-job --tcp-established"
```

### criu_restore.sh (Run on Cloud)
```bash
#!/bin/bash
CHECKPOINT_DIR="/tmp/criu_checkpoint"

# Extract and restore
tar -xzf /tmp/checkpoint.tar.gz -C $CHECKPOINT_DIR
sudo criu restore -D $CHECKPOINT_DIR --shell-job
```

---

## Limitations

1. **Root required**: CRIU needs sudo access
2. **Kernel version**: Some older kernels have limited support
3. **Network sockets**: TCP connections can be checkpointed with `--tcp-established`
4. **Shared memory**: May need special handling
5. **Same architecture**: Cannot restore x86 checkpoint on ARM

---

## Testing CRIU

```bash
# Terminal 1: Start a simple Python script
python3 -c "import time; i=0; 
while True: 
    print(f'Counter: {i}'); 
    i+=1; 
    time.sleep(1)" &

# Get the PID
PID=$!

# Wait a few seconds
sleep 5

# Checkpoint it
sudo criu dump -t $PID -D /tmp/test_checkpoint --shell-job

# The process is now frozen and gone from memory

# Restore it (it continues from where it left off!)
sudo criu restore -D /tmp/test_checkpoint --shell-job
```

---

## Integration Status

| Feature | Status |
|---------|--------|
| Manual CRIU checkpoint | ✅ Documented |
| Automated CRIU in C | 🔄 In Progress |
| Network transfer of images | 🔄 Planned |
| Full integration | 🔄 Planned |

To use CRIU with our system, you'll need to run on Ubuntu with sudo access.
