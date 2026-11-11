"""Glob/literal-path/fuzzy-component contracts against synthetic offline metadata."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
cli = str(Path(sys.argv[1]).resolve())
def invoke(*args, code=0):
    p = subprocess.run([cli, *map(str, args), "--json"], capture_output=True, timeout=30)
    assert p.returncode == code, (args, p.returncode, p.stdout, p.stderr)
    return json.loads(p.stdout)
with tempfile.TemporaryDirectory(prefix="fileweaver-phase2-search-") as td:
    base = Path(td).resolve()
    source = base / "工程 日本語😀"
    (source / "image_processing").mkdir(parents=True)
    for name in ["alpha.cpp", "beta.cpp", "UPPER.CPP", "報告😀.txt", "a[1].txt"]:
        (source / name).write_text("synthetic", encoding="utf-8")
    (source / "image_processing" / "final_report.txt").write_text("fixture", encoding="utf-8")
    db = base / "index.sqlite"
    invoke("init", "--database", db)
    invoke("index", source, "--database", db)
    def search(q, *args, mode="glob", code=0):
        return invoke("search", q, "--mode", mode, "--database", db, *args, code=code)
    def names(q, *args, mode="glob"):
        return [r["name"] for r in search(q, *args, mode=mode)["results"]]
    assert names("*.cpp") == ["alpha.cpp","beta.cpp"]
    assert names("[ab]*.cpp","--limit",1) == ["alpha.cpp"]
    assert names("*.cpp", "--descending", "--limit", 1) == ["beta.cpp"]
    assert names("*.cpp", "--min-size", 10) == []
    assert names("[!a]*.cpp") == ["beta.cpp"]
    assert names("a[[]1].txt") == ["a[1].txt"]
    assert names("報告?.txt") == ["報告😀.txt"]
    assert search("[z-a]",code=2)["ok"] is False
    assert search("[abc",code=2)["ok"] is False
    assert names("image_processing/FINAL",mode="path") == ["final_report.txt"]
    assert names("image_processing\\final",mode="path") == ["final_report.txt"]
    assert names("img proc fin", "--fuzzy-path", mode="fuzzy") == ["final_report.txt"]
    assert names("img proc fin",mode="fuzzy") == []
    assert search("anything","--fuzzy-path",mode="path",code=2)["ok"] is False
    before = db.read_bytes()
    source.rename(base / "offline")
    assert names("image_processing/final",mode="path") == ["final_report.txt"]
    assert names("*.cpp","--ext","cpp") == ["alpha.cpp","beta.cpp"]
    assert db.read_bytes() == before
print("Phase 2 search contract passed")
