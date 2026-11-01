param(
    [ValidateRange(10, 100000)][int]$Samples = 600,
    [ValidateRange(1, 30)][int]$Runs = 3,
    [string]$OutputDirectory = '',
    [Parameter(Mandatory = $true)][string]$CppReference
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'target/comparison' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$cppFixture = Join-Path $CppReference 'build/rust-comparison/cpp_render_perf.exe'
$rustFixture = Join-Path $repoRoot 'target/release/examples/gpu_perf.exe'
foreach ($fixture in @($cppFixture, $rustFixture)) {
    if (-not (Test-Path -LiteralPath $fixture)) { throw "Build fixture first: $fixture" }
}
# Serial alternating pairs: no concurrent compiler or benchmark should run.
for ($run = 1; $run -le $Runs; ++$run) {
    & $cppFixture $Samples | Set-Content -LiteralPath (Join-Path $OutputDirectory "cpp-matched-$run.jsonl") -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "C++ fixture run $run failed: $LASTEXITCODE" }
    & $rustFixture --samples $Samples --egui-batched --hdr --no-gpu-timing |
        Set-Content -LiteralPath (Join-Path $OutputDirectory "rust-matched-$run.jsonl") -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "Rust fixture run $run failed: $LASTEXITCODE" }
}
Write-Output "Paired comparison evidence: $OutputDirectory"
