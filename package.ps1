$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$releaseDirectory = Join-Path $PSScriptRoot 'dist\GlimmerTodo-v1.10.0-windows-x64'
$licenseDirectory = Join-Path $releaseDirectory 'licenses'
New-Item -ItemType Directory -Force $releaseDirectory,$licenseDirectory | Out-Null
Copy-Item -LiteralPath 'build\GlimmerTodo.exe' -Destination $releaseDirectory
Copy-Item -LiteralPath 'build\GlimmerTodo.exe' -Destination 'dist\GlimmerTodo.exe'
Copy-Item -LiteralPath 'README.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md' -Destination $releaseDirectory
Copy-Item -LiteralPath 'vendor\LICENSE.json','vendor\LICENSE.MinGW-w64.txt','vendor\COPYING.GPL3','vendor\COPYING.RUNTIME' -Destination $licenseDirectory
Copy-Item -LiteralPath 'vendor\imgui\LICENSE.txt' -Destination (Join-Path $licenseDirectory 'LICENSE.imgui.txt')
Copy-Item -LiteralPath 'vendor\filedialog\LICENSE' -Destination (Join-Path $licenseDirectory 'LICENSE.ImGuiFileDialog.txt')
Compress-Archive -LiteralPath $releaseDirectory -DestinationPath 'dist\GlimmerTodo-v1.10.0-windows-x64.zip' -Force
Get-Item 'dist\GlimmerTodo-v1.10.0-windows-x64.zip' | Select-Object FullName,Length
