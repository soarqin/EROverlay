$ErrorActionPreference='Stop'
$root='<worktree>'
Set-Location -LiteralPath $root
python target/boss-presentation-final/copy_perf.py
cargo build -p eroverlay-renderer --release --locked --message-format=json 2>target/boss-presentation-final/copy-build.stderr.log |
    Set-Content -LiteralPath target/boss-presentation-final/copy-build.jsonl -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'Renderer Release build failed' }
$artifact=Get-Content -LiteralPath target/boss-presentation-final/copy-build.jsonl | ForEach-Object { $_ | ConvertFrom-Json } |
    Where-Object { $_.reason -eq 'compiler-artifact' -and $_.target.name -eq 'eroverlay_renderer' } | Select-Object -Last 1
$renderer=@($artifact.filenames | Where-Object { $_.EndsWith('.rlib') })[0]
$deps=Join-Path $root 'target/release/deps'
$abi=Get-ChildItem -LiteralPath $deps -Filter 'liberoverlay_abi-*.rlib' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$windows=Get-ChildItem -LiteralPath $deps -Filter 'libwindows-*.rlib' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$egui=Get-ChildItem -LiteralPath $deps -Filter 'libegui-*.rlib' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
rustc target/boss-presentation-final/copy_perf.rs --edition 2024 -O -C lto=thin -C codegen-units=1 -L "dependency=$deps" --extern "eroverlay_renderer=$renderer" --extern "eroverlay_abi=$($abi.FullName)" --extern "windows=$($windows.FullName)" --extern "egui=$($egui.FullName)" -o target/boss-presentation-final/copy_perf.exe
if ($LASTEXITCODE -ne 0) { throw 'Standalone perf compile failed' }
foreach ($round in 1..3) {
    $modes=if ($round -eq 2) { @('retained','baseline') } else { @('baseline','retained') }
    foreach ($mode in $modes) {
        $perfArgs=@('--samples','300','--hdr')
        if ($mode -eq 'retained') { $perfArgs+='--retain' }
        & ./target/boss-presentation-final/copy_perf.exe @perfArgs | Tee-Object -FilePath "target/boss-presentation-final/copy-$mode-$round.jsonl"
        if ($LASTEXITCODE -ne 0) { throw "Perf failed: $mode round $round" }
    }
}
