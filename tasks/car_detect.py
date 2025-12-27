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
        print(f"[ERROR] Image {img_path} not found.")
        return -1
    
    cascade_path = os.path.join(os.path.dirname(__file__), 'cars.xml')
    car_cascade = cv2.CascadeClassifier(cascade_path)
    img = cv2.imread(img_path)
    
    # Simulate a heavy 10-step multi-stage inference process
    # This allows us to migrate "mid-task" accurately
    for step in range(start_step, 10):
        # The Schedulers will look at this file to capture "state"
        save_internal_state(step * 10) 
        
        print(f"[COMPUTE] Executing Inference Stage {step+1}/10...")
        # Burning some CPU for realism
        for _ in range(1000000): pass 
        time.sleep(1.5) 

    # Final detection
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    cars = car_cascade.detectMultiScale(gray, 1.1, 3)
    
    final_result = len(cars)
    save_internal_state(100, final_result)
    return final_result

if __name__ == "__main__":
    img_path = sys.argv[1] if len(sys.argv) > 1 else "edge/input/car.jpg"
    
    # Check if we are resuming from a checkpoint
    state = load_internal_state()
    start_at = (state["progress"] // 10) if state["progress"] < 100 else 0
    
    if start_at > 0:
        print(f"[RESUME] Resuming inference from stage {start_at+1}...")

    result = process(img_path, start_step=start_at)
    
    print("\n" + "="*40)
    print(f"FINAL OUTPUT: {result} vehicles detected.")
    print("="*40)
    
    # Cleanup state on full completion
    if os.path.exists(STATE_FILE):
        os.remove(STATE_FILE)
