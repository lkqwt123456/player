param([string]$BuildDir = 'build\Release', [string]$OutputDir = 'dist\FluxPlayer')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sourcePath = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDir))
$outputPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDir))
if (-not $outputPath.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output must remain inside this project.'
}
if (-not (Test-Path -LiteralPath (Join-Path $sourcePath 'FluxPlayer.exe'))) { throw 'Build and deploy the app first.' }
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
Get-ChildItem -LiteralPath $sourcePath -Filter '*.dll' | Copy-Item -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $sourcePath 'FluxPlayer.exe') -Destination $outputPath
foreach ($folder in @('platforms', 'styles', 'imageformats', 'multimedia', 'networkinformation', 'tls', 'iconengines', 'generic', 'translations')) {
    $pluginPath = Join-Path $sourcePath $folder
    if (Test-Path -LiteralPath $pluginPath) { Copy-Item -LiteralPath $pluginPath -Destination $outputPath -Recurse -Force }
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY.md') -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs') -Destination $outputPath -Recurse -Force
Write-Host "Portable package: $outputPath"
