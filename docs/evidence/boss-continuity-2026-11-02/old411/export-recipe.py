import difflib
import hashlib
import io
import json
import pathlib
import re
import subprocess
import tarfile

root = pathlib.Path(__file__).resolve().parents[2]
out = pathlib.Path(__file__).resolve().parent
assert out.is_relative_to((root / 'target').resolve())
commit = '411bc6384c06aef80cba888d4443fb01b2291903'
paths = ['Cargo.toml', 'Cargo.lock', 'crates/abi', 'crates/game', 'crates/ui', 'crates/boss']
blob = subprocess.check_output(['git', 'archive', commit, *paths], cwd=root)
original = {}
with tarfile.open(fileobj=io.BytesIO(blob)) as archive:
    for member in archive.getmembers():
        if not member.isfile():
            continue
        target = (out / member.name).resolve()
        assert target.is_relative_to(out), member.name
        data = archive.extractfile(member).read()
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        original[member.name] = data

def text(name):
    return original[name].decode().replace('\r\n', '\n')

def write(name, value):
    if b'\r\n' in original[name]:
        value = value.replace('\n', '\r\n')
    (out / name).write_bytes(value.encode())

workspace = re.sub(r'(?m)^members = .*$', 'members = ["crates/abi", "crates/game", "crates/boss", "crates/ui"]', text('Cargo.toml'))
write('Cargo.toml', workspace)
manifest = text('crates/boss/Cargo.toml').replace('[dependencies]', '[features]\ncontinuity-fixture = []\n\n[dependencies]', 1)
write('crates/boss/Cargo.toml', manifest)
hook = '''#[cfg(feature = "continuity-fixture")]
static NEXT_RENDER_FAULT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);

#[cfg(feature = "continuity-fixture")]
#[unsafe(no_mangle)]
pub extern "C" fn eroverlay_test_boss_fault(mask: u32) -> i32 {
    if mask & !3 != 0 {
        return abi::ERR_INVALID;
    }
    NEXT_RENDER_FAULT.store(mask, Ordering::Relaxed);
    abi::OK
}
'''
fault = '''        #[cfg(feature = "continuity-fixture")]
        let fault = NEXT_RENDER_FAULT.swap(0, Ordering::Relaxed);
        #[cfg(feature = "continuity-fixture")]
        let _snapshot_fault = (fault & 1 != 0)
            .then(|| instance.snapshot.lock().unwrap_or_else(|e| e.into_inner()));
        #[cfg(feature = "continuity-fixture")]
        let _ui_fault = (fault & 2 != 0)
            .then(|| instance.ui.lock().unwrap_or_else(|e| e.into_inner()));
'''
source = text('crates/boss/src/lib.rs')
anchor = 'fn guard(f: impl FnOnce() -> i32) -> i32 {'
assert source.count(anchor) == 1
source = source.replace(anchor, hook + anchor, 1)
anchor = '        let instance = unsafe { &*instance.cast::<Instance>() };\n        let snapshot = instance.snapshot.try_lock().ok().and_then(|s| s.clone());'
assert source.count(anchor) == 1
source = source.replace(anchor, anchor.split('\n')[0] + '\n' + fault + '\n'.join(anchor.split('\n')[1:]), 1)
assert source.replace(hook, '', 1).replace(fault, '', 1) == text('crates/boss/src/lib.rs')
write('crates/boss/src/lib.rs', source)
changed = [name for name, data in original.items() if name.startswith('crates/') and (out / name).read_bytes() != data]
assert sorted(changed) == ['crates/boss/Cargo.toml', 'crates/boss/src/lib.rs'], changed
diffs = []
for name in changed:
    diffs.extend(difflib.unified_diff(text(name).splitlines(True), (out / name).read_text().splitlines(True), fromfile='411bc638/' + name, tofile='test-fixture/' + name))
(out / 'fixture-only.patch').write_text(''.join(diffs), encoding='utf-8')
receipt = {
    'source_commit': commit,
    'source': 'git archive of pinned objects; no refs changed',
    'exported_paths': paths,
    'changed_crate_files': changed,
    'hook_removal_recovers_original_source': True,
    'constraint': 'All ABI/game/UI/Boss sources are pinned old source. Only cfg(feature=continuity-fixture) lock fault controls were backported. The old snapshot.try_lock().ok()->None->OK-empty render remains unchanged. Root workspace members trimmed for an isolated build; Cargo.lock is resolved offline then built --locked. No game assets are included.',
    'commands': ['python target/continuity-old411/export-recipe.py', 'cd target/continuity-old411', 'cargo build -p eroverlay-boss --release --features continuity-fixture --offline --target-dir build-target', 'cargo build -p eroverlay-boss --release --features continuity-fixture --locked --target-dir build-target'],
    'original_sha256': {name: hashlib.sha256(data).hexdigest() for name, data in original.items()},
    'fixture_sha256': {name: hashlib.sha256((out / name).read_bytes()).hexdigest() for name in changed},
}
(out / 'source-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
print(out)
print('Changed crate files:', changed)
print('Exported files:', len(original))
