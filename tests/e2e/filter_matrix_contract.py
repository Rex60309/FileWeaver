"""Regression for explicit empty filters plus generated online/offline filter matrix."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

if not __debug__:
    raise RuntimeError("Filter matrix checks require Python without -O")

cli, info = sys.argv[1:3]
script = Path(__file__).resolve().parents[2] / "benchmarks" / "search" / "verify_filters.py"
with tempfile.TemporaryDirectory(prefix="fileweaver-filter-test-") as temporary:
    workspace = Path(temporary)
    source = workspace / "source"
    source.mkdir()
    (source / "data").write_bytes(b"")
    (source / "data.txt").write_bytes(b"")
    database = workspace / "index.sqlite"
    for arguments in (["init", "--database", str(database)], ["index", str(source), "--database", str(database)]):
        setup = subprocess.run([cli, *arguments, "--json"], capture_output=True, timeout=30, check=False)
        assert setup.returncode == 0, setup.stderr
    found = subprocess.run([cli, "--json", "search", "data", "--database", str(database), "--ext", ""],
                           capture_output=True, timeout=15, check=False)
    assert found.returncode == 0, found.stderr
    result = json.loads(found.stdout)
    assert result["count"] == 1 and result["results"][0]["name"] == "data", result
    for option in ("--root", "--modified-after", "--modified-before", "--min-size", "--max-size"):
        invalid = subprocess.run([cli, "--json", "search", "data", "--database", str(database), option, ""],
                                 capture_output=True, timeout=15, check=False)
        assert invalid.returncode == 2 and json.loads(invalid.stdout)["ok"] is False, (option, invalid.stdout)

    report_path = workspace / "matrix.json"
    command = [sys.executable, str(script), "--cli", cli, "--build-info", info,
               "--per-root", "24", "--output", str(report_path)]
    completed = subprocess.run(command, capture_output=True, timeout=120, check=False)
    assert completed.returncode == 0, completed.stderr
    text = report_path.read_text(encoding="utf-8")
    report = json.loads(text)
    assert report["kind"] == "correctness_only_not_performance"
    assert report["fixture"]["files"] == 48 and report["fixture"]["roots"] == 2
    assert report["valid_cases_per_phase"] == 120 and report["invalid_cases"] == 14
    assert len(report["checked"]) == 240 and all(row["correct"] for row in report["checked"])
    online = [row["returned_count"] for row in report["checked"] if row["phase"] == "online"]
    offline = [row["returned_count"] for row in report["checked"] if row["phase"] == "offline_index_snapshot"]
    assert online == offline and max(online) == 48 and min(online) == 0
    assert "fileweaver-filter-matrix-" not in text
    original = report_path.read_bytes()
    duplicate = subprocess.run(command, capture_output=True, timeout=30, check=False)
    assert duplicate.returncode != 0 and report_path.read_bytes() == original
    for per_root in ("1", "0", "1025"):
        boundary = command.copy()
        boundary[boundary.index("24")] = per_root
        boundary[-1] = str(workspace / f"boundary-{per_root}.json")
        checked = subprocess.run(boundary, capture_output=True, timeout=120, check=False)
        if per_root == "1":
            assert checked.returncode == 0, checked.stderr
            assert json.loads(Path(boundary[-1]).read_text(encoding="utf-8"))["fixture"]["files"] == 2
        else:
            assert checked.returncode != 0 and not Path(boundary[-1]).exists()
print("Filter matrix and explicit-empty regression passed")
