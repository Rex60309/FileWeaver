"""Verify literal filename-start matching through CLI and existing filter oracle."""

from dataclasses import replace
from contextlib import closing
import json
from pathlib import Path
import subprocess
import sqlite3
import sys
import tempfile

if not __debug__:
    raise RuntimeError("Prefix checks require Python without -O")

module_directory = Path(__file__).resolve().parents[2] / "benchmarks" / "search"
sys.path.insert(0, str(module_directory))
from benchmark_search import invoke, require
from verify_filters import Case, Record, cases_for, check_cases, fixture, utc_ns
from verify_incremental import owned, remove_fixture, write_fixture

cli = sys.argv[1]
with tempfile.TemporaryDirectory(prefix="fileweaver-prefix-") as temporary:
    workspace = Path(temporary).resolve()
    roots, records = fixture(workspace, 24)
    extras = [("x檔案_日本語_內部.txt", 3), ("%_literal'file.txt", 5), ("éclair.txt", 1),
              ("😀_start.txt", 0), ("Report.txt", 5), ("o'report.txt", 3)]
    for name, size in extras:
        record = Record(roots[0] / "literal" / name, 0, size, utc_ns("2024-01-02T00:00:00Z"))
        write_fixture(workspace, record)
        records.append(record)
    database = workspace / "前綴 索引😀.sqlite"
    invoke(cli, ["init", "--database", database])
    for root in roots:
        invoke(cli, ["index", root, "--database", database])
    first = subprocess.run([cli, "search", "檔案", "--mode", "prefix", "--limit", "10000",
                            "--database", str(database), "--json"], capture_output=True, timeout=30, check=False)
    assert first.returncode == 0, first.stderr
    prefix = json.loads(first.stdout)
    assert prefix["count"] == 48
    substring = invoke(cli, ["search", "檔案", "--limit", "10000", "--database", database])
    assert substring["count"] == 49  # Default stays substring, including the embedded token.
    root_ids = {entry["path"]: entry["root_id"] for entry in invoke(cli, ["roots", "--database", database])["roots"]}
    before = invoke(cli, ["status", "--database", database])
    cases = [replace(case, mode="prefix") for case in cases_for(records)]
    probes = {"日本語": 0, "檔案_": 48, "😀": 1, "é": 1, "É": 0, "%_": 1,
              "%_literal'": 1, "%": 1, "_": 0, "*": 0, "o'": 1, "REP": 1, "epo": 0}
    for query, count in probes.items():
        found = invoke(cli, ["search", query, "--mode", "prefix", "--limit", "10000", "--database", database])
        assert found["count"] == count, (query, found)
        cases.append(Case(query=query, mode="prefix", limit=10000))
    cases.append(Case(query=roots[0].as_posix(), mode="prefix", limit=10000))  # Filename, not path.
    total = 0
    for phase in ("online", "offline"):
        if phase == "offline":
            for index, root in enumerate(roots):
                destination = workspace / f"offline-{index}"
                owned(workspace, root)
                owned(workspace, destination)
                require(not destination.exists(), "Offline fixture destination collision")
                root.rename(destination)
        checks = check_cases(cli, database, workspace=workspace, roots=roots, root_ids=root_ids,
                             records=records, cases=cases, phase=phase)
        assert all(row["correct"] for row in checks)
        total += len(checks)
    after = invoke(cli, ["search", "檔案", "--mode", "prefix", "--limit", "10000", "--database", database])
    assert after == prefix
    assert invoke(cli, ["status", "--database", database]) == before
    # Reconcile only generated data, then verify the complete matrix at each state.
    for index, root in enumerate(roots):
        source = workspace / f"offline-{index}"
        owned(workspace, source)
        owned(workspace, root)
        require(not root.exists(), "Restoring fixture root would overwrite a destination")
        source.rename(root)
    removed = records[0]
    remove_fixture(workspace, removed)
    scan = invoke(cli, ["index", roots[0], "--database", database])
    assert scan["missing"] == 1 and scan["missing_reconciled"] is True
    remaining = [record for record in records if record != removed]
    for phase, expected in (("missing", remaining), ("restored", records)):
        if phase == "restored":
            write_fixture(workspace, removed)
            scan = invoke(cli, ["index", roots[0], "--database", database])
            assert scan["added"] == 0 and scan["missing"] == 0
        checks = check_cases(cli, database, workspace=workspace, roots=roots, root_ids=root_ids,
                             records=expected, cases=cases, phase=phase)
        assert all(row["correct"] for row in checks)
        total += len(checks)
    assert invoke(cli, ["search", "檔案", "--mode", "prefix", "--limit", "10000",
                        "--database", database]) == prefix  # Stable file IDs after reappearance.
    legacy = workspace / "legacy.sqlite"
    require(not legacy.exists(), "Legacy fixture destination collision")
    with closing(sqlite3.connect(database)) as source, closing(sqlite3.connect(legacy)) as destination:
        source.backup(destination)
        destination.executescript("DROP TABLE root_exclusions; DROP TABLE root_scan_options; PRAGMA user_version=2;")
    legacy_before = invoke(cli, ["status", "--database", legacy])
    checks = check_cases(cli, legacy, workspace=workspace, roots=roots, root_ids=root_ids,
                         records=records, cases=cases, phase="legacy-v2")
    assert all(row["correct"] for row in checks)
    total += len(checks)
    assert invoke(cli, ["status", "--database", legacy]) == legacy_before
    with closing(sqlite3.connect(legacy)) as connection:
        assert connection.execute("PRAGMA user_version").fetchone()[0] == 2
        assert connection.execute("SELECT count(*) FROM sqlite_master WHERE name IN "
                                  "('root_exclusions','root_scan_options')").fetchone()[0] == 0
    human = subprocess.run([cli, "search", "REP", "--mode", "prefix", "--database", str(database)],
                           capture_output=True, timeout=15, check=False)
    assert human.returncode == 0 and not human.stderr and "Report.txt" in human.stdout.decode("utf-8")
    assert "prefix" in invoke(cli, ["search", "--help"])["text"]
    for query, mode in (("", "prefix"), ("x", "starts-with")):
        invalid = subprocess.run([cli, "search", query, "--mode", mode, "--database", str(database), "--json"],
                                 capture_output=True, timeout=15, check=False)
        assert invalid.returncode == 2 and json.loads(invalid.stdout)["ok"] is False
print(f"Prefix CLI contract passed: {total} ordered filter cases plus literal/Unicode probes")
