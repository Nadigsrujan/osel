import time
import os
import json
import sys
from datetime import datetime

def calculate_trend(history):
    """Calculate temperature slope (degrees per second)"""
    if len(history) < 2:
        return 0.0
    
    # Calculate difference between latest and oldest in buffer
    temp_diff = history[-1]['T'] - history[0]['T']
    time_diff = history[-1]['time'] - history[0]['time']
    
    if time_diff <= 0:
        return 0.0
        
    return temp_diff / time_diff

def save_internal_state(progress, sensor_data, history):
    """Update state with Predictive Analytics for the Dashboard"""
    
    current_temp = sensor_data.get('T', 30.0)
    current_dist = sensor_data.get('D', 100)
    
    # 1. Predictive Logic
    slope = calculate_trend(history) # Degrees per second
    
    # Fire Prediction: If temp hits 60C, it's a fire
    critical_temp = 60.0
    seconds_to_fire = 999
    if slope > 0.1: # Significant rising trend
        seconds_to_fire = max(0, round((critical_temp - current_temp) / slope, 1))
    
    # Danger Score (0-100)
    # Factor 1: Current Temp
    # Factor 2: Slope (Aggressive heat is dangerous)
    # Factor 3: Intrusion
    danger_score = (current_temp - 20) * 1.5
    danger_score += (slope * 50)
    if current_dist < 10: danger_score += 30
    danger_score = min(100, max(0, danger_score))

    state = {
        "progress": progress,
        "task": "Predictive Fire Analytics",
        "severity_score": round(danger_score, 1),
        "status": "CRITICAL" if danger_score > 70 else "WARNING" if danger_score > 40 else "NORMAL",
        "sensor_data": sensor_data,
        "analytics": {
            "temp_slope": round(slope, 2),
            "est_failure_sec": seconds_to_fire if seconds_to_fire < 300 else "Stable",
            "fire_confidence": f"{round(min(98, danger_score + (slope*10)), 1)}%"
        },
        "timestamp": time.time(),
        "history_buffer": history[-10:] # Keep buffer in checkpoint for migration!
    }

    with open("/tmp/car_detect_internal.json", "w") as f:
        json.dump(state, f)

def run_analysis(start_at=0, initial_history=None):
    """The Predictive Engine that migrates"""
    print(f"[TASK] Predictive Engine started at {start_at}%")
    
    history = initial_history or []
    
    for i in range(start_at, 101):
        # 1. HEAVY COMPUTE: Cloud-only Predictive Math
        # We simulate the complex 'Slope Analysis' and 'Prediction' load
        end_time = time.time() + 0.35
        while time.time() < end_time:
            _ = 8000 * 8000 

        # 2. Get latest sensors
        sensor_data = {"T": 30.0, "D": 100}
        if os.path.exists("/tmp/sensor_status.json"):
            try:
                with open("/tmp/sensor_status.json", "r") as f:
                    sd = json.load(f)
                    sensor_data = sd.get("sensor_data", {})
            except: pass

        # 3. Update History Buffer
        history.append({"T": sensor_data.get('T', 30.0), "time": time.time()})
        if len(history) > 15: history.pop(0)

        # Update progress (Slower for better demo)
        progress += 2
        if progress > 100: progress = 100
        
        # 4. Perform Analytics & Save (Migration-ready)
        save_internal_state(progress, sensor_data, history) # Use 'progress' and 'history'

        if progress % 10 == 0: # Use progress for printing
            print(f"[TASK] Analyzing... {progress}% (Predicting failure in: {progress*0.5}s)")
            
        # Time for user to react (increased sleep for slower task)
        time.sleep(1.2)

        if progress >= 100: # Exit loop once 100% is reached
            break

if __name__ == "__main__":
    with open("/tmp/dashboard_started", "w") as f:
        f.write("1")

    # Migration Resume Support
    start_progress = 0
    history_buffer = []
    if os.path.exists("/tmp/car_detect_internal.json"):
        try:
            with open("/tmp/car_detect_internal.json", "r") as f:
                saved = json.load(f)
                if saved.get("progress", 0) < 100:
                    start_progress = saved["progress"]
                    history_buffer = saved.get("history_buffer", [])
                    print(f"[RESUME] Recovered history buffer: {len(history_buffer)} samples")
        except: pass

    run_analysis(start_progress, history_buffer)
