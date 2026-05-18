import asyncio
import csv
import time
from bleak import BleakClient

DEVICE_ADDRESS = "00:4B:12:BD:6E:4A"
CHAR_UUID      = "12345678-1234-1234-1234-1234567890ac"
DURATION_S     = 600.0  # 10 Minutes Total Operation
CSV_PATH       = "power_aware_experiment.csv"

# Globals
latest_voltage = None
latest_current = None
latest_fan_state = None
latest_rx_time = None

def handle_notify(_, data):
    global latest_voltage, latest_current, latest_fan_state, latest_rx_time
    try:
        text = data.decode("utf-8").strip()
        parts = dict(p.split("=") for p in text.split(","))
        latest_voltage = float(parts["V"])
        latest_current = float(parts["I"])
        latest_fan_state = int(parts["F"])
        latest_rx_time = time.perf_counter()
    except Exception as e:
        print("Parse error:", e, "raw:", data)

async def main():
    global latest_voltage, latest_current, latest_fan_state
    client = BleakClient(DEVICE_ADDRESS)
    try:
        await client.connect()
        print("Connected:", client.is_connected)
        await client.start_notify(CHAR_UUID, handle_notify)
        
        print("\nWaiting for initial telemetry...")
        while latest_voltage is None:
            await asyncio.sleep(0.005)

        # Record Initial Metrics
        v_start = latest_voltage
        fan_on_ticks = 0
        total_ticks = 0
        
        # Track 10-second averages
        power_samples_10s = []
        next_report_time = 10.0

        print(f"\nLogging data for {DURATION_S/60:.1f} minutes to {CSV_PATH}...")
        print("\n" + "="*50)
        print("!!! TEST START: PLACE NODE IN DIRECT SUNLIGHT NOW !!!")
        print("="*50 + "\n")

        with open(CSV_PATH, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["t_s", "voltage_V", "current_mA", "power_mW", "fan_state"])

            t0 = time.perf_counter()
            next_tick = t0
            sun_phase_alert = False

            while True:
                now = time.perf_counter()
                t_elapsed = now - t0
                if t_elapsed >= DURATION_S:
                    break

                # Contextual prompts for the user
                if t_elapsed >= 300.0 and not sun_phase_alert:
                    print("\n" + "#"*50)
                    print("!!! 5 MINUTES ELAPSED: MOVE NODE TO DARKNESS NOW !!!")
                    print("#"*50 + "\n")
                    sun_phase_alert = True

                v = latest_voltage
                i = latest_current
                f_state = latest_fan_state
                p = v * i  # mW
                
                # Metrics tracking
                total_ticks += 1
                if f_state == 1:
                    fan_on_ticks += 1
                power_samples_10s.append(p)

                # Write raw 20ms data to CSV
                w.writerow([f"{t_elapsed:.3f}", f"{v:.4f}", f"{i:.4f}", f"{p:.4f}", f"{f_state}"])

                # Handle 10-second window terminal displays
                if t_elapsed >= next_report_time:
                    avg_p = sum(power_samples_10s) / len(power_samples_10s) if power_samples_10s else 0
                    print(f"[10s Window Report] Time: {t_elapsed-10:5.1f}s to {t_elapsed:5.1f}s | Avg Power: {avg_p:8.2f} mW | Fan State: {f_state}")
                    power_samples_10s = []
                    next_report_time += 10.0

                # Schedule next tight 20 ms loop iteration
                next_tick += 0.020
                sleep_for = next_tick - time.perf_counter()
                if sleep_for > 0:
                    await asyncio.sleep(sleep_for)

        # Final Summary Calculations for Deliverable 14
        v_end = latest_voltage
        uptime_pct = (fan_on_ticks / total_ticks) * 100
        success = "YES" if v_end >= v_start else "NO"

        print("\n" + "="*50)
        print("EXPERIMENT COMPLETE - RESULTS FOR DELIVERABLE 14")
        print("="*50)
        print(f"Initial Battery Voltage (t=0s):  {v_start:.3f} V")
        print(f"Final Battery Voltage (t=600s):  {v_end:.3f} V")
        print(f"Succeeded in Net-Positive Power: {success} (Target: V_end >= V_start)")
        print(f"Total Fan Uptime Percentage:     {uptime_pct:.2f} %")
        print("="*50 + "\n")

    finally:
        if client.is_connected:
            try:
                await client.stop_notify(CHAR_UUID)
            except Exception as e:
                print("stop_notify error:", e)
            await client.disconnect()
            print("Disconnected cleanly.")

try:
    asyncio.run(main())
except KeyboardInterrupt:
    print("Stopped by user")