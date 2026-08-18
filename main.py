from arduino.app_utils import App, Bridge
import socket, csv, json, time, threading

LISTEN_PORT = 5005
TELEMETRY_PORT = 5006
PC_IP = "192.168.1.100"
CSV_PATH = "gepard_telemetry.csv"
VALID_MODES = {"ROAM", "SENTINEL", "MANUAL"}
current_mode = "MANUAL"

FIELD_ORDER = [
    "timestamp", "ultrasonic_cm", "ultrasonic_valid", "enc_left_ticks", 
    "enc_right_ticks", "encoders_valid", "cliff_left", "cliff_right", 
    "rear_ir", "ir_valid", "bus_voltage_v", "ina226_valid", "accel_x", 
    "accel_y", "accel_z", "mpu6050_valid", "current_command", 
    "obstacle_override", "dock_mode", "estop", "mode"
]

cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
cmd_sock.bind(("0.0.0.0", LISTEN_PORT))
cmd_sock.settimeout(0.5)
tele_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

csv_file = open(CSV_PATH, "a", newline="")
csv_writer = csv.writer(csv_file)
if csv_file.tell() == 0:
    csv_writer.writerow(FIELD_ORDER)
    csv_file.flush()

def on_telemetry(ultrasonic_cm, ultrasonic_valid, enc_left_ticks, enc_right_ticks, encoders_valid, cliff_left, cliff_right, rear_ir, ir_valid, bus_voltage_v, ina226_valid, accel_x, accel_y, accel_z, mpu6050_valid, current_command, obstacle_override, dock_mode, estop):
    record = {
        "timestamp": time.time(), "ultrasonic_cm": ultrasonic_cm, "ultrasonic_valid": ultrasonic_valid,
        "enc_left_ticks": enc_left_ticks, "enc_right_ticks": enc_right_ticks, "encoders_valid": encoders_valid,
        "cliff_left": cliff_left, "cliff_right": cliff_right, "rear_ir": rear_ir, "ir_valid": ir_valid,
        "bus_voltage_v": bus_voltage_v, "ina226_valid": ina226_valid, "accel_x": accel_x, "accel_y": accel_y, "accel_z": accel_z,
        "mpu6050_valid": mpu6050_valid, "current_command": current_command, "obstacle_override": obstacle_override,
        "dock_mode": dock_mode, "estop": estop, "mode": current_mode,
    }
    csv_writer.writerow([record[f] for f in FIELD_ORDER])
    csv_file.flush()
    try: tele_sock.sendto(json.dumps(record).encode(), (PC_IP, TELEMETRY_PORT))
    except: pass

Bridge.provide("telemetry", on_telemetry)

def handle_command(text):
    global current_mode
    text = text.strip()
    if not text: return
    
    if text.startswith("N:"):
        try:
            _, direction, ms = text.split(":")
            Bridge.call("nudge", direction, int(ms))
        except: pass
        return
        
    if text.startswith("MODE:"):
        mode = text.split(":", 1)[1].upper()
        if mode in VALID_MODES:
            current_mode = mode
            Bridge.call("drive", "S")
        return
        
    if text.startswith("DOCK:"):
        on = text.split(":", 1)[1]
        Bridge.call("dockmode", 1 if on == "1" else 0)
        return
        
    if text.startswith("ESTOP:"):
        on = text.split(":", 1)[1]
        Bridge.call("estop", 1 if on == "1" else 0)
        return
        
    Bridge.call("drive", text[0])

def command_listener():
    while True:
        try:
            data, addr = cmd_sock.recvfrom(128)
            handle_command(data.decode(errors="ignore"))
        except socket.timeout: continue

threading.Thread(target=command_listener, daemon=True).start()

def loop():
    time.sleep(1)

App.run(user_loop=loop)
