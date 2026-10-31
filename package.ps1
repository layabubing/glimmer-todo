$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$releaseDirectory = Join-Path $PSScriptRoot 'dist\GlimmerTodo-v1.1.0-windows-x64'
$licenseDirectory = Join-Path $releaseDirectory 'licenses'
New-Item -ItemType Directory -Force $releaseDirectory,$licenseDirectory | Out-Null
Copy-Item -LiteralPath 'dist\GlimmerTodo.exe' -Destination $releaseDirectory
Copy-Item -LiteralPath 'README.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md' -Destination $releaseDirectory
Copy-Item -LiteralPath 'vendor\LICENSE.json','vendor\LICENSE.MinGW-w64.txt','vendor\COPYING.GPL3','vendor\COPYING.RUNTIME' -Destination $licenseDirectory
Compress-Archive -LiteralPath $releaseDirectory -DestinationPath 'dist\GlimmerTodo-v1.1.0-windows-x64.zip' -Force
Get-Item 'dist\GlimmerTodo-v1.1.0-windows-x64.zip' | Select-Object FullName,Length
