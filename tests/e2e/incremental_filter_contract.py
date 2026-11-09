"""Exercise incremental identity/filter lifecycle and synthetic mutation containment."""

import json
from dataclasses import replace
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if not __debug__:
    raise RuntimeError("Incremental checks require Python without -O")

module_directory = Path(__file__).resolve().parents[2] / "benchmarks" / "search"
sys.path.insert(0, str(module_directory))
from verify_filters import Record, utc_ns
from verify_incremental import remove_fixture, rename_fixture, write_fixture

cli, info = sys.argv[1:3]
with tempfile.TemporaryDirectory(prefix="fileweaver-incremental-test-") as temporary:
    workspace = Path(temporary).resolve()
    source = Record(workspace / "source.txt", 0, 1, utc_ns("2024-01-01T00:00:00Z"))
    write_fixture(workspace, source)
    with tempfile.TemporaryDirectory(prefix="fileweaver-guard-outside-") as other:
        outside = Path(other) / "sentinel"
        outside.write_bytes(b"SAFE")
        candidate = Record(outside, 0, 4, utc_ns("2024-01-01T00:00:00Z"))
        os.utime(outside, ns=(candidate.modified_ns, candidate.modified_ns))
        escaped_destination = Record(Path(other) / "escaped.txt", 0, 1, source.modified_ns)
        for action in (lambda: write_fixture(workspace, replace(candidate, size=1), previous=candidate),
                       lambda: remove_fixture(workspace, candidate),
                       lambda: rename_fixture(workspace, source, escaped_destination)):
            try:
                action()
            except RuntimeError:
                pass
            else:
                raise AssertionError("Synthetic mutation escaped workspace guard")
        assert outside.read_bytes() == b"SAFE"
        assert source.path.read_bytes() == b"F" and not escaped_destination.path.exists()
    destination = Record(workspace / "collision.txt", 0, 1, source.modified_ns)
    destination.path.write_bytes(b"KEEP")
    try:
        rename_fixture(workspace, source, destination)
    except RuntimeError:
        pass
    else:
        raise AssertionError("Synthetic rename overwrote destination")
    assert source.path.read_bytes() == b"F" and destination.path.read_bytes() == b"KEEP"

    output = workspace / "incremental.json"
    command = [sys.executable, str(module_directory / "verify_incremental.py"), "--cli", cli,
               "--build-info", info, "--per-root", "3", "--output", str(output)]
    completed = subprocess.run(command, capture_output=True, timeout=120, check=False)
    assert completed.returncode == 0, completed.stderr
    text = output.read_text(encoding="utf-8")
    report = json.loads(text)
    assert report["kind"] == "incremental_correctness_only_not_performance"
    assert report["fixture"]["initial_files"] == 6
    assert report["cases_per_phase"] == 124 and report["phases"] == 5
    assert len(report["checked"]) == 620 and all(row["correct"] for row in report["checked"])
    assert len(report["scan_checks"]) == 8 and len(report["status_checks"]) == 5
    assert len(report["identity_snapshots"]) == 5 and all(report["identity_checks"].values())
    assert [row["total_records"] for row in report["status_checks"]] == [6, 6, 8, 8, 8]
    assert [row["missing_files"] for row in report["status_checks"]] == [0, 0, 2, 2, 2]
    expected_targets = {"initial": [0, 0, 1, 0], "stale_before_reindex": [0, 0, 1, 0],
                        "after_index": [1, 1, 0, 1], "unchanged_refresh": [1, 1, 0, 1],
                        "restored_refresh": [0, 0, 0, 1]}
    for phase, expected in expected_targets.items():
        assert [row["returned_count"] for row in report["checked"]
                if row["phase"] == phase and row["case_id"] >= 120] == expected
    assert "fileweaver-incremental-" not in text
    original = output.read_bytes()
    duplicate = subprocess.run(command, capture_output=True, timeout=30, check=False)
    assert duplicate.returncode != 0 and output.read_bytes() == original
    for count in ("2", "1025"):
        invalid = command.copy()
        invalid[invalid.index("3")] = count
        invalid[-1] = str(workspace / f"invalid-{count}.json")
        rejected = subprocess.run(invalid, capture_output=True, timeout=30, check=False)
        assert rejected.returncode != 0 and not Path(invalid[-1]).exists()
print("Incremental filter/identity contract passed")
