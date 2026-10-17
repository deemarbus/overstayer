#!/usr/bin/env python3
"""Download the pinned validation tool over HTTPS and verify the saved digest."""
import argparse
import hashlib
from pathlib import Path
import urllib.request
import zipfile

DIGESTS = {
    "Linux": "c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352",
    "Windows": "c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab",
}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--platform", choices=DIGESTS, default="Linux")
parser.add_argument("--destination", type=Path, default=Path("/workspace/.tools/pluginval"))
args = parser.parse_args()
args.destination.mkdir(parents=True, exist_ok=True)
filename = f"pluginval_{args.platform}.zip"
archive = args.destination / filename
if not archive.exists():
    urllib.request.urlretrieve(
        f"https://github.com/Tracktion/pluginval/releases/download/v1.0.4/{filename}", archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != DIGESTS[args.platform]:
    raise SystemExit("pluginval checksum mismatch; the artifact will not be executed")
with zipfile.ZipFile(archive) as bundle:
    expected = "pluginval.exe" if args.platform == "Windows" else "pluginval"
    if bundle.namelist() != [expected]:
        raise SystemExit("Unexpected pluginval archive contents")
    executable = args.destination / args.platform / expected
    executable.parent.mkdir(parents=True, exist_ok=True)
    executable.write_bytes(bundle.read(expected))
    executable.chmod(0o755)
print(executable)
