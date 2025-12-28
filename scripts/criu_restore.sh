#!/bin/bash
#
# CRIU Restore Script - Resume a checkpointed process
# Usage: ./criu_restore.sh [checkpoint_dir]
#

set -e

CHECKPOINT_DIR=${1:-"/tmp/criu_checkpoint"}

if [ ! -d "$CHECKPOINT_DIR" ]; then
    echo "[ERROR] Checkpoint directory not found: $CHECKPOINT_DIR"
    exit 1
fi

# Check if CRIU is installed
if ! command -v criu &> /dev/null; then
    echo "[ERROR] CRIU is not installed. Run: sudo apt install criu"
    exit 1
fi

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "[ERROR] CRIU requires root. Run with sudo."
    exit 1
fi

echo "[CRIU] Restoring process from $CHECKPOINT_DIR..."

# Perform restore
criu restore \
    -D "$CHECKPOINT_DIR" \
    --shell-job \
    --tcp-established \
    -v4 \
    -o restore.log 2>&1

if [ $? -eq 0 ]; then
    echo "[CRIU] Restore successful! Process is now running."
else
    echo "[CRIU] Restore FAILED. Check $CHECKPOINT_DIR/restore.log"
    exit 1
fi
