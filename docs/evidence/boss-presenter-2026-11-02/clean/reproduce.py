import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
CLONE = ROOT / 'target/boss-presenter-clean-source'
OUT = Path(__file__).resolve().parent
EXPECTED = '9d623d0f4cc9063524b2538938f7505afdd1569f'
ENV = os.environ.copy()
removed = []
for key in list(ENV):
    if key.startswith(('EROVERLAY_', 'BOSS_', 'CARGO_FEATURE_')) or key in (
        'CARGO_TARGET_DIR', 'RUSTFLAGS', 'CARGO_ENCODED_RUSTFLAGS',
    ):
        removed.append(key)
        del ENV[key]

def git(*args):
    return subprocess.check_output(['git', *args], cwd=CLONE, env=ENV, text=True).strip()

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def pe_exports(path):
    b = path.read_bytes()
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    assert b[pe:pe + 4] == b'PE\0\0'
    count, opt_size = struct.unpack_from('<H', b, pe + 6)[0], struct.unpack_from('<H', b, pe + 20)[0]
    opt = pe + 24
    assert struct.unpack_from('<H', b, opt)[0] == 0x20b
    sections = [struct.unpack_from('<IIII', b, opt + opt_size + i * 40 + 8) for i in range(count)]
    def raw(rva):
        for virtual_size, address, raw_size, pointer in sections:
            if address <= rva < address + max(virtual_size, raw_size):
                offset = pointer + rva - address
                assert offset < len(b)
                return offset
        raise AssertionError(f'Unmapped RVA {rva:x}')
    export_rva = struct.unpack_from('<I', b, opt + 112)[0]
    if not export_rva:
        return []
    export = raw(export_rva)
    name_count = struct.unpack_from('<I', b, export + 24)[0]
    names = raw(struct.unpack_from('<I', b, export + 32)[0])
    result = []
    for i in range(name_count):
        offset = raw(struct.unpack_from('<I', b, names + i * 4)[0])
        end = b.index(0, offset)
        result.append(b[offset:end].decode('ascii'))
    return sorted(result)

data = {
    'source_commit': git('rev-parse', 'HEAD'),
    'source_branch': git('branch', '--show-current'),
    'clone_command': 'git clone --no-local --depth 1 --single-branch --branch cindy/gentle-darwin <worktree> target/boss-presenter-clean-source',
    'shallow_clone': git('rev-parse', '--is-shallow-repository') == 'true',
    'source_clean_before': not git('status', '--porcelain', '--untracked-files=normal'),
    'target_existed_before_checks': (CLONE / 'target').exists(),
    'build_features': 'default features only; no fixture or original C++ reader features',
    'removed_inherited_environment_variable_names': sorted(removed),
    'old_cpp_reference_env_set': 'EROVERLAY_CPP_REFERENCE' in ENV,
    'external_game_asset_environment_supplied': any(k.startswith(('EROVERLAY_BOSS_', 'BOSS_')) for k in ENV),
    'cargo_target_dir_env_set': 'CARGO_TARGET_DIR' in ENV,
    'cargo_version': subprocess.check_output(['cargo', '--version'], env=ENV, text=True).strip(),
    'rustc_version': subprocess.check_output(['rustc', '--version'], env=ENV, text=True).strip(),
    'started_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'steps': [],
}
assert data['source_commit'] == EXPECTED, data['source_commit']
assert data['source_branch'] == 'cindy/gentle-darwin'
assert data['source_clean_before'] and not data['target_existed_before_checks']

def save():
    (OUT / 'clean-source-reproduction.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')

def run(step, args):
    print('START ' + step + ': ' + ' '.join(args), flush=True)
    started = time.monotonic()
    log_name = 'clean-' + step + '.log'
    with (OUT / log_name).open('w', encoding='utf-8') as log:
        log.write('$ ' + ' '.join(args) + '\n')
        log.flush()
        result = subprocess.run(args, cwd=CLONE, env=ENV, stdout=log, stderr=subprocess.STDOUT)
        log.write('\nexit_code=' + str(result.returncode) + '\n')
    record = {'id': step, 'command': ' '.join(args), 'exit_code': result.returncode, 'seconds': round(time.monotonic() - started, 3), 'log': log_name}
    data['steps'].append(record)
    save()
    print('DONE ' + step + ': exit=' + str(result.returncode) + ', seconds=' + str(record['seconds']), flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)

save()
run('fmt', ['cargo', 'fmt', '--all', '--', '--check'])
run('check', ['cargo', 'check', '--workspace', '--locked'])
run('clippy', ['cargo', 'clippy', '--workspace', '--all-targets', '--locked', '--', '-D', 'warnings'])
run('test', ['cargo', 'test', '--workspace', '--locked'])
test_text = (OUT / 'clean-test.log').read_text(encoding='utf-8')
counts = re.findall(r'test result: ok\. (\d+) passed; (\d+) failed; (\d+) ignored;', test_text)
assert counts
data['test_totals'] = dict(zip(('passed', 'failed', 'ignored'), [sum(int(row[i]) for row in counts) for i in range(3)]))
data['test_totals']['result_lines'] = len(counts)
save()
run('build-package', ['pwsh', '-NoProfile', '-File', './build.ps1'])
package = CLONE / 'package'
manifest = json.loads((package / 'manifest.json').read_text(encoding='utf-8-sig'))
assert isinstance(manifest, list)
checked = []
for item in manifest:
    path = package / item['path']
    assert path.is_file(), item['path']
    assert path.stat().st_size == item['bytes'], item['path']
    assert sha(path) == item['sha256'], item['path']
    checked.append(item)
actual = {p.relative_to(package).as_posix() for p in package.rglob('*') if p.is_file() and p.name != 'manifest.json'}
assert actual == {i['path'] for i in manifest}, actual.symmetric_difference({i['path'] for i in manifest})
data['manifest_check'] = {'entries': len(checked), 'all_sizes_match': True, 'all_sha256_match': True, 'package_file_set_exact_before_harness': True}
(OUT / 'manifest-verification.json').write_text(json.dumps({'verified': checked}, indent=2) + '\n', encoding='utf-8')
data['package_source'] = json.loads((package / 'source.json').read_text(encoding='utf-8-sig'))
assert data['package_source']['commit'] == EXPECTED and not data['package_source']['dirty']
data['new_dependency_license_copies'] = []
for dependency in ['arc-swap', 'rustversion']:
    rel = 'third_party/' + dependency + '/LICENSE-MIT'
    packaged = package / rel
    assert packaged.is_file(), rel
    assert sha(packaged) == sha(CLONE / rel), rel
    assert rel in actual, rel
    data['new_dependency_license_copies'].append({'path': rel, 'bytes': packaged.stat().st_size, 'sha256': sha(packaged), 'matches_committed_source': True})
data['dlls'] = []
for rel in ['EROverlay.dll', 'overlays/Minimap.dll', 'overlays/Boss.dll']:
    path = package / rel
    names = pe_exports(path)
    assert names
    assert not any(re.search(r'test|fixture', name, re.I) for name in names), names
    data['dlls'].append({'path': rel, 'bytes': path.stat().st_size, 'sha256': sha(path), 'exports': names, 'test_or_fixture_export_present': False})
save()
run('default-dll-harness-three-cycles', [str(CLONE / 'target/release/eroverlay-harness.exe'), str(package), '3'])
data['harness'] = {'cycles': 3, 'fixture_dlls_added': False, 'game_assets_supplied': False, 'scope': 'Default production EROverlay + Minimap + Boss DLL loader/lifecycle/borrowed WARP render path. Offline game state stays unavailable, so this does not replace visible overlap or live-game acceptance.'}
data['source_clean_after'] = not git('status', '--porcelain', '--untracked-files=normal')
assert data['source_clean_after']
data['manifest_check']['all_original_items_unchanged_after_harness'] = all((package / i['path']).stat().st_size == i['bytes'] and sha(package / i['path']) == i['sha256'] for i in manifest)
assert data['manifest_check']['all_original_items_unchanged_after_harness']
data['harness_created_extra_package_files'] = sorted({p.relative_to(package).as_posix() for p in package.rglob('*') if p.is_file()} - actual - {'manifest.json'})
data['finished_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
data['success'] = True
data['limits'] = 'Independent clean-source rebuild; its paths/tool outputs may change DLL bytes. These hashes do not replace parent-task final packaged DLL identity. No game installation, injection, external assets, fixture features, visible continuity matrix or microbenchmark were run.'
save()
print('SUCCESS: ' + json.dumps({'test_totals': data['test_totals'], 'manifest': data['manifest_check'], 'source_clean_after': data['source_clean_after']}), flush=True)
