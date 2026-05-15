import csv
import statistics
import matplotlib.pyplot as plt

def load(path):
    t, v, i, p = [], [], [], []
    with open(path) as f:
        r = csv.DictReader(f)
        for row in r:
            t.append(float(row["t_s"]))
            v.append(float(row["voltage_V"]))
            i.append(float(row["current_mA"]))
            p.append(float(row["power_mW"]))
    return t, v, i, p

def summary(name, t, v, i, p):
    print(f"--- {name} ---")
    print(f"  samples       : {len(t)}")
    print(f"  avg voltage   : {statistics.mean(v):8.4f} V")
    print(f"  avg current   : {statistics.mean(i):8.3f} mA")
    print(f"  avg power     : {statistics.mean(p):8.3f} mW   "
          f"(= {statistics.mean(v) * statistics.mean(i):.3f} mW from avg V * avg I)")
    print(f"  min/max power : {min(p):.3f} / {max(p):.3f} mW")

# ---- Load both runs (skip the plugged block if you only have one) ----
t_u, v_u, i_u, p_u = load("unplugged.csv")
summary("Unplugged", t_u, v_u, i_u, p_u)

try:
    t_p, v_p, i_p, p_p = load("plugged.csv")
    summary("Plugged",   t_p, v_p, i_p, p_p)
except FileNotFoundError:
    pass

# ---- 10-second bin averages (6 points for a 60-s run) ----
BIN = 10.0
bin_centers, bin_means = [], []
for k in range(6):
    lo, hi = k * BIN, (k + 1) * BIN
    chunk = [p for tt, p in zip(t_u, p_u) if lo <= tt < hi]
    if chunk:
        bin_centers.append(lo + BIN / 2)
        bin_means.append(statistics.mean(chunk))

# ---- Plot ----
fig, ax = plt.subplots(figsize=(10, 5))
ax.plot(t_u, p_u, lw=0.7, alpha=0.6, label="Power every 20 ms")
ax.plot(bin_centers, bin_means, "o-", ms=8, lw=2, label="10-s average")
ax.set_xlabel("Time (s)")
ax.set_ylabel("Power (mW)")
ax.set_title("ESP32 battery power — unplugged from computer")
ax.legend()
ax.grid(alpha=0.3)
plt.tight_layout()
plt.savefig("power_unplugged.png", dpi=150)
plt.show()