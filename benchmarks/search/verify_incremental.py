"""Correctness-only incremental/filter lifecycle on owned generated fixtures."""

import argparse
from dataclasses import replace
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from benchmark_search import invoke, require
from verify_filters import Case, Record, cases_for, check_cases, check_search, fixture, utc_ns


def owned(workspace, path):
    require(workspace in path.resolve().parents, "Synthetic mutation escaped owned temporary workspace")


def metadata_matches(record):
    observed = record.path.stat()
    return observed.st_size == record.size and observed.st_mtime_ns == record.modified_ns


def write_fixture(workspace, record, *, previous=None):
    owned(workspace, record.path)
    if previous is None:
        require(not record.path.exists(), "Synthetic creation would overwrite an existing path")
    else:
        require(previous.path == record.path and metadata_matches(previous), "Synthetic update preflight mismatch")
    record.path.parent.mkdir(parents=True, exist_ok=True)
    record.path.write_bytes(b"F" * record.size)
    os.utime(record.path, ns=(record.modified_ns, record.modified_ns))
    require(metadata_matches(record), "Synthetic write metadata mismatch")


def remove_fixture(workspace, record):
    owned(workspace, record.path)
    require(record.path.is_file() and metadata_matches(record), "Synthetic removal preflight mismatch")
    record.path.unlink()


def rename_fixture(workspace, source, destination):
    owned(workspace, source.path)
    owned(workspace, destination.path)
    require(source.path.is_file() and metadata_matches(source) and not destination.path.exists(),
            "Synthetic rename preflight mismatch or collision")
    destination.path.parent.mkdir(parents=True, exist_ok=True)
    source.path.rename(destination.path)
    os.utime(destination.path, ns=(destination.modified_ns, destination.modified_ns))
    require(metadata_matches(destination), "Synthetic rename metadata mismatch")


def run(args):
    require(3 <= args.per_root <= 1024, "--per-root must be 3..1024")
    cli = Path(args.cli).resolve(strict=True)
    info = Path(args.build_info).resolve(strict=True)
    require(info.parent == cli.parent, "Build info must come from the CLI build directory")
    output = Path(args.output)
    require(not output.exists(), "Output already exists; choose a new report path")
    checked, scans, statuses, snapshots = [], [], [], []
    with tempfile.TemporaryDirectory(prefix="fileweaver-incremental-") as temporary:
        workspace = Path(temporary).resolve()
        roots, original = fixture(workspace, args.per_root)
        first = [record for record in original if record.root == 0]
        second = [record for record in original if record.root == 1]
        updated_a = replace(first[0], size=4096, modified_ns=utc_ns("2024-01-05T00:00:00Z"))
        updated_b = replace(second[0], modified_ns=utc_ns("2024-01-06T00:00:00Z"))
        renamed = replace(first[2], path=roots[0] / "改名 空間" / "檔案_日本語_改名😀.資料")
        created = Record(roots[0] / "新增 空間" / "檔案_日本語_新增😀.md", 0, 1, utc_ns("2024-01-07T00:00:00Z"))
        changed = [updated_a if record == first[0] else updated_b if record == second[0] else record
                   for record in original if record not in (first[1], first[2])]
        changed.extend([renamed, created])
        restored = [updated_a if record == first[0] else updated_b if record == second[0] else record
                    for record in original]
        cases = cases_for(original) + [
            Case(query=renamed.path.name, mode="exact", extension="資料", root="A"),
            Case(query=created.path.name, mode="exact", extension="md", minimum=1, maximum=1, root="A"),
            Case(query=first[0].path.name, mode="exact", maximum=0, root="A"),
            Case(query=first[0].path.name, mode="exact", minimum=4096, maximum=4096, root="A")]
        database = workspace / "增量 索引😀.sqlite"
        invoke(cli, ["init", "--database", database], timeout=30)

        def scan_check(document, phase, root_index, expected):
            actual = (document["added"], document["updated"], document["missing"])
            require(actual == expected and document["missing_reconciled"] is True
                    and document["scan"] == {"files": args.per_root, "skipped": 0, "errors": 0,
                                              "complete": True, "cancelled": False},
                    f"Unexpected incremental counters in {phase}/root {root_index}")
            scans.append({"phase": phase, "root": "A" if root_index == 0 else "B",
                          "added": actual[0], "updated": actual[1], "missing": actual[2],
                          "files": document["scan"]["files"], "correct": True})

        for index, root in enumerate(roots):
            scan_check(invoke(cli, ["index", root, "--database", database], timeout=30),
                       "initial", index, (args.per_root, 0, 0))
        registered = invoke(cli, ["roots", "--database", database], timeout=30)["roots"]
        root_ids = {entry["path"]: entry["root_id"] for entry in registered}
        require(set(root_ids) == {root.as_posix() for root in roots}, "Unexpected fixture root registrations")

        def phase_check(phase, records, *, missing, scan_records):
            checked.extend(check_cases(cli, database, workspace=workspace, roots=roots, root_ids=root_ids,
                                       records=records, cases=cases, phase=phase))
            rows = check_search(cli, database, workspace=workspace, roots=roots, root_ids=root_ids,
                                records=records, case=Case(limit=10000))
            ids = {row["path"]: row["file_id"] for row in rows}
            status = invoke(cli, ["status", "--database", database], timeout=30)
            require(status["roots"] == 2 and status["schema_version"] == 3
                    and status["present_files"] == len(records) and status["missing_files"] == missing
                    and status["scan_records"] == scan_records and status["running_scan_records"] == 0,
                    f"Unexpected catalog status in {phase}")
            statuses.append({"phase": phase, "present_files": status["present_files"],
                             "missing_files": missing, "total_records": status["present_files"] + missing,
                             "scan_records": scan_records, "correct": True})
            snapshots.append({"phase": phase, "present_paths": len(ids), "correct": True})
            return ids

        def refresh_check(phase, expected):
            document = invoke(cli, ["refresh", "--database", database], timeout=30)
            require(document["command"] == "refresh" and document["cancelled"] is False
                    and len(document["roots"]) == 2, "Unexpected refresh root set")
            by_path = {entry["path"]: entry for entry in document["roots"]}
            require(set(by_path) == set(root_ids), "Refresh changed fixture root membership")
            for index, root in enumerate(roots):
                entry = by_path[root.as_posix()]
                require(entry["attempted"] is True and entry["exit_code"] == 0
                        and entry["root_id"] == root_ids[root.as_posix()], "Refresh did not validate expected root")
                scan_check(entry["index"], phase, index, expected[index])

        initial_ids = phase_check("initial", original, missing=0, scan_records=2)
        write_fixture(workspace, updated_a, previous=first[0])
        write_fixture(workspace, updated_b, previous=second[0])
        remove_fixture(workspace, first[1])
        rename_fixture(workspace, first[2], renamed)
        write_fixture(workspace, created)
        stale_ids = phase_check("stale_before_reindex", original, missing=0, scan_records=2)
        require(stale_ids == initial_ids, "Live filesystem changes leaked into indexed search")
        for index, root in enumerate(roots):
            expected = (2, 1, 2) if index == 0 else (0, 1, 0)
            scan_check(invoke(cli, ["index", root, "--database", database], timeout=30),
                       "after_index", index, expected)
        changed_ids = phase_check("after_index", changed, missing=2, scan_records=4)
        require(all(changed_ids[path] == initial_ids[path] for path in changed_ids.keys() & initial_ids.keys()),
                "Updating/surviving paths changed file IDs")
        require(changed_ids[renamed.path.as_posix()] not in initial_ids.values()
                and changed_ids[created.path.as_posix()] not in initial_ids.values(), "New paths reused old file IDs")
        refresh_check("unchanged_refresh", [(0, 0, 0), (0, 0, 0)])
        unchanged_ids = phase_check("unchanged_refresh", changed, missing=2, scan_records=6)
        require(unchanged_ids == changed_ids, "Idempotent refresh changed path identities")
        write_fixture(workspace, first[1])
        rename_fixture(workspace, renamed, first[2])
        remove_fixture(workspace, created)
        refresh_check("restored_refresh", [(0, 2, 2), (0, 0, 0)])
        restored_ids = phase_check("restored_refresh", restored, missing=2, scan_records=8)
        require(restored_ids == initial_ids, "Restored original paths did not retain original IDs")
    report = {"report_version": 1, "kind": "incremental_correctness_only_not_performance",
              "created_at_utc": datetime.now(timezone.utc).isoformat(),
              "build": json.loads(info.read_text(encoding="utf-8")),
              "binary_sha256": hashlib.sha256(cli.read_bytes()).hexdigest(),
              "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "helpers_sha256": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                 for name in ("verify_filters.py", "benchmark_search.py")},
              "fixture": {"roots": 2, "files_per_root": args.per_root, "initial_files": len(original)},
              "cases_per_phase": len(cases), "phases": 5, "checked": checked,
              "scan_checks": scans, "status_checks": statuses, "identity_snapshots": snapshots,
              "identity_checks": {"stale_snapshot": True, "stable_survivors": True,
                                  "new_path_ids": True, "idempotent_refresh": True, "original_id_resurrection": True},
              "limitations": ["Generated fixtures only; no file-management product feature",
                              "No concurrent race/incomplete/cancellation or content-hash claim", "No timing/performance claim"]}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"Verified {len(checked)} filter searches, {len(scans)} scans and stable identities; report {output.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True)
    parser.add_argument("--build-info", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--per-root", type=int, default=24)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"Incremental verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
