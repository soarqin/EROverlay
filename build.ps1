param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$rustRoot = $PSScriptRoot
Push-Location -LiteralPath $rustRoot
try {
    if (-not $SkipBuild) {
        cargo build --workspace --release --locked
        if ($LASTEXITCODE -ne 0) { throw 'Rust Release build failed.' }
    }
    $packageRoot = Join-Path $rustRoot 'package'
    $overlayRoot = Join-Path $packageRoot 'overlays'
    $configRoot = Join-Path $packageRoot 'configs'
    New-Item -ItemType Directory -Path $overlayRoot, $configRoot -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $rustRoot 'target/release/EROverlay.dll') -Destination $packageRoot
    Copy-Item -LiteralPath (Join-Path $rustRoot 'target/release/Minimap.dll') -Destination $overlayRoot
    Copy-Item -LiteralPath (Join-Path $rustRoot 'target/release/Boss.dll') -Destination $overlayRoot
    foreach ($name in @('common', 'input', 'style', 'minimap', 'boss')) {
        Copy-Item -LiteralPath (Join-Path $rustRoot "configs/$name.ini") -Destination $configRoot
    }
    Copy-Item -LiteralPath (Join-Path $rustRoot 'configs/boss-revival.json') -Destination $configRoot
    Copy-Item -LiteralPath (Join-Path $rustRoot 'configs/boss-encounters.json') -Destination $configRoot
    Copy-Item -LiteralPath (Join-Path $rustRoot 'README.md') -Destination $packageRoot
    Copy-Item -LiteralPath (Join-Path $rustRoot 'LICENSE') -Destination $packageRoot
    Copy-Item -LiteralPath (Join-Path $rustRoot 'THIRD_PARTY.md') -Destination $packageRoot
    $noticeRoot = Join-Path $packageRoot 'third_party'
    $minhookNotices = Join-Path $noticeRoot 'minhook'
    New-Item -ItemType Directory -Path $minhookNotices -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $rustRoot 'third_party/minhook/LICENSE.txt') -Destination $minhookNotices
    Copy-Item -LiteralPath (Join-Path $rustRoot 'third_party/font-licenses') -Destination $noticeRoot -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $rustRoot 'docs') -Destination $packageRoot -Recurse -Force
    foreach ($module in @('minimap', 'boss', 'renderer', 'game', 'ui')) {
        $moduleDocs = Join-Path $packageRoot "crates/$module"
        New-Item -ItemType Directory -Path $moduleDocs -Force | Out-Null
        Get-ChildItem -LiteralPath (Join-Path $rustRoot "crates/$module") -Filter '*.md' -File |
            Copy-Item -Destination $moduleDocs -Force
    }
    Copy-Item -LiteralPath (Join-Path $rustRoot 'crates/renderer/evidence') -Destination (Join-Path $packageRoot 'crates/renderer') -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $rustRoot 'crates/game/scan-evidence.csv') -Destination (Join-Path $packageRoot 'crates/game') -Force
    Copy-Item -LiteralPath (Join-Path $rustRoot 'crates/game/scan-evidence-rust.csv') -Destination (Join-Path $packageRoot 'crates/game') -Force
    Copy-Item -LiteralPath (Join-Path $rustRoot 'crates/renderer/scan-evidence.csv') -Destination (Join-Path $packageRoot 'crates/renderer') -Force
    $sourceCommit = & git -C $rustRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit.' }
    $sourceBranch = & git -C $rustRoot branch --show-current
    $sourceChanges = & git -C $rustRoot status --porcelain --untracked-files=normal
    [ordered]@{ commit = $sourceCommit; branch = $sourceBranch; dirty = [bool]$sourceChanges } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $packageRoot 'source.json') -Encoding utf8
    $files = Get-ChildItem -LiteralPath $packageRoot -File -Recurse
    $manifest = foreach ($file in $files) {
        if ($file.Name -eq 'manifest.json') { continue }
        [ordered]@{ path = $file.FullName.Substring($packageRoot.Length + 1).Replace('\', '/'); bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $packageRoot 'manifest.json') -Encoding utf8
    Write-Output "Independent Rust package: $packageRoot"
} finally {
    Pop-Location
}
