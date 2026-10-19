#!/usr/bin/env python3
"""Cost roll-up for the hexaservo42 closed-loop stepper driver.

Reads bom.csv, looks every LCSC part up in the public JLCPCB parts mirror
(jlcsearch.tscircuit.com), picks the price tier for the order quantity and
adds JLCPCB fabrication / assembly fees for the chosen scenario.

    python3 cost_model.py                # 100 boards, uses prices.json cache
    python3 cost_model.py --qty 250
    python3 cost_model.py --scenario economic --summary
    python3 cost_model.py --refresh      # re-fetch prices from the API

Prices are a snapshot: re-run with --refresh before ordering.
"""
import argparse
import csv
import json
import math
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
CACHE = HERE / "prices.json"
API = "https://jlcsearch.tscircuit.com/components/list.json?search="

# JLCPCB fees in USD, from jlcpcb.com/help/article/pcb-assembly-price and the
# live quote endpoints on 2026-09-21. A 42 mm board is below the 70x70 mm
# minimum for Standard PCBA and 4-layer Economic orders are capped at 50 pcs,
# so both scenarios order 25 panels of 2x2 boards.
SCENARIOS = {
    # Everything populated, encoder on the bottom -> double-sided Standard PCBA
    "standard": {
        "label": "Standard PCBA, double-sided, fully populated",
        "pcb_100": {"hasl": 55.40, "enig": 70.60},
        "fixed": {
            "Setup (double-sided)": 51.12,
            "Stencils (2)": 16.42,
            "Panel fee": 8.21,
            "THT soldering fixtures (2 @ 25 panels)": 16.42,
            "Packing + confirm-placement": 1.10,
        },
        "feeder_fee_all_parts": 1.53,   # per unique part, basic or extended
        "extended_part_fee": 0.0,
        "skip_bottom": False,
    },
    # Bottom-side encoder left off (hand-solder 1x SOP-8 per board afterwards)
    "economic": {
        "label": "Economic PCBA, top side only, encoder hand-soldered later",
        "pcb_100": {"hasl": 56.50, "enig": 71.90},
        "fixed": {
            "Setup": 8.18,
            "Stencil": 1.53,
            "Panel fee": 8.21,
            "Confirm-placement": 0.45,
        },
        "feeder_fee_all_parts": 0.0,
        "extended_part_fee": 3.07,      # per unique extended part; basic/preferred free
        "skip_bottom": True,
    },
}
SMT_JOINT = 0.0016
THT_JOINT = 0.0164
THT_LABOUR = 3.58
PCB_ENGINEERING = 25.00     # fixed part of the bare-PCB quote, rest scales with area

# US landed-cost assumptions (volatile: re-check before ordering)
SHIPPING_US = 45.00         # UPS/DHL express DDP, ~2-2.5 kg
DUTY_RATE = 0.35            # JLCPCB flat prepaid DDP rate on order value, Sep 2026

# Items that are not on the PCBA order but needed per finished unit
KIT = [
    ("Diametric magnet 6x2.5 mm N35/N42", 0.15),
    ("4x M3 standoff/spacer + 4x longer M3 motor screws", 0.35),
    ("Mating housings + crimps (2x MX3.0 2x2, XH-4, PH-4)", 0.40),
]


def attrition_pcs(pads):
    """Extra pieces JLCPCB bills per BOM line (live parts API, Sep 2026)."""
    if pads <= 2:
        return 10       # chip passives, diodes, LEDs
    if pads <= 6:
        return 4        # SOT-23 class
    return 0            # ICs and connectors: 0-1


def fetch(lcsc):
    url = API + urllib.parse.quote(lcsc)
    # the mirror rejects urllib's default User-Agent
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8.5.0"})
    with urllib.request.urlopen(req, timeout=40) as r:
        comps = json.load(r)["components"]
    want = int(lcsc.lstrip("C"))
    for c in comps:
        if c["lcsc"] == want:
            return {
                "mfr": c["mfr"],
                "stock": c["stock"],
                "price": c["price"],
                "basic": bool(c["is_basic"]),
                "preferred": bool(c["is_preferred"]),
                "package": c["package"],
            }
    return None


def tier_price(price_str, n):
    """price_str like '1-49:0.2,50-149:0.16,150-:0.14' -> unit price at n pcs."""
    best = None
    for t in price_str.split(","):
        rng, p = t.split(":")
        lo, _, hi = rng.partition("-")
        lo = int(lo)
        hi = int(hi) if hi else math.inf
        if lo <= n <= hi:
            return float(p)
        if n >= lo:
            best = float(p)
    return best if best is not None else float(price_str.split(",")[0].split(":")[1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qty", type=int, default=100)
    ap.add_argument("--refresh", action="store_true", help="re-fetch prices from the API")
    ap.add_argument("--scenario", choices=sorted(SCENARIOS), default="standard")
    ap.add_argument("--finish", choices=["hasl", "enig"], default="hasl")
    ap.add_argument("--summary", action="store_true", help="skip the per-line BOM table")
    args = ap.parse_args()
    n = args.qty
    sc = SCENARIOS[args.scenario]

    cache = json.loads(CACHE.read_text()) if CACHE.exists() and not args.refresh else {}
    rows = list(csv.DictReader(open(HERE / "bom.csv")))

    for r in rows:
        if r.get("price_override"):
            # Part not in the cached price list: use the BOM's own estimate.
            cache.setdefault(r["lcsc"], {"mfr": r["mfr_part"], "stock": 10**6, "price": f"1-:{float(r['price_override'])}",
                                         "basic": False, "preferred": False, "package": "", "estimate": True})
            continue
        if r["lcsc"] not in cache:
            info = fetch(r["lcsc"])
            if info is None:
                print(f"!! {r['lcsc']} ({r['mfr_part']}) not found", file=sys.stderr)
                continue
            cache[r["lcsc"]] = info
            time.sleep(0.2)
    CACHE.write_text(json.dumps(cache, indent=1, sort_keys=True))

    # quantities per unique LCSC part (several BOM lines may share a reel)
    per_part_qty = {}
    for r in rows:
        per_part_qty[r["lcsc"]] = per_part_qty.get(r["lcsc"], 0) + int(r["qty"])

    parts_per_board = 0.0
    hand_fit_per_board = 0.0
    attrition = 0.0
    smt_joints = tht_joints = placements = 0
    unique, extended = set(), set()
    lines, warnings = [], []
    for r in rows:
        info = cache.get(r["lcsc"])
        q = int(r["qty"])
        if info is None:
            warnings.append(f"{r['lcsc']} missing from price cache")
            continue
        if info["mfr"].replace(" ", "").lower() != r["mfr_part"].replace(" ", "").lower():
            warnings.append(f"{r['lcsc']}: BOM says {r['mfr_part']!r}, JLC says {info['mfr']!r}")
        need = per_part_qty[r["lcsc"]] * n
        if need > info["stock"]:
            warnings.append(f"{r['lcsc']} {r['mfr_part']}: need {need}, JLCPCB stock {info['stock']}")
        unit = tier_price(info["price"], need)
        cls = "est" if info.get("estimate") else "basic" if info["basic"] else "pref" if info["preferred"] else "ext"
        ext_cost = unit * q
        if sc["skip_bottom"] and r["side"] == "bottom":
            hand_fit_per_board += ext_cost
            lines.append((r["refs"], q, r["value"], r["mfr_part"], r["lcsc"], "hand-fit", unit, ext_cost))
            continue
        parts_per_board += ext_cost
        placements += q
        if r["lcsc"] not in unique:
            attrition += attrition_pcs(int(r["pads"])) * unit
        unique.add(r["lcsc"])
        if cls in ("ext", "est"):
            extended.add(r["lcsc"])
        if r["mount"] == "tht":
            tht_joints += int(r["pads"]) * q
        else:
            smt_joints += int(r["pads"]) * q
        lines.append((r["refs"], q, r["value"], r["mfr_part"], r["lcsc"], cls, unit, ext_cost))

    pcb100 = sc["pcb_100"][args.finish]
    pcb = PCB_ENGINEERING + (pcb100 - PCB_ENGINEERING) * n / 100
    fixed = dict(sc["fixed"])
    feeder = len(unique) * sc["feeder_fee_all_parts"] + len(extended) * sc["extended_part_fee"]
    smt = smt_joints * n * SMT_JOINT
    tht = tht_joints * n * THT_JOINT + (THT_LABOUR if tht_joints else 0)
    kit = sum(c for _, c in KIT) + hand_fit_per_board

    print(f"# valve-node (TMC2209 + MT6701 + STM32G030 + RS-485) cost roll-up: {n} boards, {sc['label']}, {args.finish.upper()}\n")
    if not args.summary:
        print("| Refs | Qty | Value | Part | LCSC | Class | Unit $ | Per board $ |")
        print("|---|---:|---|---|---|---|---:|---:|")
        for refs, q, val, mpn, lcsc, cls, unit, ext in lines:
            rl = refs.split()
            short = refs if len(rl) <= 3 else f"{rl[0]}..{rl[-1]}"
            print(f"| {short} | {q} | {val} | {mpn} | {lcsc} | {cls} | {unit:.4f} | {ext:.3f} |")
        print()
    print(f"Per board: {placements} placements, {len(unique)} unique parts "
          f"({len(extended)} extended), {smt_joints} SMT joints, {tht_joints} THT joints\n")

    table = [("Components (JLCPCB parts-library prices)", parts_per_board * n),
             ("Attrition (extra pieces billed per BOM line)", attrition),
             (f"Bare PCB, 4-layer 42x42 mm as 2x2 panels, {args.finish.upper()} (2-layer would be ~40% less)", pcb)]
    table += list(fixed.items())
    if sc["feeder_fee_all_parts"]:
        table.append((f"Feeder loading ({len(unique)} parts x ${sc['feeder_fee_all_parts']})", feeder))
    else:
        table.append((f"Extended-part fees ({len(extended)} x ${sc['extended_part_fee']})", feeder))
    table += [(f"SMT placement ({smt_joints * n:,} joints)", smt),
              (f"Through-hole soldering ({tht_joints * n:,} joints)", tht)]
    sub = sum(v for _, v in table)
    duty = sub * DUTY_RATE

    print("| Line item | Order $ | Per board $ |")
    print("|---|---:|---:|")
    for k, v in table:
        print(f"| {k} | {v:,.2f} | {v / n:.2f} |")
    print(f"| **JLCPCB order subtotal** | **{sub:,.2f}** | **{sub / n:.2f}** |")
    print(f"| Express shipping to US (DDP) | {SHIPPING_US:,.2f} | {SHIPPING_US / n:.2f} |")
    print(f"| US duty, prepaid at {DUTY_RATE:.0%} of order value | {duty:,.2f} | {duty / n:.2f} |")
    landed = sub + SHIPPING_US + duty
    print(f"| **Landed PCBA (US)** | **{landed:,.2f}** | **{landed / n:.2f}** |")
    for name, c in KIT:
        print(f"| Kit: {name} | {c * n:,.2f} | {c:.2f} |")
    if hand_fit_per_board:
        print(f"| Kit: bottom-side parts bought separately, hand-soldered | "
              f"{hand_fit_per_board * n:,.2f} | {hand_fit_per_board:.2f} |")
    print(f"| **Complete unit, landed (US)** | **{landed + kit * n:,.2f}** | **{landed / n + kit:.2f}** |")
    print(f"| Complete unit, no duty/shipping (non-US reference) | {sub + kit * n:,.2f} | {sub / n + kit:.2f} |")
    if warnings:
        print("\nWarnings:")
        for w in warnings:
            print(" -", w)


if __name__ == "__main__":
    main()
