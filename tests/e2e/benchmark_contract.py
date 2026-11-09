"""Exercise the developer benchmark using only tiny generated fixtures."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if not __debug__:
    raise RuntimeError("Benchmark checks require Python without -O")

script = Path(__file__).resolve().parents[2] / "benchmarks" / "search" / "benchmark_search.py"
cli, info, core = sys.argv[1:4]
with tempfile.TemporaryDirectory(prefix="fileweaver-benchmark-test-") as temporary:
    output = Path(temporary) / "report.json"
    command = [sys.executable, str(script), "--cli", cli, "--build-info", info,
               "--files", "30", "--repetitions", "2", "--warmups", "1", "--smoke",
               "--output", str(output)]
    completed = subprocess.run(command, capture_output=True, timeout=90, check=False)
    assert completed.returncode == 0, completed.stderr
    report = json.loads(output.read_text(encoding="utf-8"))
    assert report["smoke"] is True
    assert report["dataset"]["files"] == 30
    assert len(report["raw"]) == 72 and len(report["summaries"]) == 6
    assert len(report["binary_sha256"]) == 64
    assert report["core_peak_rss_bytes"] is None and report["core_memory"] is None
    assert report["cli_memory"] is None and report["environment"]["cli_peak_rss_bytes"] is None
    assert report["cli_memory_observer_sha256"] is None
    assert all(row["correct"] is True and row["elapsed_ns"] >= 0 for row in report["raw"])
    assert all(row["samples"] == 6 and row["p95_ns"] >= row["p50_ns"] for row in report["summaries"])
    original = output.read_bytes()
    duplicate = subprocess.run(command, capture_output=True, timeout=30, check=False)
    assert duplicate.returncode != 0 and output.read_bytes() == original
    invalid = command.copy()
    invalid[invalid.index("30")] = "0"
    invalid[-1] = str(Path(temporary) / "invalid.json")
    rejected = subprocess.run(invalid, capture_output=True, timeout=30, check=False)
    assert rejected.returncode != 0 and not Path(invalid[-1]).exists()
    if json.loads(Path(info).read_text(encoding="utf-8"))["build_type"] != "Release":
        no_smoke = [item for item in invalid if item != "--smoke"]
        no_smoke[no_smoke.index("0")] = "30"
        assert subprocess.run(no_smoke, capture_output=True, timeout=30, check=False).returncode != 0
    core_output = Path(temporary) / "core-report.json"
    core_command = [*command[:-1], str(core_output), "--core-runner", core]
    measured = subprocess.run(core_command, capture_output=True, timeout=90, check=False)
    assert measured.returncode == 0, measured.stderr
    core_report = json.loads(core_output.read_text(encoding="utf-8"))
    assert core_report["report_version"] == 2
    assert len(core_report["raw"]) == 96 and len(core_report["summaries"]) == 8
    assert len(core_report["core_runner_sha256"]) == 64 and core_report["core_sqlite_version"]
    memory = core_report["core_memory"]
    assert memory["unit"] == "bytes"
    assert memory["scope"] == "process_lifetime_through_workload_before_report_serialization"
    if os.name == "nt" or sys.platform.startswith("linux"):
        assert memory["before_status"] == memory["after_status"] == "available"
        assert type(memory["after_workload_peak_bytes"]) is int
        assert type(memory["before_workload_peak_bytes"]) is int
        assert memory["after_workload_peak_bytes"] >= memory["before_workload_peak_bytes"] > 0
        assert core_report["core_peak_rss_bytes"] == memory["after_workload_peak_bytes"]
        if os.name == "nt":
            assert memory["metric"] == "peak_working_set"
            assert memory["source"] == "GetProcessMemoryInfo.PeakWorkingSetSize"
        else:
            assert memory["metric"] == "peak_resident_set"
            assert memory["source"] == "getrusage(RUSAGE_SELF).ru_maxrss"
    else:
        assert core_report["core_peak_rss_bytes"] is None
        assert memory["after_status"] == "unsupported_platform"
    core_rows = [row for row in core_report["raw"] if row["method"] == "cpp_application_search"]
    assert len(core_rows) == 24 and all(row["correct"] for row in core_rows)
    assert {row["phase"] for row in core_rows} == {"first_observed_in_native_process", "warmup", "warm_measured"}
    # A real but incorrect fixture must be rejected; no synthetic success JSON.
    wrong_source = Path(temporary) / "合成 source"
    wrong_source.mkdir()
    fixture_file = wrong_source / "doc_000000_cat0.txt"
    fixture_file.write_bytes(b"")
    wrong_db = Path(temporary) / "wrong.sqlite"
    for args in (["init", "--database", str(wrong_db)],
                 ["index", str(wrong_source), "--database", str(wrong_db)]):
        built = subprocess.run([cli, *args, "--json"], capture_output=True, timeout=30, check=False)
        assert built.returncode == 0, built.stderr
    native_command = [core, "--database", str(wrong_db), "--root", str(wrong_source),
                      "--files", "1", "--repetitions", "2"]
    valid = subprocess.run(native_command, capture_output=True, timeout=30, check=False)
    assert valid.returncode == 0 and len(json.loads(valid.stdout)["raw"]) == 24, valid.stderr
    fixture_file.write_bytes(b"unexpected nonzero size")
    reindexed = subprocess.run([cli, "index", str(wrong_source), "--database", str(wrong_db), "--json"],
                               capture_output=True, timeout=30, check=False)
    assert reindexed.returncode == 0, reindexed.stderr
    wrong = subprocess.run([core, "--database", str(wrong_db), "--root", str(wrong_source),
                            "--files", "1", "--repetitions", "2"], capture_output=True, timeout=30, check=False)
    assert wrong.returncode != 0 and not wrong.stdout and b"oracle" in wrong.stderr
    wrong_limit = subprocess.run([core, "--database", str(wrong_db), "--root", str(wrong_source),
                                  "--files", "0"], capture_output=True, timeout=30, check=False)
    assert wrong_limit.returncode != 0
    observed_output = Path(temporary) / "observed-report.json"
    observed_command = [*command[:-1], str(observed_output), "--cli-memory"]
    observed = subprocess.run(observed_command, capture_output=True, timeout=90, check=False)
    assert observed.returncode == 0, observed.stderr
    observed_report = json.loads(observed_output.read_text(encoding="utf-8"))
    assert observed_report["report_version"] == 3
    assert len(observed_report["cli_memory_observer_sha256"]) == 64
    assert len(observed_report["raw"]) == 72  # Extra memory searches never enter latency samples.
    cli_memory = observed_report["cli_memory"]
    assert cli_memory["total_observations"] == len(cli_memory["samples"]) == 6
    assert cli_memory["capture_point"] == "after_process_exit_with_retained_query_handle"
    assert all(row["correct"] for row in cli_memory["samples"])
    if os.name == "nt":
        assert cli_memory["status"] in ("available", "partial", "unavailable")
        available = [row["peak_bytes"] for row in cli_memory["samples"] if row["status"] == "available"]
        assert cli_memory["successful_observations"] == len(available)
        assert all(type(value) is int and value > 0 for value in available)
        if len(available) == 6:
            assert observed_report["environment"]["cli_peak_rss_bytes"] == max(available)
        else:
            assert observed_report["environment"]["cli_peak_rss_bytes"] is None
    else:
        assert cli_memory["status"] == "unsupported_platform"
        assert observed_report["environment"]["cli_peak_rss_bytes"] is None
print("Benchmark smoke contract passed; these timings are not performance evidence")
