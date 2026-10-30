$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$download = Join-Path $PSScriptRoot '.tools\w64devkit.7z.exe'
$compiler = Join-Path $PSScriptRoot '.tools\w64devkit\bin\g++.exe'
if (Test-Path $compiler) {
    & $compiler --version
    exit $LASTEXITCODE
}
New-Item -ItemType Directory -Force '.tools' | Out-Null
Invoke-WebRequest 'https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x64-2.11.0.7z.exe' -OutFile $download
$expected = '54c2bb9962e8a30c8270e34c5efc3d5fee579f53087fa7d435b95fbba2af7b2b'
if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $expected) { throw '编译工具 SHA256 校验失败。' }
$extractor = Start-Process -FilePath $download -ArgumentList '-y', '-o.tools' -WindowStyle Hidden -Wait -PassThru
if ($extractor.ExitCode -ne 0) { throw '编译工具解压失败。' }
if (!(Test-Path $compiler)) { throw '未找到安装后的 C++ 编译器。' }
& $compiler --version
