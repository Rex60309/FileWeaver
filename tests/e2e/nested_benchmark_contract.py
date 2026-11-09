"""Verify nested Unicode duplicates, path tie cutoffs and independent oracles."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

if not __debug__:
    raise RuntimeError("Nested benchmark checks require Python without -O")

module_directory = Path(__file__).resolve().parents[2] / "benchmarks" / "search"
sys.path.insert(0, str(module_directory))
from benchmark_search import matches

assert matches("ABC日本語", "substring", "abc日本語")
assert not matches("é.txt", "substring", "É")  # No accidental Unicode case folding.

cli, info, core = sys.argv[1:4]
script = module_directory / "benchmark_search.py"
with tempfile.TemporaryDirectory(prefix="fileweaver-nested-test-") as temporary:
    output = Path(temporary) / "nested.json"
    command = [sys.executable, str(script), "--cli", cli, "--core-runner", core,
               "--cli-memory", "--dataset", "nested-unicode", "--build-info", info,
               "--files", "103", "--repetitions", "2", "--warmups", "1", "--smoke",
               "--output", str(output)]
    completed = subprocess.run(command, capture_output=True, timeout=120, check=False)
    assert completed.returncode == 0, completed.stderr
    text = output.read_text(encoding="utf-8")
    report = json.loads(text)
    assert report["report_version"] == 4
    assert report["dataset"]["profile"] == "nested-unicode"
    assert report["dataset"]["directories"] == 27 and report["dataset"]["distinct_names"] == 35
    assert len(report["raw"]) == 96 and len(report["summaries"]) == 8
    assert "日本語" in text and "fileweaver-benchmark-" not in text
    labels = {row["method"] for row in report["raw"]}
    assert "python_recursive_directory_scan" in labels and "python_flat_directory_scan" not in labels
    expected_counts = {("exact", "檔案_日本語_é😀_000000_類0.txt"): 3,
                       ("exact", "檔案_日本語_é😀_000034_類4.txt"): 1,
                       ("exact", "不存在.txt"): 0, ("substring", "日本語"): 100,
                       ("substring", "_類3"): 12, ("substring", "不存在"): 0}
    for row in report["raw"]:
        assert row["correct"] is True and row["returned_count"] == expected_counts[(row["mode"], row["query"])]
    assert len(report["cli_memory"]["samples"]) == 6
    assert all(row["correct"] for row in report["cli_memory"]["samples"])

    boundary = command.copy()
    boundary[boundary.index("103")] = "1"
    boundary[-1] = str(Path(temporary) / "one.json")
    one = subprocess.run(boundary, capture_output=True, timeout=120, check=False)
    assert one.returncode == 0, one.stderr
    one_report = json.loads(Path(boundary[-1]).read_text(encoding="utf-8"))
    assert one_report["dataset"]["directories"] == 2 and one_report["dataset"]["distinct_names"] == 1
    assert all(row["returned_count"] in (0, 1) for row in one_report["raw"])

    invalid = command.copy()
    invalid[invalid.index("nested-unicode")] = "unknown-profile"
    invalid[-1] = str(Path(temporary) / "invalid.json")
    rejected = subprocess.run(invalid, capture_output=True, timeout=30, check=False)
    assert rejected.returncode != 0 and not Path(invalid[-1]).exists()
    native_rejected = subprocess.run([core, "--files", "1", "--root", temporary,
                                      "--database", str(Path(temporary) / "absent.sqlite"),
                                      "--dataset", "unknown-profile"],
                                     capture_output=True, timeout=15, check=False)
    assert native_rejected.returncode != 0 and not native_rejected.stdout
print("Nested Unicode benchmark contract passed")
