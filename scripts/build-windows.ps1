param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$FFmpegRoot = $env:FFMPEG_ROOT,
    [string]$BuildDir = 'build',
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $QtRoot -or -not $FFmpegRoot) {
    throw 'Specify -QtRoot and -FFmpegRoot (or QT_ROOT / FFMPEG_ROOT). Use a Qt MSVC x64 kit and FFmpeg shared SDK.'
}
$qtPath = (Resolve-Path -LiteralPath $QtRoot).Path
$ffmpegPath = (Resolve-Path -LiteralPath $FFmpegRoot).Path
$buildPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDir))
$architecture = @()
if ($Generator -like 'Visual Studio*') { $architecture = @('-A', 'x64') }
& $CMake -S $projectRoot -B $buildPath -G $Generator @architecture -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qtPath" "-DFFmpeg_ROOT=$ffmpegPath"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $CMake --build $buildPath --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
$outputPath = Join-Path $buildPath 'Release'
if (-not (Test-Path -LiteralPath $outputPath)) { $outputPath = $buildPath }
Get-ChildItem -LiteralPath (Join-Path $ffmpegPath 'bin') -Filter '*.dll' | Copy-Item -Destination $outputPath
& (Join-Path $qtPath 'bin\windeployqt.exe') --release --no-translations (Join-Path $outputPath 'FluxPlayer.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
$ctest = Join-Path (Split-Path -Parent (Get-Command $CMake).Source) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) { $ctest = 'ctest' }
& $ctest --test-dir $buildPath -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Queue tests failed' }
Write-Host "Ready: $outputPath\FluxPlayer.exe"
Write-Host "Integration: python scripts/integration.py --build $BuildDir/Release --network"
