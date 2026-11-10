"""Reproducible fuzzy CLI baseline over generated SQLite metadata (no source crawl)."""
import argparse
from contextlib import closing
from datetime import datetime, timezone
import json
from pathlib import Path
import sqlite3
import tempfile
import time
from benchmark_search import cli_document, hardware, invoke, percentile, require
from cli_memory import run_observed


def run(args):
    require(100 <= args.rows <= 1000000, "rows must be 100..1000000")
    require(3 <= args.repetitions <= 100, "repetitions must be 3..100")
    targets = ["needle.txt", "image_processing_final_report.pdf", "日本語😀.txt"]
    cases = [("needle", [targets[0]]), ("nedle", [targets[0]]),
             ("img proc fin", [targets[1]]), ("日本😀", [targets[2]])]
    measurements = []
    with tempfile.TemporaryDirectory(prefix="fileweaver-fuzzy-benchmark-") as temporary:
        db = Path(temporary) / "index.sqlite"
        invoke(args.cli, ["init", "--database", db])
        names = [f"noise{i:07d}.txt" for i in range(args.rows - len(targets))] + targets
        with closing(sqlite3.connect(db)) as connection:
            connection.execute("INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/synthetic','/synthetic',0)")
            connection.executemany("INSERT INTO files(root_id,normalized_path,display_path,name,extension,size_bytes,modified_at,indexed_at) VALUES(1,?,?,?,'.txt',1,0,0)",
                                   ((f"/synthetic/{i}", f"/synthetic/{name}", name) for i, name in enumerate(names)))
            connection.commit()
        for query, expected in cases:
            command = [str(args.cli), "search", query, "--mode", "fuzzy", "--database", str(db), "--json"]
            for iteration in range(args.repetitions + 1):
                begin = time.perf_counter_ns()
                process, memory = run_observed(command)
                elapsed = (time.perf_counter_ns() - begin) / 1000000
                result = cli_document(process)
                require([row["name"] for row in result["results"]] == expected,
                        f"Incorrect fuzzy results for {query}")
                measurements.append({"query": query, "iteration": iteration,
                                     "cache": "uncontrolled_first" if iteration == 0 else "warm_os_cache",
                                     "elapsed_ms": elapsed, "correct": True, "memory": memory})
    summaries = []
    for query, _ in cases:
        values = [row["elapsed_ms"] for row in measurements if row["query"] == query and row["iteration"] > 0]
        summaries.append({"query": query, "p50_ms": percentile(values, .5), "p95_ms": percentile(values, .95)})
    return {"created_at_utc": datetime.now(timezone.utc).isoformat(), "hardware": hardware(),
            "build": json.loads(args.build_info.read_text(encoding="utf-8")),
            "dataset": {"rows": args.rows, "synthetic_metadata_only": True, "target_rows": len(targets)},
            "scope": "CLI startup, read-only SQLite query, candidate generation, ranking, JSON, memory observer overhead",
            "storage": args.storage_label, "cache_note": "No cache eviction. First run is not a controlled cold-cache measurement.",
            "baseline": "First fuzzy baseline; no speedup comparison or latency-target claim",
            "summaries": summaries, "measurements": measurements}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--build-info", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rows", type=int, default=100000)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--storage-label", default="unknown; not measured")
    args = parser.parse_args()
    document = run(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(document["summaries"], ensure_ascii=True))
