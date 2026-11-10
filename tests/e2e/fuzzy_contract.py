"""Fuzzy CLI contract: synthetic Unicode metadata, offline results and JSON errors."""
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

if not __debug__:
    raise RuntimeError("Fuzzy checks require Python without -O")
cli = str(Path(sys.argv[1]).resolve())
def invoke(*args, code=0):
    result = subprocess.run([cli, *map(str, args), "--json"], capture_output=True, timeout=30)
    assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
    document = json.loads(result.stdout)  # Rejects multiple/trailing JSON documents.
    return document
with tempfile.TemporaryDirectory(prefix="fileweaver-fuzzy-") as temporary:
    base = Path(temporary).resolve()
    source = base / "資料 日本語😀"
    source.mkdir()
    names = ["report.txt", "reprot.txt", "image_processing_final_report.pdf", "日本語😀.txt", "z_report.txt"]
    for i, name in enumerate(names):
        (source / name).write_bytes(b"x" * (i + 1))
    database = base / "模糊.sqlite"
    invoke("init", "--database", database)
    invoke("index", source, "--database", database)
    def search(query, *args, code=0):
        return invoke("search", query, "--database", database, "--mode", "fuzzy", *args, code=code)
    def found(query, *args):
        return [row["name"] for row in search(query, *args)["results"]]
    assert found("reprot")[0] == "reprot.txt"
    assert "report.txt" in found("reprot")
    assert found("img proc fin") == ["image_processing_final_report.pdf"]
    assert found("日本😀") == ["日本語😀.txt"]
    assert found("report", "--ext", "txt", "--limit", 1) == ["report.txt"]
    assert found("report", "--ext", "pdf") == ["image_processing_final_report.pdf"]
    assert found("report", "--min-size", 5) == ["z_report.txt"]
    filtered = found("report", "--root", source)
    unfiltered = found("report")
    assert filtered == unfiltered, (str(source), filtered, unfiltered)
    assert found("report", "--fuzzy-exact-weight", 0, "--fuzzy-prefix-weight", 0,
                 "--fuzzy-token-weight", 0, "--fuzzy-subsequence-weight", 0,
                 "--fuzzy-typo-weight", 10000)[0] == "reprot.txt"
    assert search("report", "--fuzzy-candidates", 1, code=1)["ok"] is False
    for args in [("--sort", "name"), ("--descending",), ("--fuzzy-candidates", 0),
                 ("--fuzzy-token-weight", -1), ("--fuzzy-typo-weight", 10001)]:
        assert search("report", *args, code=2)["ok"] is False
    for query in [" " * 3, "a" * 129, "a " * 17]:
        assert search(query, code=2)["ok"] is False
    assert search("no_match_zzzzzz")["count"] == 0
    before = database.read_bytes()
    source.rename(base / "offline")
    assert found("img proc fin") == ["image_processing_final_report.pdf"]
    assert database.read_bytes() == before
    # Existing literal behavior is unchanged and ranking mode is exclusive.
    assert invoke("search", "reprot", "--database", database)["count"] == 1
    assert invoke("search", "report", "--database", database, "--sort", "relevance", code=2)["ok"] is False
print("Fuzzy CLI contract passed")
