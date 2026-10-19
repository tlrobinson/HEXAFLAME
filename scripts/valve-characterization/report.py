#!/usr/bin/env python3
"""Build the valve characterization web page from the JSONL logs in data/.

Usage: report.py [--out report/valve-report.html] [data/*.jsonl ...]
With no files, every log in data/ is used, in time order.
"""
from __future__ import annotations

import argparse
import glob
import json
import statistics
import time
from pathlib import Path

HERE = Path(__file__).parent
STALL_REASONS = {"stall", "blocked", "no_motion"}


def load(paths: list[str]) -> list[dict]:
    recs = []
    for p in paths:
        with open(p) as f:
            for line in f:
                line = line.strip()
                if line:
                    r = json.loads(line)
                    r["run"] = Path(p).stem
                    recs.append(r)
    return recs


def classify(move: dict) -> str:
    if move["reason"] == "overspeed":
        return "lunge"
    if move["reason"] in STALL_REASONS:
        return "stall"
    if (move.get("peak_err_deg") or 0) >= 5.0:
        return "marginal"
    return "ok"


def build(recs: list[dict]) -> dict:
    runs: dict[str, dict] = {}
    moves: dict[tuple[str, int], dict] = {}
    for r in recs:
        if "type" not in r:          # poweron.jsonl / diag logs use a looser schema
            continue
        run = runs.setdefault(r["run"], {"name": r["run"], "start": r["t"], "end": r["t"], "events": [], "stick": []})
        run["end"] = max(run["end"], r["t"])
        key = (r["run"], r.get("move_id"))
        if r["type"] == "event":
            run["events"].append({"t": r["t"], "msg": r["msg"]})
        elif r["type"] == "move_start":
            moves[key] = {"run": r["run"], "move_id": r["move_id"], "label": r.get("label", ""), "closing": r.get("closing"),
                          "gear": r.get("gear"), "rpm": r.get("rpm"), "maxt": r.get("maxt"), "start_deg_open": r.get("start_deg_open"),
                          "t0": r["t"], "samples": [], "psu": []}
        elif r["type"] == "move_end" and key in moves:
            m = moves[key]
            m.update({k: r.get(k) for k in ("reason", "elapsed", "travel_deg", "end_deg_open", "peak_err_deg", "end_err_deg", "blocked", "peak_rpm", "bad_replies")})
            m["outcome"] = classify(m)
        elif r["type"] == "sample" and key in moves:
            moves[key]["samples"].append([round(r["t"] - moves[key]["t0"], 3), r.get("deg_open"), r.get("err_deg"), 1 if r.get("blocked") else 0])
        elif r["type"] == "psu" and key in moves:
            moves[key]["psu"].append([round(r["t"] - moves[key]["t0"], 3), r.get("i"), r.get("v")])
        elif r["type"] == "stick_result":
            run["stick"].append({k: r.get(k) for k in ("closing_maxt", "approach_rpm", "ramp", "unstick_maxt", "seat_offset_deg", "note")})

    move_list = [m for m in moves.values() if "reason" in m]
    for m in move_list:
        currents = [p[1] for p in m["psu"] if p[1] is not None]
        m["avg_i"] = round(statistics.fmean(currents), 3) if currents else None
        m["peak_i"] = max(currents) if currents else None
        m["n_samples"] = len(m["samples"])

    # Survey grid: label starts with "survey T<maxt> R<rpm>".
    grid: dict[tuple[int, float, bool], dict] = {}
    for m in move_list:
        if not m["label"].startswith("survey T") or m["outcome"] == "lunge":
            continue
        k = (m["maxt"], round(m["rpm"], 1), bool(m["closing"]))
        grid[k] = {"maxt": m["maxt"], "rpm": round(m["rpm"], 1), "closing": bool(m["closing"]), "outcome": m["outcome"],
                   "peak_err_deg": m["peak_err_deg"], "avg_i": m["avg_i"], "peak_i": m["peak_i"], "travel_deg": m["travel_deg"],
                   "run": m["run"], "move_id": m["move_id"]}
    survey = sorted(grid.values(), key=lambda g: (g["maxt"], g["rpm"], g["closing"]))
    torques = sorted({g["maxt"] for g in survey})
    rpms = sorted({g["rpm"] for g in survey})

    # Headline numbers.
    def max_ok_rpm(maxt: int) -> float | None:
        ok = [g["rpm"] for g in survey if g["maxt"] == maxt and g["outcome"] == "ok"]
        return max(ok) if ok else None
    headline = {"torques": torques, "rpms": rpms,
                "max_ok_rpm_by_torque": {str(t): max_ok_rpm(t) for t in torques},
                "min_torque_ok_at_5rpm": next((t for t in torques if any(g["maxt"] == t and g["rpm"] <= 5.5 and g["outcome"] == "ok" for g in survey)), None)}
    stick = [s for run in runs.values() for s in run["stick"]]
    safe = [s["closing_maxt"] for s in stick if s.get("closing_maxt") and s.get("unstick_maxt") and s["unstick_maxt"] <= (s["closing_maxt"] or 0)]
    headline["max_self_recovering_closing_maxt"] = max(safe) if safe else None
    stuck = [s for s in stick if s.get("unstick_maxt") is None]
    headline["stuck_events"] = len(stuck)
    # Safety incidents: anything the harness stopped or flagged, in time order.
    keys = ("OVERSPEED", "ABORT", "uncommanded", "SILENT", "GUARD", "interrupted", "freed", "zero", "re-syncing", "NOT CONFIRMED", "STILL MOVING")
    incidents = sorted(({"t": e["t"], "run": run["name"], "msg": e["msg"]} for run in runs.values() for e in run["events"]
                        if any(k in e["msg"] for k in keys)), key=lambda e: e["t"])

    state_path = HERE / "data" / "state.json"
    state = json.loads(state_path.read_text()) if state_path.exists() else {}
    return {"generated": time.time(), "state": state, "runs": sorted(runs.values(), key=lambda r: r["start"]),
            "moves": sorted(move_list, key=lambda m: (m["run"], m["move_id"])), "survey": survey, "stick": stick, "headline": headline,
            "incidents": incidents}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE / "report" / "valve-report.html"))
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    files = a.files or sorted(glob.glob(str(HERE / "data" / "*.jsonl")))
    data = build(load(files))
    template = (HERE / "report" / "template.html").read_text()
    payload = json.dumps(data, separators=(",", ":")).replace("</", "<\\/")
    html = template.replace("/*__REPORT_DATA__*/", f"window.REPORT = {payload};")
    Path(a.out).write_text(html)
    print(f"{a.out}: {len(data['moves'])} moves, {len(data['survey'])} survey cells, {len(data['stick'])} stick results, {len(html)/1e6:.2f} MB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
