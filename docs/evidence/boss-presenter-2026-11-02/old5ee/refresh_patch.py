"""Refresh readable proof only: do not rewrite source or rebuild the tested DLL."""
import difflib
import hashlib
import json
from pathlib import Path
import zipfile

HERE = Path(__file__).resolve().parent
REL = "crates/core/src/layer_fixture.rs"
receipt = json.loads((HERE / "source-receipt.json").read_text())
with zipfile.ZipFile(HERE / "source.zip") as archive:
    original_bytes = archive.read(REL)
fixture_bytes = (HERE / "source" / REL).read_bytes()
sha = lambda data: hashlib.sha256(data).hexdigest()
record = next(r for r in receipt["files"] if r["path"] == REL)
assert sha(original_bytes) == record["original_sha256"]
assert sha(fixture_bytes) == record["fixture_sha256"]
original = original_bytes.decode().replace("\r\n", "\n")
fixture = fixture_bytes.decode().replace("\r\n", "\n")
start = "struct DriveContext {"
end = "/// # Safety\n/// Offline fixture only; changes transfer scheduling"
assert original.split(start)[0] == fixture.split(start)[0]
assert original.split(end)[1] == fixture.split(end)[1]
patch = "".join(difflib.unified_diff(original.splitlines(True), fixture.splitlines(True), fromfile=f"{receipt['pin']}/{REL}", tofile=f"fixture/{REL}"))
(HERE / "fixture-only.patch").write_text(patch, newline="\n")
proof = {
    "actual_source_bytes_match_preexisting_receipt": True,
    "normalized_change_is_only_busy_fixture_control_region": True,
    "unchanged_region": "Everything before DriveContext and after drive_busy",
    "original_bytes_sha256": sha(original_bytes),
    "fixture_bytes_sha256": sha(fixture_bytes),
    "normalized_original_sha256": sha(original.encode()),
    "normalized_fixture_sha256": sha(fixture.encode()),
    "normalized_patch_sha256": sha(patch.encode()),
    "source_or_dll_rewritten": False,
}
(HERE / "normalized-patch-receipt.json").write_text(json.dumps(proof, indent=2))
print(json.dumps(proof))
