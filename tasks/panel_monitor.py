import time
import os
import json
import socket
import sys
from datetime import datetime

# Serial configuration
SERIAL_AVAILABLE = False
try:
    import serial
    SERIAL_AVAILABLE = True
except ImportError:
    pass

# Simulation / Hardware flag
HARDWARE_REQUIRED = False  # Set to True to force waiting for Arduino

def save_panel_status(status, message, data=None):
    """Write panel sensor status for dashboard"""
    status_file = "/tmp/sensor_status.json"
    status_data = {
        "status": status,
        "message": message,
        "timestamp": datetime.now().strftime("%H:%M:%S"),
        "sensor_data": data or {}
    }
    with open(status_file, 'w') as f:
        json.dump(status_data, f)

def wait_for_hazard():
    """Wait for MIGRATE_REQ from Arduino via serial"""
    if not SERIAL_AVAILABLE:
        print("[PANEL] Serial not available, starting in demo mode...")
        save_panel_status("demo", "Running in demo mode (no hardware)")
        return {"T": 42.5, "D": 5, "L": 150} # Simulated hazard data

    ports = ['/dev/ttyUSB0', '/dev/ttyACM0', '/dev/ttyUSB1', '/dev/cu.usbserial-0001']
    ser = None
    
    for port in ports:
        try:
            ser = serial.Serial(port, 9600, timeout=1)
            print(f"[PANEL] Connected to {port}")
            break
        except:
            continue
            
    if ser is None:
        if HARDWARE_REQUIRED:
            print("[ERROR] No Arduino found!")
            save_panel_status("error", "Arduino not found")
            return None
        print("[PANEL] No hardware found, using simulated hazard...")
        save_panel_status("demo", "No hardware, simulated hazard")
        return {"T": 42.5, "D": 5, "L": 150}

    print("[PANEL] Monitoring panel sensor stream...")
    save_panel_status("waiting", "🔴 System Normal - Monitoring...")
    
    try:
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if "MIGRATE_REQ" in line:
                    # Parse data: MIGRATE_REQ,T=43.2,D=5,L=120
                    print(f"[HAZARD] {line}")
                    save_panel_status("detected", "⚠️ HAZARD DETECTED!")
                    
                    # Extract values
                    data = {}
                    parts = line.split(',')
                    for p in parts[1:]:
                        k, v = p.split('=')
                        data[k] = float(v)
                    
                    ser.close()
                    return data
            time.sleep(0.1)
    except KeyboardInterrupt:
        ser.close()
        return None

def run_severity_analysis(sensor_data):
    """Heavy computation to simulate cloud analysis"""
    print(f"\n[ANALYSIS] Starting critical severity analysis for Panel...")
    print(f"[DATA] Temp: {sensor_data['T']}C, Dist: {sensor_data['D']}cm, Light: {sensor_data['L']}")
    
    for i in range(1, 101):
        # Severity calculation: Base temp + intrusion weight + light change
        severity = sensor_data['T'] + (20 if sensor_data['D'] < 10 else 0)
        
        # Write progress and internal state
        state = {
            "progress": i,
            "task": "Panel Severity Analysis",
            "severity_score": round(severity, 2),
            "status": "DANGER" if severity > 50 else "CAUTION",
            "timestamp": time.time()
        }
        
        with open("/tmp/car_detect_internal.json", "w") as f:
            json.dump(state, f)
            
        if i % 10 == 0:
            print(f"[ANALYSIS] Progress: {i}% (Severity Score: {state['severity_score']})")
        
        # Simulate load - increase sleep to make migration visible
        time.sleep(1.5)

if __name__ == "__main__":
    # Signal that we started
    with open("/tmp/dashboard_started", "w") as f:
        f.write("1")
        
    sensor_data = wait_for_hazard()
    if sensor_data:
        run_severity_analysis(sensor_data)
