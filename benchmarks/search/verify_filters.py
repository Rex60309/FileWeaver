"""Correctness-only filter matrix over bounded generated files; no real roots."""

import argparse
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Optional

from benchmark_search import invoke, matches, require


@dataclass(frozen=True)
class Record:
    path: Path
    root: int
    size: int
    modified_ns: int


@dataclass(frozen=True)
class Case:
    query: str = "檔案"
    mode: str = "substring"
    extension: Optional[str] = None
    minimum: Optional[int] = None
    maximum: Optional[int] = None
    root: Optional[str] = None
    sort: str = "name"
    descending: bool = False
    limit: int = 7
    after: Optional[str] = None
    before: Optional[str] = None


def utc_ns(text):
    return int(datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc).timestamp()) * 1000000000


def fixture(workspace, per_root):
    roots = [workspace / "根目錄 A_日本語😀", workspace / "根目錄 B_中文"]
    records = []
    for root_index, root in enumerate(roots):
        root.mkdir()
        for index in range(per_root):
            group = index // 3
            suffix = (".txt", ".TXT", ".md", ".tar.gz", "", ".資料")[group % 6]
            name = f"檔案_日本語_é😀_{group:04d}{suffix}"
            path = root / f"分支 {index % 3}" / f"層_{group % 4}" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            size = (0, 1, 127, 4096)[(index + root_index) % 4]
            path.write_bytes(b"F" * size)
            seconds = 1704067200 + ((index + root_index) % 4) * 86400
            modified_ns = seconds * 1000000000
            os.utime(path, ns=(modified_ns, modified_ns))
            require(path.stat().st_mtime_ns == modified_ns, "Fixture filesystem did not retain exact whole-second mtime")
            records.append(Record(path, root_index, size, modified_ns))
    return roots, records


def cases_for(records):
    sorts = [(kind, descending) for kind in ("name", "size", "modified") for descending in (False, True)]
    cases = [Case(root=root, sort=kind, descending=descending)
             for root in (None, "A", "B", "parent") for kind, descending in sorts]
    for index, extension in enumerate((None, "txt", ".TXT", "md", "gz", "tar.gz", "", "資料")):
        for kind, descending in sorts:
            cases.append(Case(extension=extension, minimum=0, maximum=4096,
                              root=(None, "A", "B")[index % 3], sort=kind, descending=descending))
    ranges = [(0, 0), (1, 1), (127, 127), (4096, 4096), (1, 127), (128, 4095),
              (4097, None), (9223372036854775807, None), (0, 9223372036854775807),
              (None, 0), (127, None)]
    for index, (minimum, maximum) in enumerate(ranges):
        for root in (None, "A", "B"):
            cases.append(Case(minimum=minimum, maximum=maximum, root=root,
                              extension=(None, "txt", "資料")[index % 3],
                              sort=("name", "size", "modified")[index % 3], descending=bool(index % 2)))
    for after, before in (("2024-01-01T00:00:00Z", "2024-01-04T00:00:00Z"),
                          ("2024-01-02T00:00:00Z", None), (None, "2024-01-03T00:00:00Z"),
                          ("2024-01-04T00:00:00Z", None), (None, "2024-01-01T00:00:00Z"),
                          ("2023-12-31T00:00:00Z", "2024-01-05T00:00:00Z")):
        cases.append(Case(extension="TXT", minimum=1, maximum=4096, root="B", sort="modified",
                          descending=True, after=after, before=before))
    first, last = records[0].path.name, records[-1].path.name
    cases.extend([Case(query=first, mode="exact", limit=100),
                  Case(query=first, mode="exact", extension="txt", minimum=1, maximum=127, root="A"),
                  Case(query=first, mode="exact", extension="md"),
                  Case(query=last, mode="exact", sort="size", descending=True),
                  Case(query="' OR 1=1 --"), Case(extension="' OR 1=1 --"),
                  Case(root="child"), Case(limit=10000), Case(sort="size", descending=True, limit=1)])
    require(len(cases) == 120, "Unexpected filter matrix shape")
    return cases


def expected_rows(records, case):
    selected = []
    extension = case.extension
    if extension is not None and extension and not extension.startswith("."):
        extension = "." + extension
    for record in records:
        if not matches(record.path.name, case.mode, case.query):
            continue
        if extension is not None and record.path.suffix.encode("utf-8").lower() != extension.encode("utf-8").lower():
            continue
        if case.minimum is not None and record.size < case.minimum:
            continue
        if case.maximum is not None and record.size > case.maximum:
            continue
        if case.root in ("parent", "child") or (case.root == "A" and record.root != 0) or (case.root == "B" and record.root != 1):
            continue
        if case.after is not None and record.modified_ns <= utc_ns(case.after):
            continue
        if case.before is not None and record.modified_ns >= utc_ns(case.before):
            continue
        selected.append(record)
    selected.sort(key=lambda record: (record.path.name.encode("utf-8"), record.path.as_posix().encode("utf-8")))
    primary = (lambda record: record.path.name.encode("utf-8")) if case.sort == "name" else (
        (lambda record: record.size) if case.sort == "size" else (lambda record: record.modified_ns))
    selected.sort(key=primary, reverse=case.descending)  # Stable: only primary order reverses.
    return selected[:case.limit]


def arguments_for(case, workspace, roots):
    arguments = ["search", case.query, "--mode", case.mode, "--sort", case.sort, "--limit", str(case.limit)]
    for option, value in (("--ext", case.extension), ("--min-size", case.minimum), ("--max-size", case.maximum),
                          ("--modified-after", case.after), ("--modified-before", case.before)):
        if value is not None:
            arguments.extend([option, str(value)])
    if case.descending:
        arguments.append("--descending")
    if case.root:
        spelling = {"A": roots[0].relative_to(workspace).as_posix() + "/.",
                    "B": "./" + roots[1].relative_to(workspace).as_posix() + "/../" + roots[1].name,
                    "parent": ".", "child": roots[0].relative_to(workspace).as_posix() + "/分支 0"}[case.root]
        arguments.extend(["--root", spelling])
    return arguments


def check_search(cli, database, *, workspace, roots, root_ids, records, case):
    document = invoke(cli, [*arguments_for(case, workspace, roots), "--database", database],
                      cwd=workspace, timeout=30)
    expected = expected_rows(records, case)
    rows = document["results"]
    actual = [(row["name"], row["path"], row["size_bytes"], row["root_id"]) for row in rows]
    oracle = [(row.path.name, row.path.as_posix(), row.size, root_ids[roots[row.root].as_posix()]) for row in expected]
    require(document["count"] == len(rows) and actual == oracle, "Search differs from independent fixture oracle")
    require(all(type(row["file_id"]) is int and row["file_id"] > 0 for row in rows)
            and len({row["file_id"] for row in rows}) == len(rows), "Invalid result file IDs")
    return rows


def check_cases(cli, database, *, workspace, roots, root_ids, records, cases, phase):
    checked = []
    for index, case in enumerate(cases):
        try:
            rows = check_search(cli, database, workspace=workspace, roots=roots, root_ids=root_ids,
                                records=records, case=case)
        except RuntimeError as error:
            raise RuntimeError(f"Filter case {index} failed in {phase}: {error}") from error
        checked.append({"phase": phase, "case_id": index, "case": asdict(case),
                        "returned_count": len(rows), "correct": True})
    return checked


def run(args):
    require(1 <= args.per_root <= 1024, "--per-root must be 1..1024")
    cli = Path(args.cli).resolve(strict=True)
    info_path = Path(args.build_info).resolve(strict=True)
    require(info_path.parent == cli.parent, "Build info must come from the CLI build directory")
    output = Path(args.output)
    require(not output.exists(), "Output already exists; choose a new report path")
    build = json.loads(info_path.read_text(encoding="utf-8"))
    checked = []
    with tempfile.TemporaryDirectory(prefix="fileweaver-filter-matrix-") as temporary:
        workspace = Path(temporary).resolve()
        roots, records = fixture(workspace, args.per_root)
        database = workspace / "索引 日本語😀.sqlite"
        invoke(cli, ["init", "--database", database], timeout=30)
        for root in roots:
            indexed = invoke(cli, ["index", root, "--database", database], timeout=30)
            require(indexed["added"] == args.per_root, "Fixture index count mismatch")
        registered = invoke(cli, ["roots", "--database", database], timeout=30)["roots"]
        root_ids = {entry["path"]: entry["root_id"] for entry in registered}
        require(set(root_ids) == {root.as_posix() for root in roots}, "Unexpected registered fixture roots")
        cases = cases_for(records)
        for phase in ("online", "offline_index_snapshot"):
            if phase == "offline_index_snapshot":
                for index, root in enumerate(roots):
                    destination = workspace / f"offline-{index}"
                    require(root.resolve().parent == workspace and destination.parent.resolve() == workspace
                            and not destination.exists(), "Offline fixture move escaped owned workspace or would overwrite")
                    root.rename(destination)
            checked.extend(check_cases(cli, database, workspace=workspace, roots=roots, root_ids=root_ids,
                                       records=records, cases=cases, phase=phase))
        invalid = [["--min-size", "-1"], ["--max-size", "-1"], ["--min-size", "2", "--max-size", "1"],
                   ["--max-size", "9223372036854775808"], ["--min-size", "1KB"], ["--ext", "a/b"],
                   ["--root", ""], ["--modified-after", ""], ["--modified-before", ""],
                   ["--min-size", ""], ["--max-size", ""],
                   ["--modified-after", "2024-01-01T00:00:00Z", "--modified-before", "2024-01-01T00:00:00Z"],
                   ["--sort", "size-desc"], ["--limit", "0"]]
        for flags in invalid:
            failed = subprocess.run([str(cli), "search", "檔案", "--database", str(database), *flags, "--json"],
                                    capture_output=True, timeout=30, check=False)
            document = json.loads(failed.stdout.decode("utf-8", errors="strict"))
            require(failed.returncode == 2 and document["ok"] is False and document["exit_code"] == 2
                    and bool(failed.stderr), "Invalid filter did not preserve JSON/exit-code contract")
    report = {"report_version": 1, "kind": "correctness_only_not_performance",
              "created_at_utc": datetime.now(timezone.utc).isoformat(), "build": build,
              "binary_sha256": hashlib.sha256(cli.read_bytes()).hexdigest(),
              "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "shared_helper_sha256": hashlib.sha256(Path(__file__).with_name("benchmark_search.py").read_bytes()).hexdigest(),
              "fixture": {"roots": 2, "files_per_root": args.per_root, "files": len(records),
                          "logical_bytes": sum(row.size for row in records), "sizes": [0, 1, 127, 4096],
                          "suffixes": [".txt", ".TXT", ".md", ".tar.gz", "", ".資料"], "fixed_utc_days": 4},
              "valid_cases_per_phase": len(cases), "invalid_cases": len(invalid), "checked": checked,
              "limitations": ["Representative matrix, not exhaustive Cartesian coverage", "No timing or performance claim",
                              "Only valid Unicode, generated fixtures and indexed snapshot semantics"]}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"Verified {len(checked)} valid searches and {len(invalid)} invalid cases; report {output.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True)
    parser.add_argument("--build-info", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--per-root", type=int, default=72)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"Filter verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
