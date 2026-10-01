param([string]$Python = 'python', [string]$Firmware = '', [switch]$OneFile)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (!$Firmware) { $Firmware = Join-Path $taskRoot 'build/adapter.uf2' }
if (!(Test-Path -LiteralPath $Firmware)) { throw 'Build the Pico firmware before packaging G27 Studio.' }
$desktopRoot = Join-Path $taskRoot 'desktop'
$firmwareRoot = Join-Path $desktopRoot 'firmware'
New-Item -ItemType Directory -Force -Path $firmwareRoot | Out-Null
Copy-Item -LiteralPath $Firmware -Destination (Join-Path $firmwareRoot 'adapter.uf2')
& $Python (Join-Path $desktopRoot 'collect_notices.py')
if ($LASTEXITCODE -ne 0) { throw 'Collecting dependency notices failed.' }
$arguments = @('-m', 'PyInstaller', '--noconfirm', '--clean', '--windowed', '--name', 'G27-Studio',
    '--distpath', (Join-Path $desktopRoot 'dist'), '--workpath', (Join-Path $desktopRoot 'build'),
    '--specpath', (Join-Path $desktopRoot 'build'), '--paths', $desktopRoot,
    '--add-data', ((Join-Path $desktopRoot 'ui') + ';ui'),
    '--add-data', ($firmwareRoot + ';firmware'),
    '--add-data', ((Join-Path $desktopRoot 'licenses') + ';licenses'),
    '--collect-all', 'webview', '--hidden-import', 'hid')
if ($OneFile) { $arguments += '--onefile' }
$arguments += (Join-Path $desktopRoot 'main.py')
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw 'G27 Studio packaging failed.' }
