"""Bounded renderer-only comparison; no game, UI dispatch or measured GPU wait."""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
source = root / 'target/boss-presentation-final/copy_perf.rs'
assert source.is_file()
old = root / 'target/boss-continuity-clean-source'
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=old, text=True).strip() == '0819f40da74f8fcaf916a1ba62d9b70f60e4e913'
assert not subprocess.check_output(['git', 'status', '--porcelain'], cwd=old, text=True).strip()
assert subprocess.check_output(['git', 'diff', '0819f40', '5ee822c', '--', 'crates/renderer/src', 'crates/renderer/Cargo.toml', 'Cargo.lock'], cwd=root, text=True).strip() == ''
receipts = []

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

for name, base in [('old5ee', old), ('new', root)]:
    deps = base / 'target/release/deps'
    libraries = {}
    if name == 'new':
        build = ['cargo', 'build', '-p', 'eroverlay-renderer', '--release', '--locked', '--message-format=json']
        with (out / 'copy-new-library-build.log').open('w', encoding='utf-8') as stderr:
            messages = subprocess.check_output(build, cwd=base, stderr=stderr, text=True)
        (out / 'copy-new-library-build.jsonl').write_text(messages, encoding='utf-8')
        for line in messages.splitlines():
            row = json.loads(line)
            if row.get('reason') == 'compiler-artifact':
                for filename in row.get('filenames', []):
                    if filename.endswith('.rlib'):
                        libraries[row['target']['name']] = Path(filename)
    for library in ['eroverlay_renderer', 'eroverlay_abi', 'windows', 'egui']:
        if library in libraries:
            continue
        matches = sorted(deps.glob('lib' + library + '-*.rlib'), key=lambda p: p.stat().st_mtime_ns)
        assert matches, (name, library)
        libraries[library] = matches[-1]
    exe = out / ('copy-' + name + '.exe')
    args = ['rustc', str(source), '--edition', '2024', '-O', '-C', 'lto=thin', '-C', 'codegen-units=1', '-L', 'dependency=' + str(deps)]
    libraries = {key: libraries[key] for key in ['eroverlay_renderer', 'eroverlay_abi', 'windows', 'egui']}
    for key, path in libraries.items():
        args += ['--extern', key + '=' + str(path)]
    args += ['-o', str(exe)]
    with (out / ('copy-' + name + '-build.log')).open('w', encoding='utf-8') as log:
        subprocess.run(args, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
    receipts.append({'mode': name, 'exe': str(exe), 'sha256': sha(exe), 'source_sha256': sha(source), 'command': args,
        'libraries': {key: {'path': str(path), 'sha256': sha(path)} for key, path in libraries.items()}})

results = []
for round_ in range(1, 4):
    for name in (['old5ee', 'new'] if round_ != 2 else ['new', 'old5ee']):
        command = [str(out / ('copy-' + name + '.exe')), '--samples', '300', '--hdr', '--retain']
        log = out / f'copy-{name}-{round_}.jsonl'
        with log.open('w', encoding='utf-8') as stream:
            subprocess.run(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT, check=True)
        records = [json.loads(line) for line in log.read_text(encoding='utf-8').splitlines() if line.startswith('{')]
        row = next(r for r in records if r['kind'] == 'scenario')
        assert row['dropped'] == 0
        results.append({'round': round_, 'mode': name, 'command': command, 'log': log.name, 'sha256': sha(log), 'result': row})
receipt = {'builds': receipts, 'rounds': results, 'limits': 'Same Release source and renderer-only scene; 1080p,64 quads,300 samples,three alternating rounds on actual selected GPU. Explicit queue drain outside CPU timing. Includes normal-image copy and cache bookkeeping, not game/UI/dispatcher, presenter lock contention or startup. Old renderer source is independently verified clean 0819f40, byte-identical to 5ee822c renderer.'}
(out / 'copy-comparison.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
print(json.dumps(receipt, indent=2))
