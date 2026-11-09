"""Verify synthetic fault containment, per-root rollback, incomplete return and recovery."""

from contextlib import closing
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

if not __debug__:
    raise RuntimeError("Failure recovery checks require Python without -O")

module_directory = Path(__file__).resolve().parents[2] / "benchmarks" / "search"
sys.path.insert(0, str(module_directory))
from verify_failure_recovery import fixture_database

cli, info = sys.argv[1:3]
with tempfile.TemporaryDirectory(prefix="fileweaver-recovery-test-") as temporary:
    workspace = Path(temporary).resolve()
    with tempfile.TemporaryDirectory(prefix="fileweaver-recovery-outside-") as other:
        outside = Path(other) / "outside.sqlite"
        with closing(sqlite3.connect(outside)) as connection:
            connection.execute("PRAGMA user_version=17")
        try:
            with fixture_database(workspace, outside) as connection:
                connection.execute("PRAGMA user_version=18")
        except RuntimeError:
            pass
        else:
            raise AssertionError("Fault injection accepted an outside database")
        with closing(sqlite3.connect(outside)) as connection:
            assert connection.execute("PRAGMA user_version").fetchone() == (17,)
    absent = workspace / "absent.sqlite"
    try:
        with fixture_database(workspace, absent):
            raise AssertionError("Fault helper created an absent database")
    except RuntimeError:
        pass
    assert not absent.exists()

    output = workspace / "recovery.json"
    command = [sys.executable, str(module_directory / "verify_failure_recovery.py"), "--cli", cli,
               "--build-info", info, "--per-root", "3", "--output", str(output)]
    completed = subprocess.run(command, capture_output=True, timeout=120, check=False)
    assert completed.returncode == 0, completed.stderr
    text = output.read_text(encoding="utf-8")
    report = json.loads(text)
    assert report["kind"] == "failure_recovery_correctness_only_not_performance"
    assert report["fixture"]["initial_files"] == 6
    assert report["cases_per_phase"] == 124 and report["phases"] == 6
    assert len(report["checked"]) == 744 and all(row["correct"] for row in report["checked"])
    assert len(report["scan_checks"]) == 8 and len(report["failure_checks"]) == 3
    assert report["fault_objects_removed"] is True and all(report["identity_checks"].values())
    states = report["status_checks"]
    assert len(states) == 6
    assert [row["total_records"] for row in states] == [6, 6, 6, 7, 7, 8]
    assert [row["present_files"] for row in states] == [6, 6, 6, 6, 6, 7]
    assert [row["missing_files"] for row in states] == [0, 0, 0, 1, 1, 1]
    assert [row["scan_records"] for row in states] == [2, 3, 5, 6, 8, 10]
    assert [row["root_a_status"] for row in states] == ["completed", "failed", "failed", "completed", "incomplete", "completed"]
    assert [row["root_a_depth"] for row in states] == [128, 128, 128, 128, 1, 128]
    expected = {"initial": [0, 0, 1, 0], "failed_index": [0, 0, 1, 0], "partial_refresh": [0, 0, 1, 0],
                "write_recovery": [1, 0, 0, 1], "incomplete_refresh": [1, 0, 0, 1], "depth_recovery": [1, 1, 0, 1]}
    for phase, counts in expected.items():
        assert [row["returned_count"] for row in report["checked"]
                if row["phase"] == phase and row["case_id"] >= 120] == counts
    assert "fileweaver-recovery-" not in text
    original = output.read_bytes()
    duplicate = subprocess.run(command, capture_output=True, timeout=30, check=False)
    assert duplicate.returncode != 0 and output.read_bytes() == original
    for count in ("2", "1025"):
        invalid = command.copy()
        invalid[invalid.index("3")] = count
        invalid[-1] = str(workspace / f"invalid-{count}.json")
        rejected = subprocess.run(invalid, capture_output=True, timeout=30, check=False)
        assert rejected.returncode != 0 and not Path(invalid[-1]).exists()
print("Failure recovery contract passed")
