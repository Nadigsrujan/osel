#!/bin/bash
#
# CRIU Checkpoint Script - Freeze and capture a running process
# Usage: ./criu_checkpoint.sh <PID> [output_dir]
#

set -e

PID=$1
OUTPUT_DIR=${2:-"/tmp/criu_checkpoint"}

if [ -z "$PID" ]; then
    echo "Usage: $0 <PID> [output_dir]"
    echo "Example: $0 12345 /tmp/my_checkpoint"
    exit 1
fi

# Check if process exists
if ! kill -0 $PID 2>/dev/null; then
    echo "[ERROR] Process $PID does not exist"
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

# Create output directory
mkdir -p "$OUTPUT_DIR"
rm -rf "$OUTPUT_DIR"/*

echo "[CRIU] Checkpointing process $PID..."
echo "[CRIU] Output directory: $OUTPUT_DIR"

# Perform checkpoint
criu dump -t $PID \
    -D "$OUTPUT_DIR" \
    --shell-job \
    --tcp-established \
    -v4 \
    -o dump.log 2>&1

if [ $? -eq 0 ]; then
    echo "[CRIU] Checkpoint successful!"
    echo "[CRIU] Files created:"
    ls -la "$OUTPUT_DIR"
    
    # Create tarball for transfer
    TARBALL="/tmp/checkpoint_${PID}.tar.gz"
    tar -czf "$TARBALL" -C "$OUTPUT_DIR" .
    echo "[CRIU] Tarball created: $TARBALL ($(du -h $TARBALL | cut -f1))"
else
    echo "[CRIU] Checkpoint FAILED. Check $OUTPUT_DIR/dump.log"
    exit 1
fi
