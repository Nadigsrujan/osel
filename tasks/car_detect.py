import cv2
import sys
import os
import time
import json

STATE_FILE = "/tmp/car_detect_internal.json"

def save_internal_state(progress, result=None):
    state = {"progress": progress, "result": result}
    with open(STATE_FILE, "w") as f:
        json.dump(state, f)

def load_internal_state():
    if os.path.exists(STATE_FILE):
        with open(STATE_FILE, "r") as f:
            return json.load(f)
    return {"progress": 0, "result": None}

def process(img_path, start_step=0):
    print(f"[AI-ENGINE] Loading model and payload: {img_path}")
    if not os.path.exists(img_path):
        return -1
    
    cascade_path = os.path.join(os.path.dirname(__file__), 'cars.xml')
    car_cascade = cv2.CascadeClassifier(cascade_path)
    img = cv2.imread(img_path)
    
    # 10 Stages of Inference
    for step in range(start_step, 10):
        save_internal_state(step * 10) 
        print(f"[COMPUTE] Executing Inference Stage {step+1}/10...")
        time.sleep(3)  # Slower processing - 3 seconds per step


    final_result = len(car_cascade.detectMultiScale(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), 1.1, 3))
    save_internal_state(100, final_result)
    return final_result

if __name__ == "__main__":
    img_path = sys.argv[1] if len(sys.argv) > 1 else "edge/input/car.jpg"
    state = load_internal_state()
    start_at = (state["progress"] // 10) if state["progress"] < 100 else 0
    
    if start_at > 0:
        print(f"[RESUME] Resuming inference from stage {start_at+1}...")

    result = process(img_path, start_step=start_at)
    print(f"\n========================================\nFINAL OUTPUT: {result} vehicles detected.\n========================================\n")
    # Keep STATE_FILE so C scheduler can read 100%

