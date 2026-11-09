"""Exercise real child lifetime peaks, output draining and timeout cleanup."""

import json
import os
from pathlib import Path
import subprocess
import sys
from unittest import mock

if not __debug__:
    raise RuntimeError("Memory checks require Python without -O")

module_directory = Path(__file__).resolve().parents[2] / "benchmarks" / "search"
sys.path.insert(0, str(module_directory))
import cli_memory
from cli_memory import run_observed, summarize_readings

if os.name == "nt":
    child = """
import json, os, sys
sys.path.insert(0, sys.argv[1])
from cli_memory import WindowsProcessMemory
data = bytearray(16 * 1024 * 1024)
for offset in range(0, len(data), 4096):
    data[offset] = 1
observer = WindowsProcessMemory()
handle, code = observer.open(os.getpid())
assert handle, code
try:
    reading = observer.read(handle)
finally:
    observer.close(handle)
print(json.dumps(reading), flush=True)
del data
"""
    completed, memory = run_observed([sys.executable, "-c", child, str(module_directory)], timeout=15)
    assert completed.returncode == 0 and not completed.stderr, completed.stderr
    inside = json.loads(completed.stdout)
    assert inside["status"] == memory["status"] == "available"
    assert memory["peak_bytes"] >= inside["peak_bytes"] >= 16 * 1024 * 1024
else:
    completed, memory = run_observed([sys.executable, "-c", "print('fallback')"], timeout=15)
    assert completed.returncode == 0 and memory["status"] == "unsupported_platform"
    assert memory["peak_bytes"] is None

failed, _ = run_observed([sys.executable, "-c", "import sys; print('diagnostic', file=sys.stderr); sys.exit(7)"], timeout=15)
assert failed.returncode == 7 and b"diagnostic" in failed.stderr
large, _ = run_observed([sys.executable, "-c", "import sys; sys.stdout.write('x' * 262144); sys.stderr.write('y' * 262144)"], timeout=15)
assert large.returncode == 0 and len(large.stdout) == len(large.stderr) == 262144
created = []
real_popen = subprocess.Popen


def capture_child(*args, **kwargs):
    process = real_popen(*args, **kwargs)
    created.append(process)
    return process


with mock.patch.object(cli_memory.subprocess, "Popen", side_effect=capture_child):
    try:
        run_observed([sys.executable, "-c", "import time; time.sleep(30)"], timeout=0.03)
    except subprocess.TimeoutExpired:
        pass
    else:
        raise AssertionError("Observed child timeout did not propagate")
assert len(created) == 1 and created[0].returncode is not None
assert created[0].stdout.closed and created[0].stderr.closed

partial = summarize_readings([{"status": "available", "peak_bytes": 4096},
                              {"status": "query_failed", "peak_bytes": None}])
assert partial["status"] == "partial" and partial["successful_observations"] == 1
assert partial["observed_max_bytes"] == 4096
missing = summarize_readings([{"status": "open_process_failed", "peak_bytes": None}])
assert missing["status"] == "unavailable" and missing["observed_max_bytes"] is None
unsupported = summarize_readings([{"status": "unsupported_platform", "peak_bytes": None}])
assert unsupported["status"] == "unsupported_platform" and unsupported["observed_max_bytes"] is None
for invalid in ([], [{"status": "available", "peak_bytes": 0}],
                [{"status": "query_failed", "peak_bytes": 4096}]):
    try:
        summarize_readings(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid memory availability was accepted")
print("CLI memory observation contract passed")
