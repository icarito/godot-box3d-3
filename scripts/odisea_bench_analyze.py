#!/usr/bin/env python3
"""Analyze an Odisea device-bench session directory produced by
scripts/odisea_bench_device.sh.

Emits: results.json (full), table.json (compact), table.txt (human).
Steady state = sample.frame > 90. Replay/scene fingerprint guards validity.
"""
import json
import os
import sys
import glob
import statistics as st

STEADY_MIN_FRAME = 90
MIN_SAMPLES = 300
FRAMES_MIN, FRAMES_MAX = 300, 3000
# Replay end-state (recorded in replay_1790167671.json meta) — used as a
# replay-consumed sanity fingerprint, not a correctness gate.
REPLAY_EXPECTED_END = (6.964322, 9.206111, -6.838512)


def quantile(vals, p):
    v = sorted(vals)
    if not v:
        return None
    if len(v) == 1:
        return v[0]
    k = (len(v) - 1) * p
    f = int(k)
    c = min(f + 1, len(v) - 1)
    return v[f] + (v[c] - v[f]) * (k - f)


def stats(vals):
    if not vals:
        return None
    return {
        "mean": round(st.mean(vals), 3),
        "p50": round(quantile(vals, 0.5), 3),
        "p95": round(quantile(vals, 0.95), 3),
        "min": round(min(vals), 3),
        "max": round(max(vals), 3),
        "n": len(vals),
    }


def load_run(label, run_dir):
    """Return dict describing one run, or None if it has no perf file."""
    meta = {}
    mp = os.path.join(run_dir, "meta.json")
    if os.path.isfile(mp):
        try:
            meta = json.load(open(mp))
        except Exception:
            meta = {}
    flags = []
    for f in glob.glob(os.path.join(run_dir, "flag.*")):
        flags.append(os.path.basename(f).split(".", 1)[1])
    if meta.get("rc") == 139:
        flags.append("segfault")
    elif meta.get("rc") == 137:
        flags.append("sigkill_timeout")
    perf_path = os.path.join(run_dir, "replay_perf.json")
    rec = {"label": label, "run": meta.get("run"), "dir": run_dir,
           "meta": meta, "flags": flags, "valid": False, "invalid_reason": None,
           "stats": {}, "top_systems": [], "fingerprint": {}}
    if not os.path.isfile(perf_path):
        rec["invalid_reason"] = "no_perf_file"
        return rec
    try:
        d = json.load(open(perf_path))
    except Exception as e:
        rec["invalid_reason"] = "bad_json:%s" % e
        return rec

    samples = d.get("muestras") or []
    rec["frames"] = d.get("frames")
    rec["etiqueta"] = d.get("etiqueta")
    if len(samples) < MIN_SAMPLES:
        rec["invalid_reason"] = "short_replay:%d" % len(samples)
    steady = [s for s in samples if s.get("frame", 0) > STEADY_MIN_FRAME]
    if not steady:
        rec["invalid_reason"] = "no_steady_samples"
        return rec

    def col(k):
        return [s[k] for s in steady if k in s]

    rec["stats"] = {
        "fps": stats(col("fps")),
        "ms_process": stats(col("ms_process")),
        "ms_physics": stats(col("ms_physics")),
        "draw_calls": stats(col("draw_calls")),
        "nodos": stats(col("nodos")),
        "objetos": stats(col("objetos")),
        "nodos_last": steady[-1].get("nodos"),
        "objetos_last": steady[-1].get("objetos"),
    }
    rec["top_systems"] = sorted(
        d.get("perfiles") or [], key=lambda p: -p.get("ms_por_llamada", 0)
    )[:12]

    # fingerprint
    fp = {}
    s100 = next((s for s in samples if s.get("frame") == 100), None)
    if s100:
        fp = {"x": s100.get("x"), "y": s100.get("y"), "z": s100.get("z"),
              "nodos": s100.get("nodos"), "objetos": s100.get("objetos")}
    rec["fingerprint"] = fp

    last = samples[-1]
    if all(k in last for k in ("x", "y", "z")):
        rec["end_dist"] = round(
            ((last["x"] - REPLAY_EXPECTED_END[0]) ** 2
             + (last["y"] - REPLAY_EXPECTED_END[1]) ** 2
             + (last["z"] - REPLAY_EXPECTED_END[2]) ** 2) ** 0.5, 4)
    else:
        rec["end_dist"] = None

    if not rec["invalid_reason"]:
        if any(f in ("oom", "killed", "killed137", "sigkill_timeout") for f in flags):
            rec["invalid_reason"] = "oom_or_timeout"
        elif meta.get("rc") == 137:
            rec["invalid_reason"] = "timeout"
        else:
            rec["valid"] = True
            if not (FRAMES_MIN <= (rec["frames"] or 0) <= FRAMES_MAX):
                rec["flags"].append("odd_frames:%s" % rec["frames"])
    return rec


def summarize_engine(label, runs, meta):
    valid = [r for r in runs if r["valid"]]
    per_run = []
    for r in valid:
        s = r["stats"]
        per_run.append({
            "run": r["run"],
            "fps_mean": s["fps"]["mean"], "fps_p50": s["fps"]["p50"], "fps_p95": s["fps"]["p95"],
            "ms_process_mean": s["ms_process"]["mean"],
            "ms_process_p50": s["ms_process"]["p50"],
            "ms_process_p95": s["ms_process"]["p95"],
            "ms_physics_mean": s["ms_physics"]["mean"],
            "ms_physics_p50": s["ms_physics"]["p50"],
            "ms_physics_p95": s["ms_physics"]["p95"],
            "draw_calls_mean": s["draw_calls"]["mean"],
            "nodos_mean": s["nodos"]["mean"],
            "objetos_mean": s["objetos"]["mean"],
            "seconds": r["meta"].get("seconds"),
            "temp_soc_before": r["meta"].get("temp_soc_before"),
            "temp_soc_after": r["meta"].get("temp_soc_after"),
            "end_dist": r.get("end_dist"),
            "flags": r["flags"],
        })

    def agg(key):
        v = [p[key] for p in per_run if p[key] is not None]
        if not v:
            return None
        return {"min": round(min(v), 3), "mean": round(st.mean(v), 3),
                "max": round(max(v), 3), "spread": round(max(v) - min(v), 3),
                "values": [round(x, 3) for x in v]}

    pooled = {"fps": [], "ms_process": [], "ms_physics": [], "draw_calls": [],
              "nodos": [], "objetos": []}
    sys_ms = {}
    for r in valid:
        d = json.load(open(os.path.join(r["dir"], "replay_perf.json")))
        for s in d.get("muestras") or []:
            if s.get("frame", 0) > STEADY_MIN_FRAME:
                for k in pooled:
                    if k in s:
                        pooled[k].append(s[k])
        for p in d.get("perfiles") or []:
            sys_ms.setdefault(p.get("sistema"), []).append(p.get("ms_por_llamada", 0))
    pooled_stats = {k: stats(v) for k, v in pooled.items() if v}
    top_systems = sorted(
        ({"sistema": k, "ms_por_llamada_median": round(st.median(v), 3)}
         for k, v in sys_ms.items()),
        key=lambda x: -x["ms_por_llamada_median"])[:12]

    temps_soc = []
    for r in valid:
        for key in ("temp_soc_before", "temp_soc_after"):
            t = r["meta"].get(key)
            if t is not None and t > 0:
                temps_soc.append(t)

    f100 = [r["fingerprint"] for r in valid if r.get("fingerprint")]
    f100_med = None
    if f100:
        f100_med = {k: round(st.median([f[k] for f in f100 if f.get(k) is not None]), 4)
                    for k in ("x", "y", "z")}
    end_dists = [r["end_dist"] for r in valid if r.get("end_dist") is not None]

    return {
        "label": label,
        "bin": runs[0]["meta"].get("bin") if runs else None,
        "md5": runs[0]["meta"].get("md5") if runs else None,
        "runs_total": len(runs),
        "runs_valid": len(valid),
        "invalid": [{"run": r["run"], "reason": r["invalid_reason"]}
                    for r in runs if not r["valid"]],
        "per_run": per_run,
        "agg": {
            "fps_mean": agg("fps_mean"),
            "fps_p50": agg("fps_p50"),
            "fps_p95": agg("fps_p95"),
            "ms_process_mean": agg("ms_process_mean"),
            "ms_process_p50": agg("ms_process_p50"),
            "ms_process_p95": agg("ms_process_p95"),
            "ms_physics_mean": agg("ms_physics_mean"),
            "ms_physics_p50": agg("ms_physics_p50"),
            "ms_physics_p95": agg("ms_physics_p95"),
        },
        "pooled": pooled_stats,
        "top_systems": top_systems,
        "temp_soc_range_c": [min(temps_soc), max(temps_soc)] if temps_soc else None,
        "f100_median": f100_med,
        "end_dist_median": round(st.median(end_dists), 4) if end_dists else None,
        "frames": [r.get("frames") for r in valid if r.get("frames") is not None],
    }


def verdict(eng, base, metric, lower_better):
    """A gain counts only if the two ranges do not overlap AND the gap exceeds
    the noise floor (largest available repeat spread)."""
    e, b = eng["agg"].get(metric), base["agg"].get(metric)
    if not e or not b:
        return "n/a"
    spreads = [a["spread"] for a in (e, b) if a and len(a["values"]) >= 2]
    floor = max(spreads) if spreads else None

    def beyond(gap):
        return gap > 0 and (floor is None or gap > floor)

    if lower_better:
        if beyond(b["min"] - e["max"]):
            return "BEATS"
        if beyond(e["min"] - b["max"]):
            return "worse"
    else:
        if beyond(e["min"] - b["max"]):
            return "BEATS"
        if beyond(b["min"] - e["max"]):
            return "worse"
    return "~noise"


def fmt_range(a, key):
    if not a:
        return "-"
    return "%.2f [%.2f-%.2f]" % (a["mean"], a["min"], a["max"])


def main():
    if len(sys.argv) < 2:
        print("usage: odisea_bench_analyze.py SESSION_DIR", file=sys.stderr)
        sys.exit(2)
    sess = sys.argv[1]
    meta = json.load(open(os.path.join(sess, "meta.json")))
    engines_order = []
    for line in open(os.path.join(sess, "engines.txt")):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("|", 1)
        if len(parts) == 2:
            engines_order.append(parts[0])

    engines = []
    for label in engines_order:
        runs = []
        for rdir in sorted(glob.glob(os.path.join(sess, "raw", label, "run*"))):
            rec = load_run(label, rdir)
            if rec:
                runs.append(rec)
        if runs:
            engines.append(summarize_engine(label, runs, meta))

    base = next((e for e in engines if e["label"] == "baseline"), None)
    base_fp = base["f100_median"] if base else None

    def fp_dist(e):
        f = e.get("f100_median")
        if not base_fp or not f:
            return None
        return round(((f["x"] - base_fp["x"]) ** 2 + (f["y"] - base_fp["y"]) ** 2
                      + (f["z"] - base_fp["z"]) ** 2) ** 0.5, 3)

    def run_map(e):
        return {r["run"]: r for r in e["per_run"] if r.get("run") is not None}

    def paired(cand, ref, key):
        cm, rm = run_map(cand), run_map(ref)
        ds = []
        for i in sorted(set(cm) & set(rm)):
            a, b = cm[i].get(key), rm[i].get(key)
            if a is not None and b is not None:
                ds.append(round(a - b, 3))
        if not ds:
            return None
        sd = st.pstdev(ds)
        return {"n": len(ds), "mean": round(st.mean(ds), 3), "min": min(ds),
                "max": max(ds), "spread": round(max(ds) - min(ds), 3),
                "sd": round(sd, 3),
                "t": round(st.mean(ds) / (sd / (len(ds) ** 0.5)), 2) if sd > 0 else None,
                "pos": sum(1 for x in ds if x > 0), "neg": sum(1 for x in ds if x < 0),
                "values": ds}

    PAIR_METRICS = ("fps_mean", "fps_p50", "ms_process_mean", "ms_process_p50", "ms_physics_mean")
    refs = {e["label"]: e for e in engines}
    paired_json = {}
    for e in engines:
        paired_json[e["label"]] = {}
        for ref_label in ("base", "baseline"):
            ref = refs.get(ref_label)
            if not ref or ref["label"] == e["label"]:
                continue
            paired_json[e["label"]][ref_label] = {m: paired(e, ref, m) for m in PAIR_METRICS}
    # paired noise proxy = spread of the base-vs-baseline control pairing
    ctrl = (paired_json.get("base", {}) or {}).get("baseline") or {}
    paired_floor = {m: (ctrl[m]["spread"] if ctrl.get(m) else None) for m in PAIR_METRICS}
    paired_floor_sd = {m: (ctrl[m]["sd"] if ctrl.get(m) else None) for m in PAIR_METRICS}

    base_floor = {}
    if base:
        for m in ("fps_mean", "fps_p50", "fps_p95",
                  "ms_process_mean", "ms_process_p50", "ms_process_p95",
                  "ms_physics_mean", "ms_physics_p50", "ms_physics_p95"):
            a = base["agg"].get(m)
            base_floor[m] = a["spread"] if a and len(a["values"]) >= 2 else None

    results = {"meta": meta, "baseline": "baseline",
               "noise_floor_baseline_spread": base_floor,
               "paired_noise_floor_spread_control_base_vs_baseline": paired_floor,
               "paired_noise_floor_sd_control_base_vs_baseline": paired_floor_sd,
               "paired": paired_json, "engines": engines}

    table = {"session": meta.get("session"), "governor": meta.get("governor_pinned"),
             "governor_restore_cmd": meta.get("governor_restore_cmd"),
             "noise_floor_baseline_spread": base_floor,
             "paired_noise_floor_spread_control_base_vs_baseline": paired_floor,
             "engines": []}
    for e in engines:
        row = {
            "label": e["label"], "md5": e["md5"], "runs_valid": e["runs_valid"],
            "runs_total": e["runs_total"],
            "fps_mean_range": e["agg"]["fps_mean"],
            "ms_process_mean_range": e["agg"]["ms_process_mean"],
            "ms_physics_mean_range": e["agg"]["ms_physics_mean"],
            "pooled": e["pooled"], "top_systems": e["top_systems"],
            "verdict_vs_baseline": {
                "fps": verdict(e, base, "fps_mean", False) if base else "n/a",
                "ms_process": verdict(e, base, "ms_process_mean", True) if base else "n/a",
                "ms_process_p95": verdict(e, base, "ms_process_p95", True) if base else "n/a",
                "ms_physics": verdict(e, base, "ms_physics_mean", True) if base else "n/a",
                "ms_physics_p95": verdict(e, base, "ms_physics_p95", True) if base else "n/a",
            },
            "invalid": e["invalid"], "temp_soc_range_c": e["temp_soc_range_c"],
            "f100_median": e.get("f100_median"),
            "f100_dist_to_baseline": fp_dist(e),
            "end_dist_median": e.get("end_dist_median"),
            "frames": e.get("frames"),
        }
        table["engines"].append(row)

    json.dump(results, open(os.path.join(sess, "results.json"), "w"), indent=1)
    json.dump(table, open(os.path.join(sess, "table.json"), "w"), indent=1)

    lines = []
    lines.append("Odisea device bench | session %s | governor %s | runs/engine %s"
                 % (meta.get("session"), meta.get("governor_pinned"), meta.get("runs")))
    lines.append("device %s %s | replay %s | main-pack %s (%s)"
                 % (meta.get("arch"), meta.get("kernel"), meta.get("replay"),
                    meta.get("pck"), meta.get("pck_md5")))
    lines.append("governor restore: %s" % meta.get("governor_restore_cmd"))
    nf = " | ".join("%s spread=%s" % (k, v) for k, v in base_floor.items())
    lines.append("noise floor (baseline min-max spread across runs): %s" % nf)
    lines.append("all timing stats are per render frame over the SAME replay (frames column must be constant across contexts;")
    lines.append("same frames => same workload, so fps/ms are directly comparable across contexts).")
    lines.append("")
    hdr = ("%-10s %5s %-7s %-20s %-22s %-22s %-8s %-7s %-8s %-9s %-9s %-9s %-9s %-9s"
           % ("engine", "valid", "frames", "fps mean[min-max]", "ms_process mean[min-max]",
              "ms_physics mean[min-max]", "draws", "nodos", "objetos",
              "vd:msP", "vd:fps", "vd:msPh", "f100dbase", "enddrift"))
    lines.append(hdr)
    lines.append("-" * len(hdr))
    for r in table["engines"]:
        p = r["pooled"]

        def g(k, f):
            return p[k][f] if k in p and p[k] else None
        def fv():
            fr = r.get("frames") or []
            return "%d" % fr[0] if fr and len(set(fr)) == 1 else (
                "%d-%d" % (min(fr), max(fr)) if fr else "-")
        lines.append("%-10s %2d/%-2d %-7s %-20s %-22s %-22s %-8s %-7s %-8s %-9s %-9s %-9s %-9s %-9s" % (
            r["label"], r["runs_valid"], r["runs_total"], fv(),
            fmt_range(r["fps_mean_range"], "mean"),
            fmt_range(r["ms_process_mean_range"], "mean"),
            fmt_range(r["ms_physics_mean_range"], "mean"),
            ("%.1f" % g("draw_calls", "mean")) if g("draw_calls", "mean") is not None else "-",
            ("%.0f" % g("nodos", "mean")) if g("nodos", "mean") is not None else "-",
            ("%.0f" % g("objetos", "mean")) if g("objetos", "mean") is not None else "-",
            r["verdict_vs_baseline"]["ms_process"],
            r["verdict_vs_baseline"]["fps"],
            r["verdict_vs_baseline"]["ms_physics"],
            ("%.3f" % r["f100_dist_to_baseline"]) if r["f100_dist_to_baseline"] is not None else "-",
            ("%.3f" % r["end_dist_median"]) if r["end_dist_median"] is not None else "-",
        ))
    lines.append("")
    lines.append("steady-state (frame>90) pooled percentiles, plus p95 verdicts vs baseline:")
    h2 = ("%-10s %-13s %-9s %-9s %-9s %-9s %-9s %-9s %-9s"
          % ("engine", "fps p50/p95", "msPp50", "msPp95", "msPhp50", "msPhp95",
             "drawsp95", "vd:msP95", "vd:msPh95"))
    lines.append(h2)
    lines.append("-" * len(h2))
    for r in table["engines"]:
        p = r["pooled"]

        def g(k, f):
            return p[k][f] if k in p and p[k] else None

        def s(k, f, fmt="%.1f"):
            v = g(k, f)
            return fmt % v if v is not None else "-"
        lines.append("%-10s %-13s %-9s %-9s %-9s %-9s %-9s %-9s %-9s" % (
            r["label"],
            "%s/%s" % (s("fps", "p50"), s("fps", "p95")),
            s("ms_process", "p50"), s("ms_process", "p95"),
            s("ms_physics", "p50"), s("ms_physics", "p95"),
            s("draw_calls", "p95"),
            r["verdict_vs_baseline"]["ms_process_p95"],
            r["verdict_vs_baseline"]["ms_physics_p95"],
        ))
    lines.append("")
    lines.append("PAIRED per-rep deltas (candidate - reference), matched by round index:")
    lines.append("paired noise proxy (spread of base-vs-baseline control deltas): "
                 + " | ".join("%s=%s" % (k, v) for k, v in paired_floor.items()))
    lines.append("paired noise proxy SD (control deltas): "
                 + " | ".join("%s=%s" % (k, v) for k, v in paired_floor_sd.items()))
    h3 = ("%-10s %-9s %-3s %-24s %-24s %-24s %-24s %-6s %-7s"
          % ("engine", "vs", "n", "dFPS mean[min-max]", "dmsP mean[min-max]",
             "dmsP50 mean[min-max]", "dmsPh mean[min-max]", "t(fps)", "+/-fps"))
    lines.append(h3)
    lines.append("-" * len(h3))

    def pf(d):
        return "%.3f [%.3f-%.3f]" % (d["mean"], d["min"], d["max"])

    def jv(p, m, higher_better):
        d, fl = p.get(m), paired_floor.get(m)
        if not d:
            return "n/a"
        fl = fl or 0.0
        if d["min"] > 0 and d["mean"] > fl:
            return "BETTER" if higher_better else "WORSE"
        if d["max"] < 0 and -d["mean"] > fl:
            return "WORSE" if higher_better else "BETTER"
        return "~noise"

    for e in engines:
        for ref_label in ("base", "baseline"):
            p = paired_json.get(e["label"], {}).get(ref_label)
            if not p:
                continue
            fp = p["fps_mean"]
            lines.append("%-10s %-9s %-3s %-24s %-24s %-24s %-24s %-6s %-7s" % (
                e["label"], ref_label, (p["fps_mean"] or {}).get("n", "-"),
                pf(p["fps_mean"]) if p["fps_mean"] else "-",
                pf(p["ms_process_mean"]) if p["ms_process_mean"] else "-",
                pf(p["ms_process_p50"]) if p["ms_process_p50"] else "-",
                pf(p["ms_physics_mean"]) if p["ms_physics_mean"] else "-",
                ("%s" % fp["t"]) if fp and fp.get("t") is not None else "-",
                ("%d+/%d-" % (fp["pos"], fp["neg"])) if fp else "-"))
    lines.append("")
    lines.append("paired verdicts vs base (fps higher better / ms lower better):")
    for e in engines:
        p = paired_json.get(e["label"], {}).get("base")
        if not p:
            continue
        lines.append("  %-10s fps=%s  ms_process=%s  ms_process_p50=%s  ms_physics=%s" % (
            e["label"], jv(p, "fps_mean", True), jv(p, "ms_process_mean", False),
            jv(p, "ms_process_p50", False), jv(p, "ms_physics_mean", False)))
    lines.append("")
    for r in table["engines"]:
        if r["invalid"]:
            lines.append("[%s] INVALID runs: %s" % (r["label"], r["invalid"]))
        if r["temp_soc_range_c"]:
            lines.append("[%s] soc temp range: %s C" % (r["label"], r["temp_soc_range_c"]))
    lines.append("")
    for r in table["engines"]:
        lines.append("== top script systems by ms/call (median of valid runs): %s" % r["label"])
        for i, s in enumerate(r["top_systems"]):
            lines.append("  %2d. %-28s %8.3f" % (i + 1, s["sistema"], s["ms_por_llamada_median"]))
        lines.append("")
    txt = "\n".join(lines)
    open(os.path.join(sess, "table.txt"), "w").write(txt + "\n")
    print(txt)


if __name__ == "__main__":
    main()
