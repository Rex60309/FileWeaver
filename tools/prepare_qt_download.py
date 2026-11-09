"""Prepare pinned vcpkg metadata; retain its exact upstream SHA512 check.

GitHub may return compact JSON. The pinned PCRE2 port hashes the pretty-printed
API response. Normalize only JSON whitespace and require the original digest
before writing the download cache. No version, URL or integrity checks change.
"""
import hashlib
import json
from pathlib import Path
import urllib.request

URL = "https://api.github.com/repos/PCRE2Project/pcre2/contents/deps/sljit?ref=pcre2-10.49"
EXPECTED = "719fa1d8d8e1e749ce6f7370db32818268f84b1376ed9367fd5290ab440d8bea7a6a25a68e55045a45da68b0ce7a55daa0d7acd2f1b11774319b50268c02441c"
NAME = "pcre2-pcre2-10.49-sljit-gitlink.json"


def prepare(directory, response):
    if len(response) > 65536:
        raise ValueError("Dependency metadata exceeds size limit")
    canonical = (json.dumps(json.loads(response), indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    if hashlib.sha512(canonical).hexdigest() != EXPECTED:
        raise ValueError("Pinned PCRE2 metadata SHA512 mismatch; refusing cache write")
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / NAME
    if target.exists() and target.read_bytes() == canonical:
        return
    if target.exists():
        raise ValueError("Unexpected existing metadata cache; refusing overwrite")
    with target.open("xb") as stream:
        stream.write(canonical)


def main():
    destination = Path(__file__).resolve().parents[1] / "build/tools/vcpkg/downloads"
    request = urllib.request.Request(URL, headers={"User-Agent": "FileWeaver-pinned-dependency-check"})
    with urllib.request.urlopen(request, timeout=30) as response:
        payload = response.read(65537)
    prepare(destination, payload)
    print("Pinned PCRE2 metadata verified; original SHA512 retained")


if __name__ == "__main__":
    main()
