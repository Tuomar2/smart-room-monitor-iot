import asyncio
import struct
import collections
import requests
import paho.mqtt.client as mqtt
from bleak import BleakScanner, BleakClient
import os
from dotenv import load_dotenv
# --- BLE Configuration ---
DEVICE_NAME = "Nano333" # Vaihdettu "Nano33" jos se oli aiemmin skannerissa näkynyt nimi
RX_UUID = "ed6629b5-3f81-4d4a-be67-e81d560fc69e" 
TX_UUID = "d3601b1c-013d-4fc2-9e65-ea619cf81131" 

# --- MQTT Configuration ---
MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883
MQTT_TOPIC = "proto_assignment/group24/data"

# --- Webhook Configuration ---
load_dotenv()
WEBHOOK_URL = os.getenv("DISCORD_WEBHOOK_URL")

if not WEBHOOK_URL:
    raise ValueError("DISCORD_WEBHOOK_URL is not set")

temp_history = collections.deque(maxlen=5) 

def trigger_actuation(current_temp):
    print(f">>> WEBHOOK TRIGGERED: Sending alert to Discord! Temp: {current_temp:.2f}°C")
    payload = {"content": f"🚨 **ALERT**: Room is occupied and temperature is spiking ({current_temp:.2f}°C). Alarm activated!"}
    try:
        # Lähetetään oikeasti Discordiin!
        requests.post(WEBHOOK_URL, json=payload) 
    except Exception as e:
        print(f"Failed to trigger webhook: {e}")

async def main():
    # Korjattu MQTT API varoitus!
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_start()
        print(f"Connected to public MQTT Broker: {MQTT_BROKER}")
    except Exception as e:
        print(f"MQTT connection failed: {e}. Continuing in local-only mode...")

    print(f"Scanning for '{DEVICE_NAME}'...")
    devices = await BleakScanner.discover()
    target_device = next((d for d in devices if d.name == DEVICE_NAME), None)
            
    if not target_device:
        print(f"Could not find '{DEVICE_NAME}'. Make sure the Arduino is powered on.")
        return

    print(f"Found {DEVICE_NAME} at {target_device.address}. Connecting...")

    async with BleakClient(target_device.address) as client:
        print("Connected successfully! Starting data loop...\n")
        
        try:
            while True:
                # --- A. READ SENSOR DATA ---
                raw_data = await client.read_gatt_char(RX_UUID)
                temperature, humidity, prox_float = struct.unpack('<fff', raw_data)
                proximity = int(prox_float) 
                
                is_occupied = proximity < 200 
                
                # --- B. DATA ANALYSIS ---
                temp_history.append(temperature)
                avg_temp = sum(temp_history) / len(temp_history)
                temp_is_rising = len(temp_history) == 5 and (temp_history[-1] - temp_history[0] > 0.5)

                print(f"Temp: {temperature:.1f}C (Avg: {avg_temp:.1f}C) | Humid: {humidity:.1f}% | Prox: {proximity} (Occupied: {is_occupied})")

                # --- C. PUBLISH TO MQTT ---
                mqtt_payload = f'{{"temp": {temperature:.2f}, "humidity": {humidity:.2f}, "occupied": {str(is_occupied).lower()}}}'
                mqtt_client.publish(MQTT_TOPIC, mqtt_payload)

                # --- D. ACTUATION & DECISION MAKING ---
                pwm_val = 0
                light_val = 0
                alarm_val = 0
                
                # 1. Vihreä valo päälle, jos joku on huoneessa
                if is_occupied:
                    light_val = 1
                
                # 2. Sininen "tuuletin", jos on liian lämmin
                if is_occupied and temperature > 24.0:
                    pwm_val = 150 
                    if temperature > 26.0:
                        pwm_val = 255 
                
                # 3. Punainen hälytysvalo ja Discord-viesti, jos lämpö nousee nopeasti
                if is_occupied and temp_is_rising:
                    alarm_val = 1
                    print(">>> ALERT: Temperature is spiking while room is occupied!")
                    trigger_actuation(temperature)

                control_bytes = bytearray([pwm_val, light_val, alarm_val])
                await client.write_gatt_char(TX_UUID, control_bytes)
                
                await asyncio.sleep(2) 
                
        except KeyboardInterrupt:
            print("\nShutting down gracefully...")
            mqtt_client.loop_stop()

if __name__ == "__main__":
    asyncio.run(main())
