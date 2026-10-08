import os
import random
import time
import pypot.dynamixel

# Gesture dictionaries
WALK2 = {1: 75.03, 2: -30, 3: 30.2, 5: 30, 7: 29.23, 8: -45.6, 9: 75.38}
WALK = {1: 76.62, 2: -35, 3: 35.82, 5: 40, 7: 35.47, 8: -35, 9: 72.04}
EXITED = {1: 85.58, 2: -18, 3: 47.87, 5: 0, 7: 17.19, 8: -15.81, 9: 40.22}
OUTPOS = {1: 18.68, 2: -23, 3: 11.74, 5: -40, 7: 23.78, 8: -20.79, 9: 87.16}
TWOPOS = {1: 76.09, 2: -30, 3: 38.9, 5: -30, 7: 30.99, 8: -15.81, 9: 72.22}
ONEPOS = {1: 74.59, 2: -4, 3: 6.54, 5: 35, 7: 3.65, 8: -6.79, 9: 75.03}
RESTPOS = {1: 3.3, 2: -5, 3: 6.24, 5: 0, 7: 4.7, 8: -5, 9: 1.98}

# List of gestures
gestures = [WALK2, WALK, EXITED, OUTPOS, TWOPOS, ONEPOS]

# Connect to U2D2 device
def connect_to_u2d2():
    ports = pypot.dynamixel.get_available_ports()
    if not ports:
        return None
    for port in ports:
        if not os.path.exists(port):
            continue
        try:
            return pypot.dynamixel.DxlIO(port)
        except Exception as e:
            print(f"Could not open gesture port {port}: {e}")
    return None

# Function to execute a random gesture
def perform_random_gesture(dxl_io):
    # Choose a random gesture from the list
    random_gesture = random.choice(gestures)
    print("random_gesture", random_gesture)
    time.sleep(1.5)

    
    # Set moving speed for each motor
    speed = 40  # Adjust speed as required
    dxl_io.set_moving_speed({motor_id: speed for motor_id in random_gesture.keys()})
    dxl_io.set_moving_speed({5: 45})  # Special speed for motor 5

    # Set goal position for each motor based on the chosen gesture
    dxl_io.set_goal_position(random_gesture)

# Function to execute the RESTPOS gesture
def perform_rest_position(dxl_io):
    speed = 30  # Adjust speed as required for RESTPOS
    dxl_io.set_moving_speed({motor_id: speed for motor_id in RESTPOS.keys()})
    dxl_io.set_goal_position(RESTPOS)
    print("COMPLETED")
    #sys.exit()

# Main function
def main(runtime):
    dxl_io = connect_to_u2d2()

    if dxl_io is not None:
        start_time = time.time()
        elapsed_time = 0

        # Run random gestures for the given amount of time
        while elapsed_time < runtime:
            perform_random_gesture(dxl_io)
            elapsed_time = time.time() - start_time
        
        # After the time has elapsed, execute RESTPOS continuously
        while True:
            perform_rest_position(dxl_io)
            time.sleep(1)  # Time between RESTPOS executions
            break

