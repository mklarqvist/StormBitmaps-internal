#!/usr/bin/env python3
"""Generate manuscript benchmark-table rows from retained result artifacts."""

from __future__ import annotations

import csv
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TABLES = ROOT / "paper" / "tables"


def tex_identifier(value: str) -> str:
    return value.replace("_", r"\_")


def sci_integer(value: str) -> str:
    number = int(value)
    exponent = len(str(number)) - 1
    mantissa = number / (10**exponent)
    return rf"${mantissa:.2f}\mathord{{\times}}10^{{{exponent}}}$"


def dominant_route(cells: str) -> str:
    entries = re.findall(r"(B x [BSRW]|S x S|empty)=([0-9.]+)%", cells)
    if not entries:
        raise ValueError(f"cannot parse cell mixture: {cells!r}")
    name, share = max(entries, key=lambda item: float(item[1]))
    labels = {
        "B x B": r"\cell{\rB}{\rB}",
        "B x S": r"\cell{\rB}{\rS}",
        "B x R": r"\cell{\rB}{\rR}",
        "B x W": r"\cell{\rB}{\rW}",
        "S x S": r"\cell{\rS}{\rS}",
        "empty": r"span reject",
    }
    return rf"{labels[name]} ({share}\%)"


def generate_dynamic_rows() -> None:
    source = ROOT / "results" / "vs_roaring.csv"
    rows = []
    with source.open(newline="", encoding="utf-8") as handle:
        for record in csv.DictReader(handle):
            ratio = float(record["vs_roaring"])
            interval = (
                f'{ratio:.2f} [{float(record["vs_roaring_lo"]):.2f}--'
                f'{float(record["vs_roaring_hi"]):.2f}]'
            )
            if ratio > 1.0:
                interval = rf"\textbf{{{interval}}}"
            rows.append(
                " & ".join(
                    [
                        rf"\texttt{{\seqsplit{{{tex_identifier(record['corpus'])}}}}}",
                        tex_identifier(record["domain"]),
                        sci_integer(record["universe"]),
                        interval,
                        dominant_route(record["cells"]),
                    ]
                )
                + r" \\"
            )
    rows[-1] = rows[-1].removesuffix(r" \\")
    (TABLES / "tab_dynamic_corpora_rows.tex").write_text(
        "% Generated from results/vs_roaring.csv; do not edit by hand.\n"
        + "\n".join(rows)
        + "\n",
        encoding="utf-8",
    )


def format_time(value: str) -> str:
    number = float(value)
    if number >= 1000:
        return f"{number:,.0f}".replace(",", "{,}")
    if number >= 100:
        return f"{number:.1f}"
    return f"{number:.2f}"


def format_speedup(value: float) -> str:
    if value >= 1000:
        rendered = f"{value:,.0f}".replace(",", "{,}")
    elif value >= 100:
        rendered = f"{value:.0f}"
    elif value >= 10:
        rendered = f"{value:.1f}"
    else:
        rendered = f"{value:.2f}"
    return rf"\textbf{{{rendered}}}"


def generate_exact_rows() -> None:
    """Generate the exact-cardinality table from the two retained online runs."""
    source = ROOT / "results" / "vs_roaring.csv"
    no_map_source = ROOT / "results" / "vs_roaring_nozm.csv"
    with no_map_source.open(newline="", encoding="utf-8") as handle:
        no_map = {row["corpus"]: row for row in csv.DictReader(handle)}

    rows = []
    with source.open(newline="", encoding="utf-8") as handle:
        for record in csv.DictReader(handle):
            ab = float(record["allbitmap_ns"])
            selected = float(record["pertile_ns"])
            ab_no_map = float(no_map[record["corpus"]]["allbitmap_ns"])
            selected_no_map = float(no_map[record["corpus"]]["pertile_ns"])
            rows.append(
                " & ".join(
                    [
                        rf"\texttt{{\seqsplit{{{tex_identifier(record['corpus'])}}}}}",
                        tex_identifier(record["domain"]),
                        sci_integer(record["universe"]),
                        format_time(record["allbitmap_ns"]),
                        rf"\textbf{{{format_time(record['pertile_ns'])}}}",
                        format_speedup(ab / selected),
                        format_speedup(ab_no_map / selected_no_map),
                    ]
                )
                + r" \\"
            )
    rows[-1] = rows[-1].removesuffix(r" \\")
    (TABLES / "tab_exact_corpora_rows.tex").write_text(
        "% Generated from results/vs_roaring.csv and results/vs_roaring_nozm.csv; do not edit by hand.\n"
        + "\n".join(rows)
        + "\n",
        encoding="utf-8",
    )


def clean_markdown_cell(value: str) -> str:
    return value.strip().replace("**", "").replace("~", "")


def parse_markdown_table(lines: list[str], heading: str) -> list[list[str]]:
    start = next(i for i, line in enumerate(lines) if line.startswith(heading))
    table_lines = []
    in_table = False
    for line in lines[start + 1 :]:
        if line.startswith("|"):
            in_table = True
            if not re.match(r"^\|[-:| ]+\|$", line):
                table_lines.append([clean_markdown_cell(x) for x in line.strip("|\n").split("|")])
        elif in_table:
            break
    return table_lines[1:]


def numeric_cell(value: str) -> str:
    return value.removesuffix(" ns").strip().replace(" ", "{,}").replace("×", "")


def generate_allpairs_rows() -> None:
    source = ROOT / "results" / "ALLPAIRS.md"
    lines = source.read_text(encoding="utf-8").splitlines()
    groups = [
        ("### Filter path", "column postings", parse_markdown_table(lines, "### Filter path")),
        ("### Bypass path", r"\cell{\rB}{\rB} zone map", parse_markdown_table(lines, "### Bypass path")),
    ]
    output = ["% Generated from results/ALLPAIRS.md; do not edit by hand."]
    for group_index, (_, route, records) in enumerate(groups):
        if group_index:
            output.append(r"\midrule")
        for corpus, baseline, selected, speedup in records:
            output.append(
                " & ".join(
                    [
                        rf"\texttt{{\seqsplit{{{tex_identifier(corpus)}}}}}",
                        route,
                        numeric_cell(baseline),
                        rf"\textbf{{{numeric_cell(selected)}}}",
                        rf"\textbf{{{numeric_cell(speedup)}}}",
                    ]
                )
                + r" \\"
            )
    output[-1] = output[-1].removesuffix(r" \\")
    (TABLES / "tab_allpairs_rows.tex").write_text(
        "\n".join(output) + "\n", encoding="utf-8"
    )


def generate_threshold_rows() -> None:
    source = ROOT / "results" / "threshold" / "all_corpora_full.csv"
    by_corpus: dict[str, dict[str, str]] = {}
    order: list[str] = []
    with source.open(newline="", encoding="utf-8") as handle:
        for record in csv.DictReader(handle):
            corpus = record["corpus"]
            if corpus not in by_corpus:
                by_corpus[corpus] = {
                    "domain": record["domain"],
                    "N": record["N"],
                }
                order.append(corpus)
            by_corpus[corpus][record["t"]] = record["gated_speedup"]

    rows = []
    for corpus in order:
        record = by_corpus[corpus]
        values = []
        for threshold in ("0.001000", "0.010000", "0.100000"):
            speedup = float(record[threshold])
            values.append(format_speedup(speedup) if speedup > 1.0001 else "1.00")
        rows.append(
            " & ".join(
                [
                    rf"\texttt{{\seqsplit{{{tex_identifier(corpus)}}}}}",
                    tex_identifier(record["domain"]),
                    f"{int(record['N']):,}".replace(",", "{,}"),
                    *values,
                ]
            )
            + r" \\"
        )
    rows[-1] = rows[-1].removesuffix(r" \\")
    (TABLES / "tab_threshold_rows.tex").write_text(
        "% Generated from results/threshold/all_corpora_full.csv; do not edit by hand.\n"
        + "\n".join(rows)
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    TABLES.mkdir(parents=True, exist_ok=True)
    generate_exact_rows()
    generate_dynamic_rows()
    generate_allpairs_rows()
    generate_threshold_rows()
