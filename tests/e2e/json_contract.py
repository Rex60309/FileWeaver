"""Strict JSON/UTF-8 CLI contract checks using isolated synthetic fixtures."""

import contextlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

if not __debug__:
    raise RuntimeError("JSON contract checks require Python without optimization (-O)")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f"Duplicate JSON key: {key}"
        result[key] = value
    return result


def reject_constant(value):
    raise AssertionError(f"Nonstandard JSON constant: {value}")


cli = str(Path(sys.argv[1]).resolve())


def run(arguments, code=0, *, prefix=False, warning=False):
    arguments = [str(argument) for argument in arguments]
    command = [cli, "--json", *arguments] if prefix else [cli, *arguments, "--json"]
    process = subprocess.run(command, capture_output=True, timeout=15, check=False)
    assert process.returncode == code, (command, process.returncode, process.stderr)
    document = json.loads(process.stdout.decode("utf-8", errors="strict"),
                          object_pairs_hook=unique_object, parse_constant=reject_constant)
    assert document["ok"] is (code == 0), document
    assert type(document["exit_code"]) is int, document
    assert document["exit_code"] == code, document
    if code or warning:
        assert process.stderr, (command, document)
    else:
        assert not process.stderr, (command, process.stderr)
    return document


def execute(database, statement, parameters=()):
    with contextlib.closing(sqlite3.connect(database)) as connection:
        with connection:
            connection.execute(statement, parameters)


with tempfile.TemporaryDirectory(prefix="fileweaver-json-") as temporary:
    workspace = Path(temporary)
    source = workspace / "資料 source"
    source.mkdir()
    selected = source / "quote ' %_ 文.txt"
    selected.write_bytes(b"abc")
    database = workspace / "index database.sqlite"

    assert run([])["command"] == "help"
    assert run(["--help"])["command"] == "help"
    assert run(["search", "--help"], prefix=True)["command"] == "help"
    assert run(["init", "--database", database], prefix=True)["command"] == "init"
    assert run(["roots", "--database", database])["roots"] == []
    assert run(["status", "--database", database])["present_files"] == 0
    assert run(["refresh", "--database", database])["roots"] == []
    first = run(["index", source, "--database", database])
    assert first["command"] == "index" and first["added"] == 1, first
    assert first["scan"]["complete"] is True and first["missing_reconciled"] is True
    repeated = run(["index", source, "--database", database])
    assert repeated["added"] == 0 and repeated["updated"] == 0
    inspected = run(["roots", "--database", database])
    assert inspected["count"] == 1 and inspected["roots"][0]["policy_known"] is True
    assert inspected["roots"][0]["exclusions"] == []
    assert inspected["roots"][0]["maximum_depth"] == 128
    assert type(inspected["roots"][0]["last_scan_at"]) is int
    stats = run(["status", "--database", database], prefix=True)
    assert stats["schema_version"] == 3 and stats["roots"] == 1 and stats["present_files"] == 1
    refreshed = run(["refresh", "--database", database])
    assert refreshed["command"] == "refresh" and len(refreshed["roots"]) == 1
    assert refreshed["roots"][0]["index"]["updated"] == 0

    arguments = ["search", "quote", "--database", database, "--sort", "size", "--descending",
                 "--ext", "TXT", "--min-size", "3", "--max-size", "3", "--root", source]
    found = run(arguments)
    assert found["command"] == "search" and found["count"] == 1
    record = found["results"][0]
    assert record["path"] == selected.resolve().as_posix(), record
    assert record["name"] == selected.name and record["size_bytes"] == 3
    assert type(record["file_id"]) is int and type(record["root_id"]) is int
    empty = run(["search", "not-found", "--database", database], prefix=True)
    assert empty["results"] == [] and empty["count"] == 0
    deferred = run(["index", source, "--database", database, "--exclude", "absent"], warning=True)
    assert deferred["missing_reconciled"] is False
    assert run(["roots", "--database", database])["roots"][0]["exclusions"] == ["absent"]
    saved = run(["refresh", "--database", database], warning=True)
    assert saved["roots"][0]["index"]["missing_reconciled"] is False
    missing_source = workspace / "missing-source"
    missing_source.mkdir()
    retained = missing_source / "retained.txt"
    retained.write_bytes(b"retained")
    run(["index", missing_source, "--database", database])
    retained.unlink()
    missing_source.rmdir()
    partial = run(["refresh", "--database", database], 1)
    assert len(partial["roots"]) == 2
    assert sorted(root["exit_code"] for root in partial["roots"]) == [0, 3]
    assert all(root["attempted"] is True for root in partial["roots"])
    assert run(["search", "retained", "--database", database])["count"] == 1
    missing_source.mkdir()
    run(["index", missing_source, "--database", database])
    run(["unknown"], 2)
    run(["--unknown"], 2)
    run(["search", "--database", database], 2)
    run(["search", "quote", "--limit", "0", "--database", database], 2)
    run(["search", "quote", "--modified-after", "bad-time", "--database", database], 2)
    run(["search", "quote", "--database", workspace / "absent.sqlite"], 4)
    run(["refresh", "--database", workspace / "absent.sqlite"], 4)
    for inspection in ("roots", "status"):
        run([inspection, "--database", workspace / "absent.sqlite"], 4)
        run([inspection], 2)
    run(["init", "--database", workspace / "absent" / "index.sqlite"], 3)
    run(["--json=false"], 2)
    run(["--help", "extra"], 2)
    corrupt = workspace / "corrupt.sqlite"
    corrupt.write_bytes(b"not a sqlite database")
    run(["search", "quote", "--database", corrupt], 5)
    execute(database, "CREATE TRIGGER reject_json BEFORE INSERT ON files BEGIN SELECT RAISE(ABORT,'json fixture'); END")
    rejected = source / "new.txt"
    rejected.write_bytes(b"new")
    run(["index", source, "--database", database], 1)
    execute(database, "DROP TRIGGER reject_json")
    rejected.unlink()

    # A literal --json after the delimiter is a query, not an output-mode switch.
    literal = subprocess.run([cli, "search", "--database", str(database), "--", "--json"],
                             capture_output=True, timeout=15, check=False)
    assert literal.returncode == 0 and literal.stdout == b"" and literal.stderr == b""
    attached = subprocess.run([cli, "search", "quote", "--database", str(database), "--ext=--json"],
                              capture_output=True, timeout=15, check=False)
    assert attached.returncode == 0 and attached.stdout == b"" and attached.stderr == b""

    execute(database, "UPDATE files SET size_bytes=?", (9223372036854775807,))
    large = run(["search", "quote", "--database", database])
    assert large["results"][0]["size_bytes"] == 9223372036854775807
    execute(database, "UPDATE files SET display_path=CAST(X'ff' AS TEXT)")
    malformed = run(["search", "quote", "--database", database], 1)
    assert "results" not in malformed
    execute(database, "UPDATE roots SET normalized_path=CAST(X'ff' AS TEXT) WHERE id=(SELECT min(id) FROM roots)")
    run(["roots", "--database", database], 1)
    legacy = workspace / "legacy.sqlite"
    run(["init", "--database", legacy])
    run(["index", source, "--database", legacy, "--exclude", "absent"], warning=True)
    execute(legacy, "DROP TABLE root_exclusions")
    execute(legacy, "DROP TABLE root_scan_options")
    execute(legacy, "PRAGMA user_version=2")
    old_roots = run(["roots", "--database", legacy])
    assert old_roots["roots"][0]["policy_known"] is False
    assert old_roots["roots"][0]["maximum_depth"] is None
    assert run(["status", "--database", legacy])["schema_version"] == 2
    assert run(["search", "quote", "--database", legacy])["count"] == 1
    run(["refresh", "--database", legacy], 5)
    run(["init", "--database", legacy])
    unconfirmed = run(["refresh", "--database", legacy], 1)
    assert unconfirmed["roots"][0]["attempted"] is False
    assert unconfirmed["roots"][0]["exit_code"] == 5
    run(["index", source, "--database", legacy, "--exclude", "absent"], warning=True)
    assert run(["refresh", "--database", legacy], warning=True)["ok"] is True

print("JSON contract passed: strict documents, UTF-8, commands, errors and exact int64 values")
