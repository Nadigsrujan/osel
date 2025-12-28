#!/usr/bin/env python3
"""
Car Detection Task - Hardware Triggered Version
Waits for NodeMCU/Arduino ultrasonic sensor trigger, then runs simulated workload
"""

import time
import json
import os
import sys

STATE_FILE = "/tmp/car_detect_internal.json"

# Try to import serial (optional - falls back to auto-start if not available)
try:
    import serial
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False
    print("[WARN] pyserial not installed. Running without hardware trigger.")

def save_internal_state(progress, result=None):
    """Save progress for dashboard and migration"""
    state = {"progress": progress, "result": result}
    with open(STATE_FILE, "w") as f:
        json.dump(state, f)

def save_sensor_status(status, message=""):
    """Save sensor status for dashboard display"""
    status_file = "/tmp/sensor_status.json"
    data = {"status": status, "message": message}
    with open(status_file, "w") as f:
        json.dump(data, f)

def load_internal_state():
    """Load previous state for resume after migration"""
    if os.path.exists(STATE_FILE):
        try:
            with open(STATE_FILE, "r") as f:
                return json.load(f)
        except:
            pass
    return {"progress": 0, "result": None}

# Set to True to REQUIRE hardware trigger (won't auto-start)
# Set to False to allow dashboard-based start without sensor
HARDWARE_REQUIRED = False

def wait_for_hardware_trigger():
    """Wait for VEHICLE_DETECTED signal from NodeMCU/Arduino via serial"""
    if not SERIAL_AVAILABLE:
        if HARDWARE_REQUIRED:
            save_sensor_status("error", "pyserial not installed")
            print("[ERROR] pyserial not installed. Cannot use hardware mode.")
            print("[ERROR] Install with: pip3 install pyserial")
            return False
        save_sensor_status("demo", "Running in demo mode (no sensor)")
        print("[EDGE] No serial - starting immediately (demo mode)")
        return True
    
    # Try common serial ports (Linux and Mac)
    ports = ['/dev/ttyUSB0', '/dev/ttyACM0', '/dev/ttyUSB1', 
             '/dev/cu.usbserial-0001', '/dev/cu.SLAB_USBtoUART',
             '/dev/cu.wchusbserial1410']
    ser = None
    
    for port in ports:
        try:
            ser = serial.Serial(port, 9600, timeout=1)
            save_sensor_status("connected", f"Sensor connected on {port}")
            print(f"[HARDWARE] ✅ Connected to {port}")
            break
        except:
            continue
    
    if ser is None:
        if HARDWARE_REQUIRED:
            save_sensor_status("error", "No sensor found!")
            print("[ERROR] No Arduino/NodeMCU found on any port!")
            print("[ERROR] Check USB connection and try: ls /dev/ttyUSB* /dev/cu.usb*")
            return False
        save_sensor_status("demo", "No sensor found - demo mode")
        print("[WARN] No Arduino/NodeMCU found. Starting without trigger (demo mode).")
        return True
    
    save_sensor_status("waiting", "🔴 Waiting for vehicle...")
    print("[EDGE] Waiting for sensor trigger...")
    print("[EDGE] Move object close to ultrasonic sensor (<20cm)")
    
    try:
        while True:
            if ser.in_waiting > 0:
                msg = ser.readline().decode('utf-8', errors='ignore').strip()
                print(f"[SERIAL] Received: {msg}")
                if "VEHICLE_DETECTED" in msg:
                    save_sensor_status("detected", "🚗 VEHICLE DETECTED!")
                    print("[EDGE] ✅ Trigger received - starting computation!")
                    ser.close()
                    return True
            time.sleep(0.1)
    except KeyboardInterrupt:
        ser.close()
        return False

def run_simulated_task(start_step=0):
    """
    Simulated heavy computation workload.
    10 stages, 3 seconds each = 30 seconds total.
    Can be migrated at any stage.
    """
    print(f"[AI-ENGINE] Starting simulated workload from step {start_step}")
    
    for step in range(start_step, 10):
        progress = step * 10
        save_internal_state(progress)
        
        print(f"[COMPUTE] Processing Stage {step+1}/10 ({progress}%)...")
        
        # Simulate heavy work
        time.sleep(3)
    
    # Final result (simulated detection count)
    result = 3  # Simulated: "3 vehicles detected"
    save_internal_state(100, result)
    
    return result

if __name__ == "__main__":
    print("=" * 50)
    print("  🚗 VEHICLE DETECTION TASK - HARDWARE MODE")
    print("=" * 50)
    
    # Check if resuming from migration
    state = load_internal_state()
    start_at = (state["progress"] // 10) if state["progress"] < 100 else 0
    
    if start_at > 0:
        print(f"[RESUME] Continuing from stage {start_at + 1} (progress was {state['progress']}%)")
    else:
        # Fresh start - wait for hardware trigger OR dashboard start
        # Check if started by dashboard (file exists)
        if not os.path.exists("/tmp/dashboard_started"):
            # Wait for hardware trigger
            if not wait_for_hardware_trigger():
                print("[EXIT] No trigger received")
                sys.exit(0)
        else:
            print("[EDGE] Started by Dashboard - skipping sensor wait")
            os.remove("/tmp/dashboard_started")
    
    # Run the task
    result = run_simulated_task(start_step=start_at)
    
    print()
    print("=" * 50)
    print(f"  ✅ TASK COMPLETE")
    print(f"  📊 Simulated Result: {result} vehicles detected")
    print("=" * 50)
