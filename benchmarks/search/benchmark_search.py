"""Synthetic search baselines with optional native application timing; stdlib driver."""

import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import heapq
import json
import math
import os
from pathlib import Path
import platform
import sqlite3
import subprocess
import sys
import tempfile
import time
from cli_memory import run_observed, summarize_readings


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def hardware():
    info = {"os": platform.platform(), "architecture": platform.machine(),
            "cpu": platform.processor() or "unknown", "logical_cpus": os.cpu_count(),
            "ram_bytes": None, "driver_peak_rss_bytes": None, "cli_peak_rss_bytes": None}
    if os.name == "nt":
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
                info["cpu"] = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()

            class Memory(ctypes.Structure):
                _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong),
                            *[(name, ctypes.c_ulonglong) for name in
                              ("physical", "available", "page", "available_page", "virtual",
                               "available_virtual", "extended")]]
            memory = Memory()
            memory.length = ctypes.sizeof(memory)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
                info["ram_bytes"] = memory.physical
            class Counters(ctypes.Structure):
                _fields_ = [("length", ctypes.c_ulong), ("faults", ctypes.c_ulong),
                            *[(name, ctypes.c_size_t) for name in
                              ("peak_working_set", "working_set", "peak_paged", "paged",
                               "peak_nonpaged", "nonpaged", "pagefile", "peak_pagefile")]]
            counters = Counters()
            counters.length = ctypes.sizeof(counters)
            current = ctypes.windll.kernel32.GetCurrentProcess
            current.restype = ctypes.c_void_p
            memory_info = ctypes.windll.psapi.GetProcessMemoryInfo
            memory_info.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong]
            if memory_info(current(), ctypes.byref(counters), counters.length):
                info["driver_peak_rss_bytes"] = counters.peak_working_set
        except (OSError, AttributeError):
            pass
    else:
        try:
            info["ram_bytes"] = os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
        except (OSError, ValueError, AttributeError):
            pass
    try:
        if os.name != "nt":
            import resource
            peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
            info["driver_peak_rss_bytes"] = peak if sys.platform == "darwin" else peak * 1024
    except (ImportError, OSError):
        pass
    return info


def name_of(index):
    return f"doc_{index:06d}_cat{index % 10}.txt"


def relative_of(index, dataset):
    if dataset == "flat-ascii":
        return Path(name_of(index))
    group = index // 3
    name = f"檔案_日本語_é😀_{group:06d}_類{group % 10}.txt"
    return Path(f"資料 目錄{index % 3}") / f"階層_{group % 8}" / name


def matches(name, mode, query):
    # bytes.lower folds ASCII only, as SQLite lower does in the current contract.
    if mode == "exact":
        return name == query
    folded_name, folded_query = name.encode("utf-8").lower(), query.encode("utf-8").lower()
    return folded_name.startswith(folded_query) if mode == "prefix" else folded_query in folded_name


def cli_document(process):
    require(process.returncode == 0, f"CLI failed: {process.stderr.decode('utf-8', errors='replace')}")
    require(not process.stderr, "Unexpected benchmark CLI warning")
    document = json.loads(process.stdout.decode("utf-8", errors="strict"))
    require(document.get("ok") is True, "CLI returned unsuccessful JSON")
    return document


def invoke(cli, arguments, *, cwd=None, timeout=300):
    process = subprocess.run([str(cli), *map(str, arguments), "--json"],
                             capture_output=True, timeout=timeout, check=False, cwd=cwd)
    return cli_document(process)


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def run(args):
    cli = Path(args.cli).resolve(strict=True)
    core_runner = Path(args.core_runner).resolve(strict=True) if args.core_runner else None
    build_path = Path(args.build_info).resolve(strict=True)
    output = Path(args.output)
    require(not output.exists(), "Output already exists; choose a new report path")
    require(build_path.parent == cli.parent, "Build info must come from the CLI build directory")
    require(core_runner is None or core_runner.parent == cli.parent,
            "Core runner must come from the same build directory as the CLI")
    build = json.loads(build_path.read_text(encoding="utf-8"))
    require(args.smoke or build["build_type"] == "Release", "Measurements require Release; --smoke is test-only")
    require(1 <= args.files <= 100000, "--files must be 1..100000")
    require(2 <= args.repetitions <= 200, "--repetitions must be 2..200")
    require(1 <= args.warmups <= 10, "--warmups must be 1..10")
    nested = args.dataset == "nested-unicode"
    cases = [("exact", relative_of(0, args.dataset).name),
             ("exact", relative_of(args.files - 1, args.dataset).name),
             ("exact", "不存在.txt" if nested else "not-present.txt"),
             ("substring", "日本語" if nested else "doc_000"),
             ("substring", "_類3" if nested else "_cat3"),
             ("substring", "不存在" if nested else "not-present")]
    raw = []
    core_document = None
    cli_memory = None
    with tempfile.TemporaryDirectory(prefix="fileweaver-benchmark-") as temporary:
        workspace = Path(temporary).resolve()
        source = workspace / ("資料 日本語😀 source" if nested else "source")
        source.mkdir()
        start = time.perf_counter_ns()
        directories = set()
        for index in range(args.files):
            relative = relative_of(index, args.dataset)
            if relative.parent != Path(".") and relative.parent not in directories:
                (source / relative.parent).mkdir(parents=True, exist_ok=True)
                directories.update(parent for parent in relative.parents if parent != Path("."))
            (source / relative).touch(exist_ok=False)
        generation_ns = time.perf_counter_ns() - start
        database = workspace / ("索引 日本語😀.sqlite" if nested else "index.sqlite")
        invoke(cli, ["init", "--database", database])
        start = time.perf_counter_ns()
        indexed = invoke(cli, ["index", source, "--database", database])
        indexing_ns = time.perf_counter_ns() - start
        require(indexed["added"] == args.files, "Index count differs from generated fixture")
        expected = {}
        for mode, query in cases:
            candidates = (relative_of(index, args.dataset) for index in range(args.files))
            expected[(mode, query)] = heapq.nsmallest(100, (
                (relative.name, (source / relative).as_posix(), 0) for relative in candidates
                if matches(relative.name, mode, query)))

        def cli_search(mode, query):
            result = invoke(cli, ["search", query, "--mode", mode, "--limit", "100", "--database", database])
            require(result["count"] == len(result["results"]), "Invalid result count")
            return [(item["name"], item["path"], item["size_bytes"]) for item in result["results"]]

        def sqlite_search(mode, query):
            # Only constant predicates are selected; values remain bound.
            predicate = "name=?" if mode == "exact" else "instr(lower(name),lower(?))>0"
            connection = sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)
            try:
                return connection.execute(
                    "SELECT name,display_path,size_bytes FROM files NOT INDEXED WHERE is_present=1 AND " +
                    predicate + " ORDER BY name COLLATE BINARY,normalized_path COLLATE BINARY,root_id,id LIMIT 100",
                    (query,)).fetchall()
            finally:
                connection.close()

        def filesystem_search(mode, query):
            if nested:
                def raise_error(error):
                    raise error

                selected = heapq.nsmallest(100, ((name, (Path(base) / name).as_posix())
                    for base, _, files in os.walk(source, onerror=raise_error, followlinks=False)
                    for name in files if matches(name, mode, query)))
                return [(name, path, Path(path).stat().st_size) for name, path in selected]
            with os.scandir(source) as entries:
                selected = heapq.nsmallest(100, ((entry.name, entry.path) for entry in entries
                    if entry.is_file(follow_symlinks=False) and matches(entry.name, mode, query)))
            return [(name, Path(path).as_posix(), Path(path).stat().st_size) for name, path in selected]

        methods = [("cli_process_json", cli_search), ("python_sqlite_forced_full_scan", sqlite_search),
                   ("python_recursive_directory_scan" if nested else "python_flat_directory_scan", filesystem_search)]
        for phase, rounds in (("first_observed_after_fixture_build", 1), ("warmup", args.warmups),
                              ("warm_measured", args.repetitions)):
            for iteration in range(rounds):
                # Rotate method order to reduce a fixed first-method advantage.
                ordered = methods[iteration % len(methods):] + methods[:iteration % len(methods)]
                for mode, query in cases:
                    for label, method in ordered:
                        start = time.perf_counter_ns()
                        result = method(mode, query)
                        elapsed = time.perf_counter_ns() - start
                        require(result == expected[(mode, query)], f"Incorrect results: {label}/{mode}/{query}")
                        raw.append({"phase": phase, "iteration": iteration, "method": label,
                                    "mode": mode, "query": query, "elapsed_ns": elapsed,
                                    "returned_count": len(result), "correct": True})
        if core_runner:
            profile_arguments = ["--dataset", args.dataset] if nested else []
            completed = subprocess.run([str(core_runner), "--database", str(database), "--root", str(source),
                                       "--files", str(args.files), "--repetitions", str(args.repetitions),
                                       "--warmups", str(args.warmups), *profile_arguments],
                                      capture_output=True, timeout=300, check=False)
            require(completed.returncode == 0 and not completed.stderr,
                    f"Core runner failed: {completed.stderr.decode('utf-8', errors='replace')}")
            core_document = json.loads(completed.stdout.decode("utf-8", errors="strict"))
            require(core_document["method"] == "cpp_application_search", "Unexpected core method")
            require(core_document.get("dataset_profile", "flat-ascii") == args.dataset,
                    "Native fixture profile differs from driver")
            require(len(core_document["raw"]) == 6 * (1 + args.warmups + args.repetitions),
                    "Incomplete core sample count")
            for row in core_document["raw"]:
                require(row["correct"] is True and row["elapsed_ns"] >= 0 and
                        row["returned_count"] == len(expected[(row["mode"], row["query"])]),
                        "Core sample differs from Python fixture oracle")
            raw.extend(core_document["raw"])
            methods.append(("cpp_application_search", None))
        if args.cli_memory:
            readings = []
            for mode, query in cases:
                completed, memory = run_observed([str(cli), "search", query, "--mode", mode,
                                                 "--limit", "100", "--database", str(database), "--json"])
                document = cli_document(completed)
                result = [(item["name"], item["path"], item["size_bytes"]) for item in document["results"]]
                require(document["count"] == len(result) and result == expected[(mode, query)],
                        "Incorrect observed CLI search results")
                readings.append({"mode": mode, "query": query, "correct": True, **memory})
            cli_memory = summarize_readings(readings)
        summaries = []
        for label, _ in methods:
            for mode in ("exact", "substring"):
                samples = [row["elapsed_ns"] for row in raw if row["phase"] == "warm_measured"
                           and row["method"] == label and row["mode"] == mode]
                summaries.append({"method": label, "mode": mode, "samples": len(samples),
                                  "p50_ns": percentile(samples, 0.50), "p95_ns": percentile(samples, 0.95)})
    report = {"report_version": 4 if nested else 3 if args.cli_memory else 2 if core_runner else 1, "created_at_utc": datetime.now(timezone.utc).isoformat(),
              "measurement_kind": "distinct process/Python envelopes plus optional in-process C++ application search",
              "smoke": args.smoke, "build": build,
              "binary_sha256": hashlib.sha256(cli.read_bytes()).hexdigest(),
              "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "core_runner_sha256": hashlib.sha256(core_runner.read_bytes()).hexdigest() if core_runner else None,
              "core_sqlite_version": core_document["sqlite_version"] if core_document else None,
              "core_memory": core_document.get("memory", {"after_status": "helper_does_not_report_memory"}) if core_document else None,
              "core_peak_rss_bytes": core_document.get("memory", {}).get("after_workload_peak_bytes") if core_document else None,
              "cli_memory": cli_memory,
              "cli_memory_observer_sha256": hashlib.sha256(Path(__file__).with_name("cli_memory.py").read_bytes()).hexdigest() if args.cli_memory else None,
              "core_timing_envelope": "search_files call: open/read transaction/SQL/result vector; excludes process startup, request/oracle construction, result destruction, validation and JSON" if core_runner else None,
              "dependency_baseline": json.loads((Path(__file__).resolve().parents[2] / "vcpkg.json").read_text(encoding="utf-8"))["builtin-baseline"],
              "cli_sqlite_version": None,
              "environment": hardware(), "python_version": platform.python_version(),
              "python_sqlite_version": sqlite3.sqlite_version, "storage_kind": args.storage_kind,
              "storage_model": args.storage_model,
              "storage_note": "Operator label for temporary-fixture storage; not automatically verified",
              "cache_note": "No OS cache eviction. Fixture creation/indexing warms caches; first_observed is not cold.",
              "dataset": {"files": args.files, "profile": args.dataset,
                          "directories": len(directories), "distinct_names": (args.files + 2) // 3 if nested else args.files,
                          "shape": "two levels, up to three copies per multilingual basename, zero-byte .txt files" if nested else "one flat directory, unique ASCII names, zero-byte .txt files",
                          "limit": 100, "cases": cases, "generation_ns": generation_ns, "indexing_process_ns": indexing_ns},
              "repetitions": args.repetitions, "warmups": args.warmups,
              "percentile": "nearest-rank over measured samples pooled by method/mode",
              "summaries": summaries, "raw": raw,
              "limitations": ["CLI includes process startup, C++ search, JSON and Python decoding; SQLite/filesystem baselines are Python",
                              "No controlled cold-cache or optimized trigram/fuzzy baseline",
                              "No p95 target compliance claim; storage may be unknown",
                              "Optional C++ method runs after other baselines; its first observation is warm",
                              "Native memory is a process-lifetime peak through workload, not search-only allocations; excludes later report serialization/teardown",
                              "Optional CLI memory pass uses six additional processes, not timed-process memory; unavailable or partial values are explicit"]}
    if cli_memory and cli_memory["status"] == "available":
        report["environment"]["cli_peak_rss_bytes"] = cli_memory["observed_max_bytes"]
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"Correctness verified; report saved as {output.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True)
    parser.add_argument("--core-runner", help="Optional same-build search_core_benchmark executable")
    parser.add_argument("--cli-memory", action="store_true", help="Observe six extra CLI child processes outside timing; Windows memory API")
    parser.add_argument("--build-info", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--files", type=int, default=1000)
    parser.add_argument("--dataset", choices=("flat-ascii", "nested-unicode"), default="flat-ascii")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--storage-kind", choices=("unknown", "ssd", "hdd"), default="unknown")
    parser.add_argument("--storage-model", default="unknown", help="Operator-reported fixture storage model")
    parser.add_argument("--smoke", action="store_true", help="Allow a Debug correctness smoke test; not performance evidence")
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as failure:
        print(f"Benchmark failed: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
