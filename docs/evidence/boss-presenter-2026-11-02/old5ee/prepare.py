"""Backport only the cross-CPU fixture controls to pinned 5ee source."""
import difflib
import hashlib
import json
import subprocess
from pathlib import Path
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = HERE / "source"
FIXTURE = "crates/core/src/layer_fixture.rs"
PIN = "5ee822c78921cd29e7c7252ed3810e563663bd54"

def sha(data):
    return hashlib.sha256(data).hexdigest()

with zipfile.ZipFile(HERE / "source.zip") as archive:
    originals = {i.filename: archive.read(i) for i in archive.infolist() if not i.is_dir()}
original = originals[FIXTURE].decode().replace("\r\n", "\n")
fixture = (ROOT / FIXTURE).read_text()
assert fixture.count(".read()") == 2
assert fixture.count("handle.with_maintenance_lock_for_fixture") == 2
fixture = fixture.replace(".read()", ".lock()")
fixture = fixture.replace("handle.with_maintenance_lock_for_fixture(|| {", "{\n                let _busy = handle.lock().unwrap_or_else(|e| e.into_inner());")
fixture = fixture.replace("            });\n        });\n        let other_cpu", "            }\n        });\n        let other_cpu")
fixture = fixture.replace("handle.with_maintenance_lock_for_fixture(|| context.runtime.render(frame));", "let _busy = handle.lock().unwrap_or_else(|e| e.into_inner());\n                    context.runtime.render(frame);")
assert "with_maintenance_lock_for_fixture" not in fixture
(SOURCE / FIXTURE).write_text(fixture)
subprocess.run(["rustfmt", "--edition", "2024", str(SOURCE / FIXTURE)], check=True)
fixture = (SOURCE / FIXTURE).read_text()
changed = []
manifest = []
for name, data in originals.items():
    actual = (SOURCE / name).read_bytes()
    if name == FIXTURE:
        changed.append(name)
    else:
        assert actual == data, f"unexpected non-fixture production change: {name}"
    manifest.append({"path": name, "original_sha256": sha(data), "fixture_sha256": sha(actual)})
assert changed == [FIXTURE]
(HERE / "fixture-only.patch").write_text("".join(difflib.unified_diff(original.splitlines(True), fixture.splitlines(True), fromfile=f"{PIN}/{FIXTURE}", tofile=f"fixture/{FIXTURE}")), newline="\n")
(HERE / "source-receipt.json").write_text(json.dumps({"pin": PIN, "changed_files": changed, "archive_sha256": sha((HERE / "source.zip").read_bytes()), "build_command": "cargo build -p eroverlay-core --release --features layer-order-fixture --locked --target-dir ../build-target", "all_other_exported_files_exact": True, "files": manifest}, indent=2))
print(f"prepared {len(manifest)} pinned files; only {FIXTURE} differs")
