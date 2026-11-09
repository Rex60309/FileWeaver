"""Correctness-only rollback/recovery faults in an owned temporary database."""

import argparse
from contextlib import closing, contextmanager
from dataclasses import replace
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

from benchmark_search import invoke, require
from verify_filters import Case, Record, cases_for, check_cases, check_search, fixture, utc_ns
from verify_incremental import owned, remove_fixture, write_fixture


@contextmanager
def fixture_database(workspace, database):
    owned(workspace, database)
    require(database.is_file(), "Fault database is not an owned fixture file")
    with closing(sqlite3.connect(database, timeout=5)) as connection:
        connection.execute("PRAGMA foreign_keys=ON")
        yield connection


def install_write_fault(workspace, database, root_id):
    with fixture_database(workspace, database) as connection, connection:
        connection.execute("BEGIN IMMEDIATE")
        connection.execute("CREATE TABLE _fw_fixture_fault_state(root_id INTEGER PRIMARY KEY,n INTEGER NOT NULL)")
        connection.execute("INSERT INTO _fw_fixture_fault_state VALUES(?,0)", (root_id,))
        connection.execute("""CREATE TRIGGER _fw_fixture_fail_write BEFORE INSERT ON files
            WHEN NEW.root_id=(SELECT root_id FROM _fw_fixture_fault_state)
            BEGIN
              UPDATE _fw_fixture_fault_state SET n=n+1;
              SELECT CASE WHEN (SELECT n FROM _fw_fixture_fault_state)>=2
                THEN RAISE(ABORT,'fixture failure after first metadata write') END;
            END""")


def counter_rolled_back(workspace, database):
    with fixture_database(workspace, database) as connection:
        require(connection.execute("SELECT n FROM _fw_fixture_fault_state").fetchone() == (0,),
                "Failed writer did not roll back its first statement/counter")


def remove_write_fault(workspace, database):
    counter_rolled_back(workspace, database)
    with fixture_database(workspace, database) as connection, connection:
        connection.execute("BEGIN IMMEDIATE")
        connection.execute("DROP TRIGGER _fw_fixture_fail_write")
        connection.execute("DROP TABLE _fw_fixture_fault_state")


def set_fixture_depth(workspace, database, root_id, *, previous, depth):
    with fixture_database(workspace, database) as connection, connection:
        connection.execute("BEGIN IMMEDIATE")
        changed = connection.execute("UPDATE root_scan_options SET maximum_depth=? WHERE root_id=? AND maximum_depth=?",
                                     (depth, root_id, previous)).rowcount
        require(changed == 1, "Synthetic policy precondition changed")


def failed_command(cli, arguments):
    process = subprocess.run([str(cli), *map(str, arguments), "--json"], capture_output=True, timeout=30, check=False)
    document = json.loads(process.stdout.decode("utf-8", errors="strict"))
    require(process.returncode == 1 and document["ok"] is False and document["exit_code"] == 1
            and bool(process.stderr), "Expected one failure JSON document and exit 1")
    return document, process.stderr.decode("utf-8", errors="strict")


def run(args):
    require(3 <= args.per_root <= 1024, "--per-root must be 3..1024")
    cli = Path(args.cli).resolve(strict=True)
    info = Path(args.build_info).resolve(strict=True)
    require(info.parent == cli.parent, "Build info must come from the CLI build directory")
    output = Path(args.output)
    require(not output.exists(), "Output already exists; choose a new report path")
    checked, scans, failures, statuses = [], [], [], []
    with tempfile.TemporaryDirectory(prefix="fileweaver-recovery-") as temporary:
        workspace = Path(temporary).resolve()
        roots, original = fixture(workspace, args.per_root)
        a = [record for record in original if record.root == 0]
        b = [record for record in original if record.root == 1]
        a0 = replace(a[0], size=4096, modified_ns=utc_ns("2024-01-05T00:00:00Z"))
        a2 = replace(a[2], size=1, modified_ns=utc_ns("2024-01-06T00:00:00Z"))
        b0 = replace(b[0], size=127, modified_ns=utc_ns("2024-01-08T00:00:00Z"))
        created = Record(roots[0] / "新增 空間" / "檔案_日本語_新建😀.md", 0, 127, utc_ns("2024-01-07T00:00:00Z"))
        top = Record(roots[0] / "檔案_日本語_頂層😀.txt", 0, 1, utc_ns("2024-01-10T00:00:00Z"))
        partial = [b0 if record == b[0] else record for record in original]
        recovered = [a0 if record == a[0] else a2 if record == a[2] else b0 if record == b[0] else record
                     for record in original if record != a[1]] + [created]
        final = recovered + [top]
        cases = cases_for(original) + [Case(query=created.path.name, mode="exact", root="A"),
                                      Case(query=top.path.name, mode="exact", root="A"),
                                      Case(query=a[0].path.name, mode="exact", maximum=0, root="A"),
                                      Case(query=a[0].path.name, mode="exact", minimum=4096, root="A")]
        database = workspace / "故障 索引😀.sqlite"
        invoke(cli, ["init", "--database", database], timeout=30)

        def complete_scan(document, phase, root_index, expected, files):
            require((document["added"], document["updated"], document["missing"]) == expected
                    and document["missing_reconciled"] is True
                    and document["scan"] == {"files": files, "skipped": 0, "errors": 0,
                                              "complete": True, "cancelled": False}, "Unexpected complete scan summary")
            scans.append({"phase": phase, "root": "A" if root_index == 0 else "B", "kind": "complete",
                          "added": expected[0], "updated": expected[1], "missing": expected[2], "files": files})

        for index, root in enumerate(roots):
            complete_scan(invoke(cli, ["index", root, "--database", database], timeout=30),
                          "initial", index, (args.per_root, 0, 0), args.per_root)
        registered = invoke(cli, ["roots", "--database", database], timeout=30)["roots"]
        root_ids = {entry["path"]: entry["root_id"] for entry in registered}
        require(set(root_ids) == {root.as_posix() for root in roots}, "Unexpected fixture roots")
        normal_depth = next(entry["maximum_depth"] for entry in registered if entry["path"] == roots[0].as_posix())
        require(normal_depth >= 3, "Fixture requires complete two-level traversal")

        def phase_check(phase, records, *, missing, scan_count, root_status, depth):
            checked.extend(check_cases(cli, database, workspace=workspace, roots=roots, root_ids=root_ids,
                                       records=records, cases=cases, phase=phase))
            rows = check_search(cli, database, workspace=workspace, roots=roots, root_ids=root_ids,
                                records=records, case=Case(limit=10000))
            status = invoke(cli, ["status", "--database", database], timeout=30)
            require(status["schema_version"] == 3 and status["roots"] == 2
                    and status["present_files"] == len(records) and status["missing_files"] == missing
                    and status["scan_records"] == scan_count and status["running_scan_records"] == 0,
                    "Unexpected retained/recovered catalog status")
            inspected = {entry["path"]: entry for entry in invoke(cli, ["roots", "--database", database], timeout=30)["roots"]}
            require(set(inspected) == set(root_ids), "Fault changed registered roots")
            for index, root in enumerate(roots):
                entry = inspected[root.as_posix()]
                require(entry["root_id"] == root_ids[root.as_posix()] and entry["policy_known"] is True
                        and entry["exclusions"] == [] and entry["last_scan_status"] == root_status[index]
                        and entry["maximum_depth"] == (depth if index == 0 else normal_depth), "Unexpected root status/policy")
            statuses.append({"phase": phase, "present_files": len(records), "missing_files": missing,
                             "total_records": len(records) + missing, "scan_records": scan_count,
                             "root_a_status": root_status[0], "root_b_status": root_status[1], "root_a_depth": depth})
            return {row["path"]: row["file_id"] for row in rows}

        initial_ids = phase_check("initial", original, missing=0, scan_count=2,
                                  root_status=("completed", "completed"), depth=normal_depth)
        write_fixture(workspace, a0, previous=a[0])
        write_fixture(workspace, a2, previous=a[2])
        remove_fixture(workspace, a[1])
        write_fixture(workspace, created)
        write_fixture(workspace, b0, previous=b[0])
        install_write_fault(workspace, database, root_ids[roots[0].as_posix()])
        _, diagnostic = failed_command(cli, ["index", roots[0], "--database", database])
        require("fixture failure after first metadata write" in diagnostic, "Did not reach second synthetic write")
        counter_rolled_back(workspace, database)
        failed_ids = phase_check("failed_index", original, missing=0, scan_count=3,
                                root_status=("failed", "completed"), depth=normal_depth)
        require(failed_ids == initial_ids, "Failed index lost old paths/IDs")
        failures.append({"phase": "failed_index", "exit_code": 1, "counter_after_rollback": 0,
                         "failure_after_first_statement": True})

        document, _ = failed_command(cli, ["refresh", "--database", database])
        entries = {entry["path"]: entry for entry in document["roots"]}
        require(set(entries) == set(root_ids) and document["command"] == "refresh"
                and document["cancelled"] is False, "Unexpected partial refresh roots")
        bad, good = entries[roots[0].as_posix()], entries[roots[1].as_posix()]
        require(bad["attempted"] is True and bad["exit_code"] == 1 and "index" not in bad
                and "fixture failure after first metadata write" in bad["error"]
                and good["attempted"] is True and good["exit_code"] == 0
                and bad["root_id"] == root_ids[roots[0].as_posix()]
                and good["root_id"] == root_ids[roots[1].as_posix()], "Partial refresh hid root outcome")
        complete_scan(good["index"], "partial_refresh", 1, (0, 1, 0), args.per_root)
        counter_rolled_back(workspace, database)
        partial_ids = phase_check("partial_refresh", partial, missing=0, scan_count=5,
                                 root_status=("failed", "completed"), depth=normal_depth)
        require(partial_ids == initial_ids, "Partial refresh changed existing path identities")
        failures.append({"phase": "partial_refresh", "exit_code": 1, "failed_root": "A",
                         "healthy_root_updated": 1, "counter_after_rollback": 0})

        remove_write_fault(workspace, database)
        complete_scan(invoke(cli, ["index", roots[0], "--database", database], timeout=30),
                      "write_recovery", 0, (1, 2, 1), args.per_root)
        recovered_ids = phase_check("write_recovery", recovered, missing=1, scan_count=6,
                                   root_status=("completed", "completed"), depth=normal_depth)
        require(all(recovered_ids[path] == initial_ids[path] for path in recovered_ids.keys() & initial_ids.keys())
                and recovered_ids[created.path.as_posix()] not in initial_ids.values(), "Write recovery lost stable/new IDs")

        write_fixture(workspace, top)
        set_fixture_depth(workspace, database, root_ids[roots[0].as_posix()], previous=normal_depth, depth=1)
        document, _ = failed_command(cli, ["refresh", "--database", database])
        entries = {entry["path"]: entry for entry in document["roots"]}
        require(set(entries) == set(root_ids) and document["command"] == "refresh"
                and document["cancelled"] is False, "Unexpected incomplete refresh roots")
        bad, good = entries[roots[0].as_posix()], entries[roots[1].as_posix()]
        summary = bad["index"]
        require(bad["attempted"] is True and bad["exit_code"] == 1 and summary["ok"] is False
                and summary["scan"]["complete"] is False and summary["scan"]["cancelled"] is False
                and summary["scan"]["files"] == 1 and summary["scan"]["errors"] >= 1
                and summary["scan"]["skipped"] >= summary["scan"]["errors"]
                and (summary["added"], summary["updated"], summary["missing"]) == (0, 0, 0)
                and summary["missing_reconciled"] is False, "Incomplete scan reported committed changes")
        require(good["attempted"] is True and good["exit_code"] == 0
                and bad["root_id"] == root_ids[roots[0].as_posix()]
                and good["root_id"] == root_ids[roots[1].as_posix()], "Incomplete root blocked healthy root")
        scans.append({"phase": "incomplete_refresh", "root": "A", "kind": "incomplete",
                      "observed_files": 1, "errors": summary["scan"]["errors"], "committed_changes": 0})
        complete_scan(good["index"], "incomplete_refresh", 1, (0, 0, 0), args.per_root)
        incomplete_ids = phase_check("incomplete_refresh", recovered, missing=1, scan_count=8,
                                    root_status=("incomplete", "completed"), depth=1)
        require(incomplete_ids == recovered_ids, "Incomplete traversal leaked partial metadata")
        failures.append({"phase": "incomplete_refresh", "exit_code": 1, "observed_files": 1,
                         "committed_changes": 0, "prior_ids_retained": True})

        set_fixture_depth(workspace, database, root_ids[roots[0].as_posix()], previous=1, depth=normal_depth)
        document = invoke(cli, ["refresh", "--database", database], timeout=30)
        entries = {entry["path"]: entry for entry in document["roots"]}
        require(set(entries) == set(root_ids) and document["command"] == "refresh"
                and document["cancelled"] is False, "Recovery changed root membership")
        for index, root in enumerate(roots):
            entry = entries[root.as_posix()]
            require(entry["attempted"] is True and entry["exit_code"] == 0
                    and entry["root_id"] == root_ids[root.as_posix()], "Depth recovery root failed")
            complete_scan(entry["index"], "depth_recovery", index, (1, 0, 0) if index == 0 else (0, 0, 0),
                          args.per_root + 1 if index == 0 else args.per_root)
        final_ids = phase_check("depth_recovery", final, missing=1, scan_count=10,
                               root_status=("completed", "completed"), depth=normal_depth)
        require(all(final_ids[path] == value for path, value in recovered_ids.items())
                and final_ids[top.path.as_posix()] not in recovered_ids.values(), "Depth recovery lost stable/new IDs")
        with fixture_database(workspace, database) as connection:
            require(connection.execute("SELECT count(*) FROM sqlite_schema WHERE name IN (?,?)",
                                       ("_fw_fixture_fail_write", "_fw_fixture_fault_state")).fetchone() == (0,)
                    and connection.execute("PRAGMA user_version").fetchone() == (3,), "Synthetic fault objects/version were not restored")
    report = {"report_version": 1, "kind": "failure_recovery_correctness_only_not_performance",
              "created_at_utc": datetime.now(timezone.utc).isoformat(), "build": json.loads(info.read_text(encoding="utf-8")),
              "binary_sha256": hashlib.sha256(cli.read_bytes()).hexdigest(),
              "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "helpers_sha256": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                 for name in ("benchmark_search.py", "verify_filters.py", "verify_incremental.py")},
              "fixture": {"roots": 2, "files_per_root": args.per_root, "initial_files": len(original)},
              "cases_per_phase": len(cases), "phases": 6, "checked": checked, "scan_checks": scans,
              "failure_checks": failures, "status_checks": statuses, "fault_objects_removed": True,
              "identity_checks": {"failed_retention": True, "partial_refresh": True, "write_recovery": True,
                                  "incomplete_retention": True, "depth_recovery": True},
              "limitations": ["Synthetic SQLite write fault and depth limit only", "No permission/disk-full/corruption/cancellation/race claim",
                              "Per-root transactions, not batch-wide atomic rollback", "No timing/performance claim"]}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"Verified {len(checked)} searches and {len(failures)} failure paths with recovery; report {output.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True)
    parser.add_argument("--build-info", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--per-root", type=int, default=24)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, RuntimeError, ValueError, sqlite3.Error, subprocess.SubprocessError) as error:
        print(f"Failure-recovery verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
