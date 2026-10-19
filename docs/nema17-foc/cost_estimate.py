#!/usr/bin/env python3
"""Reproduce the 100-board planning estimate; no vendor quotation is implied.

Run: python3 docs/nema17-foc/cost_estimate.py
Prices checked 2026-09-21. Decimal arithmetic; USD excluding tax and NRE.
Changing quantity requires refreshing price breaks, fixtures and inspection fees.
"""

import csv
from decimal import Decimal as D
from pathlib import Path

ROOT = Path(__file__).resolve().parent
QTY = D(100)
RATE_SOURCE = "https://jlcpcb.com/help/article/pcb-assembly-price"


def bom_sum(rows, groups):
    return sum(
        (D(row["quantity_per_board"]) * D(row["unit_usd"])
         for row in rows if row["group"] in groups), D(0)
    )


def estimate(rows, variant):
    parts = bom_sum(rows, {"electronics", variant})
    # Two-sided Standard assembly, estimated 50 distinct fitted part types.
    # Bottom encoder and local bypass capacitor are included in joint count.
    # 330 is a planning count, not an export from an existing routed PCB.
    assembly = {
        "Assembly setup (two sides)": D("51.12"),
        "Stencil (two sides)": D("16.42"),
        "Feeder loading (50 types)": D(50) * D("1.53"),
        "SMT joints (330 per board)": QTY * D(330) * D("0.0016"),
        "THT joints (16 per board)": QTY * D(16) * D("0.0164"),
        "Hand solder labor per order": D("3.58"),
        "Rigid-board fixtures (100-199 tier)": D("41.04"),
        # Conservative allowance if both driver thermal pads are inspected.
        "X-ray allowance (200 packages)": QTY * D(2) * D("0.49"),
        "Packing and handling allowance": D("10.00"),
    }
    # 330 * .0016 = .528, already above the .48/board SMT minimum.
    assert D(330) * D("0.0016") >= D("0.48")
    ledger = [
        ("Electronic BOM", parts),
        ("Parts attrition/procurement allowance (3%)", parts * D("0.03")),
        ("4-layer PCB fabrication allowance", D("1.50")),
    ]
    ledger += [(name, cost / QTY) for name, cost in assembly.items()]
    assembled = sum((cost for _, cost in ledger), D(0))
    ledger += [
        ("Magnet and mounting hardware", bom_sum(rows, {"mechanical"})),
        # 3 minutes per board at $30/h, after fixture/software development.
        ("Program mount calibrate and functional test allowance", D("1.50")),
        # Rework reserve is separate from purchasing feeder-loss spare parts.
        ("Rework reserve (2% of assembled board cost)", assembled * D("0.02")),
        ("Shipping allowance (destination unspecified)", D("1.00")),
    ]
    delivered = sum((cost for _, cost in ledger), D(0))
    return ledger, assembled, delivered


def main():
    with (ROOT / "bom.csv").open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    output = []
    for variant in ("can", "rs485"):
        ledger, assembled, delivered = estimate(rows, variant)
        print(f"{variant.upper()}: 100 boards, USD, excluding tax and NRE")
        for name, per_board in ledger:
            output.append([variant, name, f"{per_board:.4f}", f"{per_board * QTY:.2f}"])
        for name, value in (("ASSEMBLED BOARD SUBTOTAL", assembled),
                            ("TESTED KIT DELIVERED TOTAL", delivered)):
            output.append([variant, name, f"{value:.4f}", f"{value * QTY:.2f}"])
            print(f"  {name}: ${value:.2f}/board; ${value * QTY:.2f}/lot")
        print("  Add $2-5/node for a local power brake resistor if required.")
    with (ROOT / "cost_summary.csv").open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["variant", "item", "usd_per_board", "usd_per_100"])
        writer.writerows(output)


if __name__ == "__main__":
    main()
