#!/usr/bin/env python3
"""Generate the strategy and selection figures from retained result records."""

from __future__ import annotations

import re
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[2]
FIGURES = ROOT / "paper" / "figures"
OPTLOG = (ROOT / "results" / "OPTLOG.md").read_text(encoding="utf-8")
ALLPAIRS = (ROOT / "results" / "ALLPAIRS.md").read_text(encoding="utf-8")
ABLATION = (ROOT / "results" / "corpora" / "ABLATION.md").read_text(encoding="utf-8")

BLUE = "#4477AA"
GREEN = "#228833"
MAGENTA = "#AA3377"
GREY = "#BBBBBB"
DARK = "#222222"
RED = "#CC6677"

plt.rcParams.update(
    {
        "font.family": "DejaVu Sans",
        "font.size": 8,
        "axes.labelsize": 8,
        "xtick.labelsize": 7,
        "ytick.labelsize": 7,
        "legend.fontsize": 7,
        "axes.linewidth": 0.7,
        "pdf.fonttype": 42,
        "ps.fonttype": 42,
        "svg.fonttype": "none",
        "figure.facecolor": "white",
        "savefig.facecolor": "white",
        "savefig.bbox": "tight",
        "savefig.pad_inches": 0.02,
    }
)


def strip_markup(value: str) -> str:
    return value.replace("**", "").replace("FAIL", "").replace("PASS", "").strip()


def number(value: str) -> float:
    value = strip_markup(value).replace("%", "").replace("×", "").removesuffix("x")
    return float(value)


def markdown_rows(text: str, heading: str) -> list[list[str]]:
    lines = text.splitlines()
    start = next(i for i, line in enumerate(lines) if heading in line)
    rows: list[list[str]] = []
    for line in lines[start + 2 :]:
        if not line.startswith("|"):
            break
        rows.append([part.strip() for part in line.strip("|").split("|")])
    return rows


def parse_arrow(value: str) -> tuple[float, float]:
    left, right = strip_markup(value).split("→")
    return number(left), number(right)


def panel_label(ax: plt.Axes, label: str) -> None:
    ax.text(-0.12, 1.05, label, transform=ax.transAxes, fontweight="bold", fontsize=9)


def save(fig: plt.Figure, stem: str) -> None:
    FIGURES.mkdir(parents=True, exist_ok=True)
    fig.savefig(FIGURES / f"{stem}.pdf", bbox_inches="tight", pad_inches=0.02)
    fig.savefig(FIGURES / f"{stem}.svg", bbox_inches="tight", pad_inches=0.02)
    plt.close(fig)


def generate_strategy_figure() -> None:
    match = re.search(r"SIMD-throughput kernel wins (\d+) of (\d+) asymmetric", ALLPAIRS)
    if not match:
        raise ValueError("cannot find the asymmetric strategy result")
    simd, total = map(int, match.groups())
    avoidance = total - simd

    residency = markdown_rows(OPTLOG, "| host | SIMD | zone map |")
    hosts = [row[0] for row in residency]
    simd_paths = [parse_arrow(row[1]) for row in residency]
    avoidance_paths = [parse_arrow(row[2]) for row in residency]

    fig, axes = plt.subplots(
        1, 2, figsize=(7.25, 2.45), gridspec_kw={"width_ratios": [0.9, 2.1]}
    )

    ax = axes[0]
    ax.barh([1, 0], [avoidance, simd], color=[BLUE, GREY], height=0.56)
    ax.set_yticks([1, 0], ["work avoidance", "same-traversal SIMD"])
    ax.set_xlabel("asymmetric cell–shape wins")
    ax.set_xlim(0, total * 1.12)
    ax.set_xticks([0, 5, 10, 15, 20, 25])
    for y, value in zip([1, 0], [avoidance, simd]):
        ax.text(value + 0.5, y, str(value), va="center", fontweight="bold")
    ax.spines[["top", "right"]].set_visible(False)
    panel_label(ax, "a")

    ax = axes[1]
    colors = [BLUE, GREEN, MAGENTA]
    x = np.array([0, 1])
    for host, color, simd_line, avoidance_line in zip(
        hosts, colors, simd_paths, avoidance_paths
    ):
        ax.plot(x, simd_line, color=color, marker="o", linewidth=1.5, label=host)
        ax.plot(x, avoidance_line, color=color, marker="s", linewidth=1.5)
    ax.axhline(1, color=DARK, linewidth=0.8, linestyle="--")
    ax.set_yscale("log")
    ax.set_xticks(x, ["1 MB\nL2-resident", "48 MB\nDRAM-resident"])
    ax.set_ylabel("speedup over scalar (×)")
    ax.set_xlim(-0.12, 1.12)
    ax.grid(axis="y", which="both", color="#E6E6E6", linewidth=0.6)
    ax.legend(
        loc="upper center",
        bbox_to_anchor=(0.5, 1.18),
        frameon=False,
        ncol=3,
        columnspacing=1.2,
        handlelength=1.4,
    )
    ax.spines[["top", "right"]].set_visible(False)
    panel_label(ax, "b")

    fig.subplots_adjust(wspace=0.42)
    save(fig, "fig3_strategy")


def generate_selection_figure() -> None:
    selection = markdown_rows(OPTLOG, "| host | selection %, per-pair")
    policies = ["per pair", "per tile", "probe"]
    selection_values = np.array(
        [[number(row[1]), number(row[2]), np.nan] for row in selection], dtype=float
    )
    probe = markdown_rows(OPTLOG, "| host | all-bitmap | per-tile (model)")
    probe_by_host = {row[0]: row for row in probe}
    for index, row in enumerate(selection):
        selection_values[index, 2] = number(probe_by_host[row[0]][4])

    speed_values = np.array(
        [[number(row[2]), number(row[3])] for row in probe], dtype=float
    )
    host_labels = {
        "apple-m4": "Apple M4",
        "neoverse-sve2": "Neoverse SVE2",
        "sapphire-rapids": "Sapphire Rapids",
        "sapphire": "Sapphire Rapids",
    }
    hosts = [host_labels.get(row[0], row[0]) for row in selection]

    ablation_rows = markdown_rows(ABLATION, "| corpus | density | winning cell")
    densities = np.array([float(strip_markup(row[1])) for row in ablation_rows])
    gains = np.array([number(row[8]) for row in ablation_rows])
    is_sorted = np.array(["_srt" in row[0] for row in ablation_rows])

    fig, axes = plt.subplots(1, 3, figsize=(7.25, 2.45))
    colors = [BLUE, GREEN, MAGENTA]

    ax = axes[0]
    base = np.arange(len(policies))
    width = 0.23
    for offset, host, color, values in zip(
        (-width, 0, width), hosts, colors, selection_values
    ):
        ax.bar(base + offset, values, width=width, color=color, label=host)
    ax.axhline(2, color=RED, linewidth=0.9, linestyle="--", label="2% budget")
    ax.set_yscale("log")
    ax.set_xticks(base, policies)
    ax.set_ylabel("selection share of runtime (%)")
    ax.set_ylim(0.07, 80)
    ax.legend(frameon=False, loc="upper right")
    ax.grid(axis="y", which="both", color="#E6E6E6", linewidth=0.6)
    ax.spines[["top", "right"]].set_visible(False)
    panel_label(ax, "a")

    ax = axes[1]
    base = np.arange(len(hosts))
    ax.bar(
        base - 0.17, speed_values[:, 0], width=0.34, color=GREY,
        label="model-only tile"
    )
    ax.bar(
        base + 0.17, speed_values[:, 1], width=0.34, color=BLUE,
        label="probe-and-commit"
    )
    ax.axhline(1, color=DARK, linewidth=0.8, linestyle="--")
    ax.set_xticks(base, ["Apple\nM4", "Neoverse\nSVE2", "Sapphire\nRapids"])
    ax.set_ylabel("speedup over all-bitmap (×)")
    ax.set_ylim(0, 1.75)
    ax.legend(frameon=False, loc="upper right")
    ax.spines[["top", "right"]].set_visible(False)
    panel_label(ax, "b")

    ax = axes[2]
    ax.scatter(
        densities[~is_sorted], gains[~is_sorted], s=25, color=BLUE,
        label="original order"
    )
    ax.scatter(
        densities[is_sorted], gains[is_sorted], s=30, marker="s", color=MAGENTA,
        label="row-sorted"
    )
    ax.axhline(1, color=DARK, linewidth=0.8, linestyle="--")
    ax.set_xscale("log")
    ax.set_xlabel("global density")
    ax.set_ylabel("best-cell gain with gated index (×)")
    ax.set_ylim(0.85, 4.1)
    ax.legend(frameon=False, loc="upper left")
    ax.grid(axis="y", color="#E6E6E6", linewidth=0.6)
    ax.spines[["top", "right"]].set_visible(False)
    panel_label(ax, "c")

    fig.subplots_adjust(wspace=0.42)
    save(fig, "fig4_selection")


if __name__ == "__main__":
    generate_strategy_figure()
    generate_selection_figure()
