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

def save_panel_status(status, message, data=None):
    """Write panel sensor status for dashboard"""
    status_file = "/tmp/sensor_status.json"
    status_data = {
        "status": status,
        "message": message,
        "timestamp": datetime.now().strftime("%H:%M:%S"),
        "sensor_data": data or {"T": 0, "D": 0, "L": 0}
    }
    with open(status_file, 'w') as f:
        json.dump(status_data, f)

def wait_for_hazard():
    """Wait for MIGRATE from ESP8266 via serial"""
    if not SERIAL_AVAILABLE:
        print("[PANEL] Serial (pyserial) not installed. Check requirements.txt")
        return None

    # Common ports for NodeMCU
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
        print("[PANEL] No Arduino/ESP8266 found. Is it plugged in?")
        save_panel_status("error", "Hardware not found")
        return None

    print("[PANEL] Monitoring panel sensor stream...")
    save_panel_status("waiting", "🔴 System Normal - Monitoring...")
    
    try:
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                
                # Update metrics immediately whenever data arrives
                if any(x in line for x in ["T=", "D="]):
                    data = {}
                    parts = line.split(',')
                    for p in parts:
                        if '=' in p:
                            k, v = p.split('=')
                            data[k] = float(v)
                    
                    # Update status for dashboard display
                    if "EDGE_OK" in line:
                        save_panel_status("waiting", "🔴 System Normal", data)
                        # LOG TO FILE for dashboard debugging
                        with open("/tmp/task_logs.log", "a") as logf:
                            logf.write(f"[SERIAL] {line}\n")
                    
                    # Hazard Trigger
                    if "MIGRATE" in line:
                        print(f"[HAZARD] {line}")
                        save_panel_status("detected", "⚠️ PANEL HAZARD DETECTED!", data)
                        with open("/tmp/task_logs.log", "a") as logf:
                            logf.write(f"⚠️ HAZARD: {line}\n")
                        ser.close()
                        return data
            time.sleep(0.05)
    except KeyboardInterrupt:
        ser.close()
        return None

def run_heavy_analysis(sensor_data, start_progress=1):
    """Simulated heavy computation to trigger REAL migration load"""
    print(f"\n[ANALYSIS] Starting critical severity analysis for Panel (from {start_progress}%)...")
    
    for i in range(start_progress, 101):
        # 1. ACTUAL CPU SPIKE: Do some math for a split second
        end_time = time.time() + 0.5 
        while time.time() < end_time:
            _ = 12345 * 54321 # Waste CPU cycles
            
        # 2. Severity calculation
        severity = sensor_data.get('T', 30.0) + (20 if sensor_data.get('D', 100) < 10 else 0)
        
        # 3. Share state with system (allows C scheduler to 'checkpoint' us)
        state = {
            "progress": i,
            "task": "Panel Severity Analysis",
            "severity_score": round(severity, 2),
            "status": "DANGER" if severity > 50 else "CAUTION",
            "sensor_data": sensor_data,
            "timestamp": time.time()
        }
        
        with open("/tmp/car_detect_internal.json", "w") as f:
            json.dump(state, f)
            
        if i % 5 == 0:
            print(f"[ANALYSIS] Progress: {i}% (Severity Score: {state['severity_score']})")
        
        # Dashboard stress automation will take over once we hit 20%
        time.sleep(0.2)

if __name__ == "__main__":
    # CHECK: Are we resuming from a checkpoint on the Cloud?
    json_path = "/tmp/car_detect_internal.json"
    
    # If the file exists and progress > 0, we are MIGRATING/RESUMING
    if os.path.exists(json_path):
        try:
            with open(json_path, 'r') as f:
                checkpoint = json.load(f)
                if checkpoint.get("progress", 0) > 0 and checkpoint.get("progress", 0) < 100:
                    print(f"[CLOUD] Resuming analysis from {checkpoint['progress']}%...")
                    run_heavy_analysis(checkpoint.get("sensor_data", {}), checkpoint["progress"])
                    sys.exit(0)
        except:
            pass

    # Normal Edge Start
    with open("/tmp/dashboard_started", "w") as f:
        f.write("1")
        
    sensor_data = wait_for_hazard()
    if sensor_data:
        run_heavy_analysis(sensor_data)
