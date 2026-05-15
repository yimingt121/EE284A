import pandas as pd

df = pd.read_csv("battery_log.csv")

# Print what phases were actually seen
print("Phases in data:", df["phase"].unique())

# Trim first 1 second of each phase to skip transients
df["t_in_phase"] = df.groupby("phase")["t_s"].transform(lambda x: x - x.min())
df_clean = df[df["t_in_phase"] >= 1.0]

# Print results for the duty cycles we care about
print("\n=== Average Power by Duty Cycle ===")
for phase in ["25", "50", "100"]:
    sub = df_clean[df_clean["phase"] == phase]
    if len(sub) == 0:
        print(f"Duty cycle = {phase}%: NO DATA")
    else:
        print(f"Duty cycle = {phase}%: average power = {sub['power_mW'].mean():.2f} mW  (n={len(sub)})")

# Also show OFF (MCU baseline with fan wired in but not switching)
off = df_clean[df_clean["phase"] == "OFF"]
if len(off) > 0:
    print(f"Fan OFF (MCU baseline): average power = {off['power_mW'].mean():.2f} mW  (n={len(off)})")