"""Unicode filenames/queries and a hidden Windows legacy-code-page console."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if not __debug__:
    raise RuntimeError("Unicode checks require Python without -O")


def console_probe(cli, database, expected):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateConsoleScreenBuffer.argtypes = [wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                                wintypes.DWORD, ctypes.c_void_p]
    kernel.CreateConsoleScreenBuffer.restype = wintypes.HANDLE
    kernel.ReadConsoleOutputCharacterW.argtypes = [wintypes.HANDLE, wintypes.LPWSTR,
                                                  wintypes.DWORD, wintypes.DWORD,
                                                  ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.SetConsoleOutputCP.argtypes = [wintypes.UINT]
    kernel.SetConsoleActiveScreenBuffer.argtypes = [wintypes.HANDLE]
    assert kernel.SetConsoleOutputCP(950), ctypes.get_last_error()
    handle = kernel.CreateConsoleScreenBuffer(0xC0000000, 3, None, 1, None)
    assert handle and handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
    assert kernel.SetConsoleActiveScreenBuffer(handle), ctypes.get_last_error()
    import msvcrt
    descriptor = msvcrt.open_osfhandle(handle, os.O_WRONLY)
    try:
        process = subprocess.run([cli, "search", "主控台", "--database", database],
                                 stdout=descriptor, stderr=subprocess.PIPE, timeout=15, check=False)
        assert process.returncode == 0 and not process.stderr, process.stderr
        assert kernel.GetConsoleOutputCP() == 950, kernel.GetConsoleOutputCP()
        buffer = ctypes.create_unicode_buffer(8192)
        read = wintypes.DWORD()
        assert kernel.ReadConsoleOutputCharacterW(handle, buffer, 8192, 0, ctypes.byref(read)), ctypes.get_last_error()
        text = buffer[:read.value]
        assert expected in text, f"Legacy-console Unicode mismatch: {text!r}"
    finally:
        os.close(descriptor)


if len(sys.argv) > 1 and sys.argv[1] == "--console-probe":
    console_probe(*sys.argv[2:5])
    sys.exit(0)

cli = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="fileweaver-unicode-") as temporary:
    root = Path(temporary).resolve()
    source = root / "中文 日本語 😀 source"
    source.mkdir()
    name = "報告_日本語 (完成)-é😀.txt"
    selected = source / name
    selected.write_bytes(b"abc")
    database = root / "資料庫_日本語😀.sqlite"
    for arguments in (["init", "--database", database], ["index", source, "--database", database]):
        process = subprocess.run([cli, *map(str, arguments), "--json"], capture_output=True, timeout=20, check=False)
        assert process.returncode == 0, process.stderr
        assert json.loads(process.stdout.decode("utf-8"))["ok"] is True
    for query in ("報告", "日本語", "完成", "é", "😀", name):
        arguments = [cli, "search", query, "--database", str(database)]
        process = subprocess.run([*arguments, "--json"], capture_output=True, timeout=15, check=False)
        assert process.returncode == 0 and not process.stderr, process.stderr
        document = json.loads(process.stdout.decode("utf-8", errors="strict"))
        assert document["count"] == 1 and document["results"][0]["name"] == name
        human = subprocess.run(arguments, capture_output=True, timeout=15, check=False)
        assert human.returncode == 0 and name in human.stdout.decode("utf-8", errors="strict"), (query, human.returncode, human.stdout, human.stderr)
    exact = subprocess.run([cli, "search", name, "--mode", "exact", "--database", str(database), "--json"],
                           capture_output=True, timeout=15, check=False)
    assert exact.returncode == 0 and json.loads(exact.stdout)["results"][0]["name"] == name
    if os.name == "nt":
        console_name = "主控台_日本語 (完成)-é.txt"
        (source / console_name).write_bytes(b"console")
        indexed = subprocess.run([cli, "index", str(source), "--database", str(database), "--json"],
                                 capture_output=True, timeout=20, check=False)
        assert indexed.returncode == 0, indexed.stderr
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0  # SW_HIDE: never show the test console to the user.
        probe = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--console-probe",
                                cli, str(database), console_name], capture_output=True, timeout=30,
                               startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE,
                               check=False)
        assert probe.returncode == 0, probe.stderr
        # Generated names exercise nested directories and punctuation, not user data.
        nested_source = root / "多層目錄"
        nested_source.mkdir()
        for directory in ("ひらがな", "漢字（全形）", "網緑音"):
            child = nested_source / directory
            child.mkdir()
            (child / "合成〖_日本語⌇符号_〗⁉…♡×.mp4").write_bytes(b"fixture")
        nested_db = root / "nested.sqlite"
        for arguments in (["init", "--database", "nested.sqlite"],
                          ["index", str(nested_source), "--database", "nested.sqlite"]):
            process = subprocess.run([cli, *arguments, "--json"], cwd=root,
                                     capture_output=True, timeout=20, check=False)
            assert process.returncode == 0, process.stderr
        malformed = nested_source / "漢字（全形）" / ("invalid-" + chr(0xD800) + ".txt")
        malformed.write_bytes(b"invalid Unicode name fixture")
        (nested_source / "new-valid.txt").write_bytes(b"must roll back")
        failed = subprocess.run([cli, "index", str(nested_source), "--database", str(nested_db), "--json"],
                                capture_output=True, timeout=20, check=False)
        assert failed.returncode == 1, failed.stderr
        diagnostic = failed.stderr.decode("utf-8", errors="strict")
        assert "Cannot encode filesystem path as UTF-8" in diagnostic, diagnostic
        assert "invalid-\\uD800.txt" in diagnostic, diagnostic
        assert json.loads(failed.stdout)["ok"] is False
        preserved = subprocess.run([cli, "search", ".", "--database", str(nested_db), "--json"],
                                   capture_output=True, timeout=15, check=False)
        assert preserved.returncode == 0, preserved.stderr
        rows = json.loads(preserved.stdout)["results"]
        assert len(rows) == 3 and all(row["name"] != "new-valid.txt" for row in rows)
        assert malformed.read_bytes() == b"invalid Unicode name fixture"
print("Unicode contract passed: multilingual names, queries, UTF-8 pipes and applicable native console")
