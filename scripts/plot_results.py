#!/usr/bin/env python3
"""Regenerate the figures in docs/figures/ from the CSV files in data/.

Usage (from the repository root):
    pip install matplotlib
    python3 scripts/plot_results.py
"""
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
OUT = ROOT / "docs" / "figures"

# Validated categorical palette (lightness band, chroma, colour-blind separation).
BLUE, ORANGE, GREEN = "#2A78D6", "#EB6834", "#1BAF7A"
INK, INK2, GRID, BG = "#0B0B0B", "#52514E", "#E4E3DF", "#FFFFFF"
OPS = [("keygen_us", "KeyGen", BLUE), ("encaps_us", "Encaps", ORANGE),
       ("decaps_us", "Decaps", GREEN)]


def read(name):
    with open(DATA / name, newline="") as f:
        return list(csv.DictReader(f))


def style(ax, ylabel, title):
    ax.set_facecolor(BG)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(INK2)
    ax.tick_params(colors=INK2, labelsize=9)
    ax.yaxis.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    ax.set_ylabel(ylabel, color=INK2, fontsize=10)
    ax.set_title(title, color=INK, fontsize=11, loc="left", pad=22)


def legend(ax, ncol=3):
    ax.legend(frameon=False, fontsize=9, ncol=ncol, loc="lower left",
              bbox_to_anchor=(0, 1.0), handlelength=1.2)


def bar_labels(ax, bars, fmt="{:.1f}"):
    for b in bars:
        ax.annotate(fmt.format(b.get_height()), (b.get_x() + b.get_width() / 2, b.get_height()),
                    xytext=(0, 2), textcoords="offset points", ha="center", va="bottom",
                    fontsize=7.5, color=INK2)


def save(fig, name):
    fig.patch.set_facecolor(BG)  # opaque background: readable in GitHub dark mode too
    fig.tight_layout()
    fig.savefig(OUT / name, dpi=160, facecolor=BG)
    plt.close(fig)
    print("wrote", OUT / name)


def fig_levels(rows):
    sel = {r["parameter_set"]: r for r in rows
           if r["platform"] == "esp32s3" and r["cpu_mhz"] == "240"
           and r["opt_level"] == "O2" and r["run"] != "6"}
    levels = ["512", "768", "1024"]
    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    w = 0.26
    for i, (key, label, color) in enumerate(OPS):
        xs = [j + (i - 1) * w for j in range(3)]
        ys = [float(sel[l][key]) / 1000 for l in levels]
        bars = ax.bar(xs, ys, w * 0.92, color=color, label=label)
        bar_labels(ax, bars)
    ax.set_xticks(range(3), [f"ML-KEM-{l}" for l in levels])
    style(ax, "Latency [ms]", "Latency per operation (ESP32-S3, 240 MHz, -O2)")
    legend(ax)
    save(fig, "latency_by_level.png")


def fig_freq(rows):
    pts = {int(r["cpu_mhz"]): r for r in rows
           if r["platform"] == "esp32s3" and r["parameter_set"] == "768"
           and r["opt_level"] == "O2" and r["run"] != "6"}
    freqs = [240, 160, 80]
    period = [1000 / f for f in freqs]
    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    for key, label, color in [OPS[2], OPS[0]]:
        meas = [float(pts[f][key]) / 1000 for f in freqs]
        ideal_slope = meas[0] / period[0]
        ax.plot([0, 13], [0, 13 * ideal_slope], "--", color=color, linewidth=1.2)
        ax.plot(period, meas, "-o", color=color, linewidth=2, markersize=6,
                markeredgecolor="white", markeredgewidth=1.2, label=f"{label} (measured)")
    ax.set_xlim(0, 13.5)
    ax.set_ylim(0, 27)
    ax.set_xticks(period, [f"{p:.2f} ns\n{f} MHz" for p, f in zip(period, freqs)])
    ax.text(7.0, 5.0, "dashed: ideal t ∝ 1/f", fontsize=8, color=INK2)
    style(ax, "Latency [ms]", "ML-KEM-768 latency vs. CPU clock period (-O2)")
    legend(ax, ncol=2)
    save(fig, "clock_scaling.png")


def fig_opt(rows):
    sel = {r["opt_level"]: r for r in rows
           if r["platform"] == "esp32s3" and r["parameter_set"] == "768"
           and r["cpu_mhz"] == "240" and r["run"] != "6"}
    fig, ax = plt.subplots(figsize=(6.4, 3.4))
    w = 0.36
    for i, (lvl, color) in enumerate([("O2", BLUE), ("Os", ORANGE)]):
        xs = [j + (i - 0.5) * w for j in range(3)]
        ys = [float(sel[lvl][k]) / 1000 for k, _, _ in OPS]
        bars = ax.bar(xs, ys, w * 0.92, color=color, label=f"-{lvl}")
        bar_labels(ax, bars, "{:.2f}")
    ax.set_xticks(range(3), [label for _, label, _ in OPS])
    style(ax, "Latency [ms]", "ML-KEM-768: -O2 vs -Os (240 MHz) — lower is better")
    legend(ax, ncol=2)
    save(fig, "optimisation_level.png")


def fig_dual(rows):
    labels = ["One task\n(CPU 1)", "Two tasks\n(CPU 0 + CPU 1)", "Two tasks\n(both on CPU 1)"]
    ys = [float(r["exchanges_per_s"]) for r in rows]
    fig, ax = plt.subplots(figsize=(6.4, 3.4))
    bars = ax.bar(range(3), ys, 0.55, color=BLUE)
    bar_labels(ax, bars)
    ideal = 2 * ys[0]
    ax.axhline(ideal, color=INK2, linestyle="--", linewidth=1)
    ax.text(2.3, ideal + 1.5, "ideal 2×", fontsize=8, color=INK2, ha="right")
    ax.set_xticks(range(3), labels)
    ax.set_ylim(0, ideal * 1.15)
    style(ax, "Exchanges per second", "Dual-core throughput, ML-KEM-768 (240 MHz, -O2)")
    save(fig, "dualcore_throughput.png")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = read("results.csv")
    fig_levels(rows)
    fig_freq(rows)
    fig_opt(rows)
    fig_dual(read("dualcore.csv"))


if __name__ == "__main__":
    main()
