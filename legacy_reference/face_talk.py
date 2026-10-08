import os
import glob
import time
import threading
from STservo_sdk import *  # Uses STServo SDK library
import random

# ========= CONFIG =========

STS_IDS = [1, 2, 3, 4, 5, 6, 7]

STS_MOVING_SPEED = 4000   # Default STServo moving speed (fallback)
STS_MOVING_ACC   = 250    # Default STServo moving acceleration (fallback)

# STServo limits
STS_MIN_POS = 0           # Minimum position value
STS_MAX_POS = 4095        # Maximum position value

BAUDRATE = 1000000
portHandler = None
packetHandler_st = None
_hardware_ready = False
_init_attempted = False

# Thread Lock for Safe Serial Port Access
lock = threading.Lock()


def _find_serial_port():
    """Pick robot serial port from env or first available USB device."""
    port = os.getenv("ROBOT_SERIAL_PORT", "").strip()
    if port and os.path.exists(port):
        return port
    for pattern in ("/dev/ttyACM*", "/dev/ttyUSB*"):
        ports = sorted(glob.glob(pattern))
        if ports:
            return ports[0]
    return None


def _init_hardware():
    """Lazy-connect to ST servos; skip gracefully when hardware is absent."""
    global portHandler, packetHandler_st, _hardware_ready, _init_attempted
    if _hardware_ready:
        return True
    if _init_attempted:
        return False
    _init_attempted = True

    device = _find_serial_port()
    if not device:
        print("⚠️ No robot serial port found — face/jaw motion disabled")
        return False

    try:
        portHandler = PortHandler(device)
        packetHandler_st = sts(portHandler)
        if not portHandler.openPort():
            print(f"⚠️ Could not open {device} — face/jaw motion disabled")
            return False
        if not portHandler.setBaudRate(BAUDRATE):
            print(f"⚠️ Could not set baud rate on {device} — face/jaw motion disabled")
            return False
        _hardware_ready = True
        print(f"✅ Robot serial port connected: {device}")
        return True
    except Exception as e:
        print(f"⚠️ Robot hardware init failed ({device}): {e} — face/jaw motion disabled")
        return False

# ========= GLOBAL STATE =========

_last_jaw_position = None
_last_jaw_speed = None

_system_enabled = False   # controls all loops (eyes, eyelids, eyebrows, jaw loop)
_jaw_enabled = False      # controls jaw random motion inside jaw loop
_threads_started = False  # tracks if background threads are running

# Thread references (initialized later)
_eye_thread = None
_eyelid_thread = None
_eyebrow_thread = None
_jaw_thread = None


# ========= CORE SERVO HELPERS =========

def st_position_with_speed(servo_id, position, speed, acc):
    """Move one ST servo to a given position with a custom speed and acceleration."""
    if not _hardware_ready:
        return
    position = max(STS_MIN_POS, min(STS_MAX_POS, int(position)))

    with lock:
        sts_comm_result, sts_error = packetHandler_st.WritePosEx(
            servo_id, position, speed, acc
        )

    if sts_comm_result != COMM_SUCCESS:
        print(f"STServo {servo_id} Error: {packetHandler_st.getTxRxResult(sts_comm_result)}")
    if sts_error != 0:
        print(f"STServo {servo_id} Packet Error: {packetHandler_st.getRxPacketError(sts_error)}")
    else:
        print(
            f"STServo {servo_id} moved to position {position} "
            f"(Speed: {speed}, Acc: {acc})"
        )


def _rand_with_gap(min_v, max_v, last_v, gap=20):
    """Return random int in [min_v, max_v] at least `gap` away from last_v."""
    if last_v is None:
        return random.randint(min_v, max_v)

    while True:
        v = random.randint(min_v, max_v)
        if abs(v - last_v) >= gap:
            return v


def move_servos(targets):
    """
    Move multiple servos with individual position, speed, and acceleration.
    targets: list of (id, position, speed, acc) tuples
    """
    if not targets:
        print("⚠️ No targets provided to move_servos()")
        return

    threads = []

    for item in targets:
        if len(item) != 4:
            print(f"⚠️ Invalid target tuple (expected 4 values: id, pos, speed, acc): {item}")
            continue

        servo_id, position, speed, acc = item

        t = threading.Thread(
            target=st_position_with_speed,
            args=(servo_id, position, speed, acc)
        )
        t.start()
        threads.append(t)

    for t in threads:
        t.join()


# ========= FACE MOTION FUNCTIONS =========

def eye_lid():
    # EYELIDS DOWN (both eyes, eyebrows down)
    move_servos([
        (1, 1600, 400, 50),  # CLOSED RIGHT EYELID
        (4, 1700, 400, 50),  # DOWN RIGHT EYEBROW
        (3, 2500, 400, 50),  # CLOSED LEFT EYELID
        (5, 2400, 400, 50),  # DOWN LEFT EYEBROW
    ])
    time.sleep(0.5)

    # BACK TO CENTER
    move_servos([
        (1, 2048, 400, 50),  # CENTRAL RIGHT EYELID
        (4, 2048, 400, 50),  # CENTRAL RIGHT EYEBROW
        (3, 2048, 400, 50),  # CENTRAL LEFT EYELID
        (5, 2048, 400, 50),  # CENTRAL LEFT EYEBROW
    ])
    time.sleep(0.5)


def eye_brow_raise():
    move_servos([
        (4, 2500, 200, 10),   # UP RIGHT EYEBROW
        (5, 1600, 200, 10),   # UP LEFT EYEBROW
        (2, 1500, 4000, 50),  # UPPER LIP OPEN
        (9, 1450, 4000, 50),  # LOWER LIP OPEN
    ])
    time.sleep(1)

    move_servos([
        (4, 2048, 200, 10),   # HOME RIGHT EYEBROW
        (5, 2048, 4000, 10),  # HOME LEFT EYEBROW
        (2, 2048, 4000, 50),  # HOME UPPER LIP
        (9, 2048, 4000, 50),
    ])
    time.sleep(1)


def eye_movement():
    # LEFT EYES
    move_servos([
        (6, 2300, 100, 10),  # LEFT EYES
    ])
    time.sleep(4)

    # CENTER
    move_servos([
        (6, 2024, 100, 10),  # CENTRAL EYES
    ])
    time.sleep(4)

    # RIGHT EYES
    move_servos([
        (6, 1800, 100, 10),  # RIGHT EYES
    ])
    time.sleep(4)

    # BACK TO CENTER
    move_servos([
        (6, 2024, 100, 10),  # CENTRAL EYES
    ])
    time.sleep(4)


# ========= LOOP WRAPPERS (CHECK _system_enabled) =========

def eye_movement_loop():
    """Run eye movement continuously while system is enabled."""
    global _system_enabled
    while _system_enabled:
        eye_movement()


def eye_lid_loop():
    """Run eyelid motion every ~20 seconds while system is enabled."""
    global _system_enabled
    while _system_enabled:
        eye_lid()
        # Only wait more if still enabled
        for _ in range(7):
            if not _system_enabled:
                break
            time.sleep(1)


def eyebrow_loop():
    """Run eyebrow motion continuously while system is enabled."""
    global _system_enabled
    while _system_enabled:
        eye_brow_raise()


def home():
    """Send all servos to neutral home position once."""
    move_servos([
        (1, 2048, 4000, 250),
        (2, 2048, 4000, 250),
        (3, 2048, 4000, 250),
        (4, 2048, 4000, 250),
        (5, 2048, 4000, 250),
        (6, 2048, 4000, 250),
        (7, 2048, 4000, 250),
        (8, 2048, 4000, 250),
        (9, 2048, 4000, 250),
        (10, 2048, 4000, 250),
    ])
    time.sleep(2)


# ========= JAW LOOP AND API =========

def _jaw_loop():
    """Background loop that moves jaw only when _jaw_enabled and _system_enabled are True."""
    global _last_jaw_position, _last_jaw_speed, _jaw_enabled, _system_enabled

    while _system_enabled:
        if _jaw_enabled:
            # random position: 1900–2048, ≥ 20 away from last position
            position = _rand_with_gap(1700, 2048, _last_jaw_position, gap=20)
            # random speed: 1000–1200, ≥ 20 away from last speed
            speed    = _rand_with_gap(900, 950, _last_jaw_speed, gap=20)
            acc      = 45  # fixed

            _last_jaw_position = position
            _last_jaw_speed = speed

            # OPEN LOWER JAW (randomized)
            move_servos([
                (10, position, speed, acc),
            ])
            time.sleep(0.3)
        else:
            time.sleep(0.1)


# ========= START / STOP SYSTEM LOOPS =========

def _start_system_loops():
    """Start eye, eyelid, eyebrow and jaw loops in background threads."""
    global _system_enabled, _threads_started
    global _eye_thread, _eyelid_thread, _eyebrow_thread, _jaw_thread

    if _threads_started:
        return

    _system_enabled = True

    _eye_thread = threading.Thread(target=eye_movement_loop, daemon=True)
    _eye_thread.start()

    _eyelid_thread = threading.Thread(target=eye_lid_loop, daemon=True)
    _eyelid_thread.start()

    _eyebrow_thread = threading.Thread(target=eyebrow_loop, daemon=True)
    _eyebrow_thread.start()

    _jaw_thread = threading.Thread(target=_jaw_loop, daemon=True)
    _jaw_thread.start()

    _threads_started = True


def _stop_system_loops():
    """Signal all loops to stop. They will exit after current sleep."""
    global _system_enabled, _threads_started
    _system_enabled = False
    _threads_started = False
    # Threads are daemons; they will exit when _system_enabled becomes False and the process stays alive.


# ========= PUBLIC API: jaw_movement(state) =========

def jaw_movement(state: int):
    """
    External control function.

    From another Python file:

        jaw_movement(1)
            -> start / continue full face motion (eyes, eyelids, eyebrows) and random jaw.

        jaw_movement(0)
            -> stop all loops, move to home() once, then stay idle until jaw_movement(1) is called again.
    """
    global _jaw_enabled

    if not _init_hardware():
        return

    if state == 1:
        # Start system loops if not running
        _start_system_loops()
        _jaw_enabled = True

    else:
        # Disable jaw random motion and stop system loops
        _jaw_enabled = False
        _stop_system_loops()
        # Move to home once
        home()


# ========= OPTIONAL: STANDALONE TEST =========

# if __name__ == "__main__":
#     # Example: start for 10 seconds, then stop
#     jaw_movement(1)
#     time.sleep(10)
#     jaw_movement(0)
#     print("Stopped and at home.")

