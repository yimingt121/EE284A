import asyncio
import csv
import time
from bleak import BleakClient

DEVICE_ADDRESS = "00:4B:12:BD:6E:4A"
CHAR_UUID      = "12345678-1234-1234-1234-1234567890ac"
DURATION_S     = 60.0
CSV_PATH       = "battery_log.csv"   # change to plugged.csv / unplugged.csv per run

# Globals updated by the notify callback; main loop samples them every 20 ms.
latest_voltage = None
latest_current = None
latest_rx_time = None

def handle_notify(_, data):
    """Parse 'V=3.909,I=-192.300' payloads from the ESP32."""
    global latest_voltage, latest_current, latest_rx_time
    try:
        text = data.decode("utf-8").strip()
        parts = dict(p.split("=") for p in text.split(","))
        latest_voltage = float(parts["V"])
        latest_current = float(parts["I"])
        latest_rx_time = time.perf_counter()
    except Exception as e:
        print("parse error:", e, "raw:", data)

async def main():
    client = BleakClient(DEVICE_ADDRESS)
    try:
        await client.connect()
        print("Connected:", client.is_connected)
        await client.start_notify(CHAR_UUID, handle_notify)
        print(f"Logging for {DURATION_S:.0f} s to {CSV_PATH} ...")

        # Wait for the first packet so we don't write Nones at t=0.
        while latest_voltage is None:
            await asyncio.sleep(0.005)

        with open(CSV_PATH, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["t_s", "voltage_V", "current_mA", "power_mW"])

            t0 = time.perf_counter()
            next_tick = t0
            while True:
                now = time.perf_counter()
                t_elapsed = now - t0
                if t_elapsed >= DURATION_S:
                    break

                v = latest_voltage
                i = latest_current
                p = v * i                       # V * mA = mW
                print(f"t={t_elapsed:6.2f}s  V={v:.3f} V  I={i:8.2f} mA  P={p:8.2f} mW")
                w.writerow([f"{t_elapsed:.3f}",
                            f"{v:.4f}", f"{i:.4f}", f"{p:.4f}"])

                # Schedule next 20 ms sample (drift-corrected).
                next_tick += 0.020
                sleep_for = next_tick - time.perf_counter()
                if sleep_for > 0:
                    await asyncio.sleep(sleep_for)

        print(f"Done. Wrote {CSV_PATH}")
    finally:
        if client.is_connected:
            try:
                await client.stop_notify(CHAR_UUID)
            except Exception as e:
                print("stop_notify error:", e)
            await client.disconnect()
            print("Disconnected cleanly")

try:
    asyncio.run(main())
except KeyboardInterrupt:
    print("Stopped by user")