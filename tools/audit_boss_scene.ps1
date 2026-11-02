param(
    [Parameter(Mandatory)][string]$Assembly,
    [Parameter(Mandatory)][string]$Scene,
    [Parameter(Mandatory)][uint32[]]$Entity,
    [string]$Model = '',
    [Parameter(Mandatory)][string]$Output
)
$ErrorActionPreference = 'Stop'
# Explicit offline input: use a decompressed MSB and an existing SoulsFormats DLL.
# Neither the reader nor game assets are dependencies of the normal build.
$readerPath = (Resolve-Path -LiteralPath $Assembly).Path
$scenePath = (Resolve-Path -LiteralPath $Scene).Path
[System.Reflection.Assembly]::LoadFrom($readerPath) | Out-Null
$map = [SoulsFormats.MSBE]::Read($scenePath)
$selected = @()
foreach ($kind in @('Enemies', 'DummyEnemies')) {
    foreach ($part in $map.Parts.$kind) {
        if ($Entity -contains $part.EntityID) {
            $selected += [ordered]@{
                kind = $kind; entity = $part.EntityID; name = $part.Name
                model = $part.ModelName; npc = $part.NPCParamID
                char_init = $part.CharaInitID; talk = $part.TalkID
                game_edition_disable = $part.GameEditionDisable
            }
        }
    }
}
$parts = @($map.Parts.Enemies) + @($map.Parts.DummyEnemies)
$result = [ordered]@{
    schema = 1
    reader_sha256 = (Get-FileHash -LiteralPath $readerPath -Algorithm SHA256).Hash.ToLowerInvariant()
    scene = [System.IO.Path]::GetFileName($scenePath)
    scene_sha256 = (Get-FileHash -LiteralPath $scenePath -Algorithm SHA256).Hash.ToLowerInvariant()
    enemies = $map.Parts.Enemies.Count
    dummy_enemies = $map.Parts.DummyEnemies.Count
    requested_entities = @($Entity)
    selected_entities = @($selected)
    model_query = $Model
    model_matches = @($parts | Where-Object { $Model -ne '' -and $_.ModelName -eq $Model }).Count
    limit = 'Scene membership only; does not prove quest reachability or observed gameplay.'
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Output -Encoding utf8
Write-Output "Scene evidence written to $Output"
