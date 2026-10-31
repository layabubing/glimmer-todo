param([switch]$Test)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$compilerDirectory = Join-Path $PSScriptRoot '.tools\w64devkit\bin'
if (!(Test-Path (Join-Path $compilerDirectory 'g++.exe'))) { throw '缺少 C++ 编译器，请按照 README 安装 w64devkit。' }
$env:PATH = "$compilerDirectory;$env:PATH"
New-Item -ItemType Directory -Force build,dist | Out-Null
& windres '-c' '65001' 'app.rc' '-O' 'coff' '-o' 'build\app.res'
if ($LASTEXITCODE -ne 0) { throw '资源编译失败' }
$options = @('-std=c++20','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-Wno-missing-field-initializers','-DWINVER=0x0A00','-D_WIN32_WINNT=0x0A00','-DNOMINMAX','-DUNICODE','-D_UNICODE','-static','-static-libgcc','-static-libstdc++','-s')
& g++ @options '-mwindows' '-municode' 'src\main.cpp' 'src\model.cpp' 'src\virtual_desktop.cpp' 'build\app.res' '-o' 'dist\GlimmerTodo.exe' '-lcomctl32' '-lcomdlg32' '-lshell32' '-lole32' '-ldwmapi' '-luuid' '-ladvapi32' '-Wl,--dynamicbase,--nxcompat'
if ($LASTEXITCODE -ne 0) { throw '应用编译失败' }
if ($Test) {
    & g++ @options 'tests\model_tests.cpp' 'src\model.cpp' '-o' 'build\model_tests.exe' '-luser32'
    if ($LASTEXITCODE -ne 0) { throw '测试编译失败' }
    & '.\build\model_tests.exe'
    if ($LASTEXITCODE -ne 0) { throw '测试失败' }
    & g++ @options 'tests\window_probe.cpp' '-o' 'build\window_probe.exe' '-lpsapi' '-luser32'
    if ($LASTEXITCODE -ne 0) { throw '窗口诊断工具编译失败' }
}
Get-Item 'dist\GlimmerTodo.exe' | Select-Object FullName,Length
