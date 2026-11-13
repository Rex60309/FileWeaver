"""Measure indexing on owned synthetic files; never accepts a user source folder."""

import argparse
from contextlib import closing
from dataclasses import replace
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sqlite3
import statistics
import sys
import tempfile
import time

from benchmark_search import hardware, invoke, require
from verify_filters import Record
from verify_incremental import remove_fixture, rename_fixture, write_fixture


def snapshot(database, records, missing):
    with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)) as db:
        actual = dict(db.execute("SELECT display_path,size_bytes FROM files WHERE is_present=1"))
        absent = db.execute("SELECT count(*) FROM files WHERE is_present=0").fetchone()[0]
    expected = {r.path.as_posix(): r.size for r in records.values()}
    require(actual == expected and absent == missing, "Index snapshot differs from synthetic fixture")


def run(args):
    cli = args.cli.resolve(strict=True)
    info = args.build_info.resolve(strict=True)
    require(info.parent == cli.parent, "Build info must be from the CLI build directory")
    build = json.loads(info.read_text(encoding="utf-8"))
    require(build["build_type"] == "Release", "Use a Release build")
    require(100 <= args.files <= 100000, "files must be 100..100000")
    require(1 <= args.rounds <= 10, "rounds must be 1..10")
    require(not args.output.exists(), "Output already exists; choose a new path")
    raw = []
    with tempfile.TemporaryDirectory(prefix="fileweaver-index-measurement-") as temporary:
        workspace = Path(temporary).resolve()
        source = workspace / "source"
        source.mkdir()
        folders = [source / f"branch_{i % 3}" / f"bucket_{i}" for i in range(24)]
        for folder in folders:
            folder.mkdir(parents=True, exist_ok=True)
        start = time.perf_counter_ns()
        records = {}
        for i in range(args.files):
            path = folders[i % len(folders)] / f"檔案_日本語😀_{i:06d}.txt"
            path.touch(exist_ok=False)
            records[path] = Record(path, 0, 0, path.stat().st_mtime_ns)
        generation_ns = time.perf_counter_ns() - start
        for round_index in range(args.rounds):
            database = workspace / f"catalog-{round_index}.sqlite"
            invoke(cli, ["init", "--database", database])

            def measure(phase, expected, missing):
                begin = time.perf_counter_ns()
                result = invoke(cli, ["index", source, "--database", database])
                elapsed = time.perf_counter_ns() - begin
                scan = result["scan"]
                require(scan["complete"] and not scan["cancelled"] and
                        scan["files"] == args.files and scan["errors"] == 0 and scan["skipped"] == 0,
                        "Incomplete or unexpected scan")
                counters = (result["added"], result["updated"], result["missing"])
                require(counters == expected and result["missing_reconciled"], "Wrong incremental counters")
                snapshot(database, records, missing)
                size = sum(p.stat().st_size for p in
                           [database, Path(str(database) + "-wal"), Path(str(database) + "-shm")]
                           if p.exists())
                raw.append({"round": round_index, "phase": phase, "elapsed_ns": elapsed,
                            "added": counters[0], "updated": counters[1], "missing": counters[2],
                            "present_files": len(records), "database_and_sidecars_bytes": size,
                            "correct": True})

            measure("initial", (args.files, 0, 0), 0)
            measure("unchanged", (0, 0, 0), 0)
            ordered = sorted(records.values(), key=lambda r: r.path.as_posix())
            for previous in ordered[:10]:
                record = replace(previous, size=previous.size + 1,
                                 modified_ns=previous.modified_ns + 1000000000)
                write_fixture(workspace, record, previous=previous)
                records[record.path] = record
            for previous in ordered[10:20]:
                record = replace(previous, path=previous.path.with_name(f"renamed_{round_index}_" + previous.path.name))
                rename_fixture(workspace, previous, record)
                del records[previous.path]
                records[record.path] = record
            for previous in ordered[20:30]:
                remove_fixture(workspace, previous)
                del records[previous.path]
            for i in range(10):
                path = folders[i] / f"created_{round_index}_{i}.txt"
                record = Record(path, 0, 1, 1704067200000000000)
                write_fixture(workspace, record)
                records[path] = record
            measure("changed", (20, 10, 20), 20)
            measure("unchanged_after_changes", (0, 0, 0), 20)
        summaries = []
        for phase in ("initial", "unchanged", "changed", "unchanged_after_changes"):
            values = [r["elapsed_ns"] for r in raw if r["phase"] == phase]
            summaries.append({"phase": phase, "samples": len(values), "median_ns": statistics.median(values),
                              "min_ns": min(values), "max_ns": max(values)})
    return {"created_at_utc": datetime.now(timezone.utc).isoformat(), "hardware": hardware(),
            "build": build, "binary_sha256": hashlib.sha256(cli.read_bytes()).hexdigest(),
            "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "dataset": {"files": args.files, "subdirectories": 27, "initial_size_bytes": 0,
                        "unicode_filenames": True, "changes_per_round":
                        {"create": 10, "modify": 10, "rename": 10, "delete": 10}},
            "generation_ns": generation_ns, "rounds": args.rounds,
            "scope": "CLI startup, full metadata rescan/index transaction and JSON decoding; validation excluded",
            "cache": "No cache eviction; generated fixture and repeated scans warm the OS cache",
            "storage": args.storage_label, "raw": raw, "summaries": summaries,
            "limitations": ["Fresh database per round; current fixture includes previous round changes",
                            "The changed scan is a full metadata rescan, not an event-only update",
                            "Median/range only: too few initial-index samples to estimate tail latency",
                            "No GUI, cold-cache, real-content, power-control or cross-machine measurement"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--build-info", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--files", type=int, default=10000)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--storage-label", default="unknown")
    args = parser.parse_args()
    try:
        document = run(args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(document, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
        print(json.dumps(document["summaries"]))
    except (OSError, RuntimeError, ValueError, sqlite3.Error) as error:
        print(f"Index measurement failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
