param([switch]$Test)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$compilerDirectory = Join-Path $PSScriptRoot '.tools\w64devkit\bin'
if (!(Test-Path (Join-Path $compilerDirectory 'g++.exe'))) { throw '缺少 C++ 编译器，请按照 README 安装 w64devkit。' }
$env:PATH = "$compilerDirectory;$env:PATH"
New-Item -ItemType Directory -Force build,dist | Out-Null
& windres '-c' '65001' 'app.rc' '-O' 'coff' '-o' 'build\app.res'
if ($LASTEXITCODE -ne 0) { throw '资源编译失败' }
$options = @('-std=c++20','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-Wno-missing-field-initializers','-DWINVER=0x0A00','-D_WIN32_WINNT=0x0A00','-DNOMINMAX','-DUNICODE','-D_UNICODE','-DIMGUI_USE_WCHAR32','-finput-charset=UTF-8','-fexec-charset=UTF-8','-Ivendor\imgui','-Isrc','-include','src\filedialog_config.hpp','-static','-static-libgcc','-static-libstdc++','-s')
$vendorDirectory = 'build\ui-1.92.3-0.6.8-wchar32'
New-Item -ItemType Directory -Force $vendorDirectory | Out-Null
$vendorSources = @('vendor\imgui\imgui.cpp','vendor\imgui\imgui_draw.cpp','vendor\imgui\imgui_tables.cpp','vendor\imgui\imgui_widgets.cpp','vendor\imgui\backends\imgui_impl_win32.cpp','vendor\imgui\backends\imgui_impl_dx11.cpp','vendor\imgui\misc\cpp\imgui_stdlib.cpp','vendor\filedialog\ImGuiFileDialog.cpp')
$headerTime = @((Get-ChildItem 'vendor\imgui','vendor\filedialog' -Recurse -Filter '*.h'),(Get-Item 'src\filedialog_config.hpp')) | ForEach-Object { $_.LastWriteTimeUtc } | Sort-Object -Descending | Select-Object -First 1
$vendorObjects = foreach ($source in $vendorSources) {
    $object = Join-Path $vendorDirectory (([IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
    if (!(Test-Path -LiteralPath $object) -or (Get-Item $source).LastWriteTimeUtc -gt (Get-Item $object).LastWriteTimeUtc -or $headerTime -gt (Get-Item $object).LastWriteTimeUtc) {
        & g++ @options '-w' '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) { throw "界面依赖编译失败：$source" }
    }
    $object
}
& ar rcs 'build\ui-vendor.a' @vendorObjects
if ($LASTEXITCODE -ne 0) { throw '界面依赖归档失败' }
$common = @('src\model.cpp','src\error.cpp','src\virtual_desktop.cpp','src\updater.cpp','src\startup.cpp','src\taskbar_panel.cpp','src\chrome.cpp','src\ui.cpp','build\ui-vendor.a','build\app.res')
$libraries = @('-lshell32','-lole32','-ldwmapi','-luuid','-ladvapi32','-lwinhttp','-lbcrypt','-lshlwapi','-lgdi32','-ld3d11','-ldxgi','-ld3dcompiler','-limm32')
& g++ @options '-mwindows' '-municode' 'src\app.cpp' @common '-o' 'build\GlimmerTodo.exe' @libraries '-Wl,--dynamicbase,--nxcompat'
if ($LASTEXITCODE -ne 0) { throw '应用编译失败' }
if ($Test) {
    & g++ @options 'tests\model_tests.cpp' 'src\model.cpp' 'src\error.cpp' '-o' 'build\model_tests.exe' '-luser32' '-lgdi32'
    if ($LASTEXITCODE -ne 0) { throw '测试编译失败' }
    & '.\build\model_tests.exe'
    if ($LASTEXITCODE -ne 0) { throw '测试失败' }
    & g++ @options 'tests\ui_tests.cpp' @common '-o' 'build\ui_tests.exe' @libraries '-lgdiplus'
    if ($LASTEXITCODE -ne 0) { throw '窗口模式检查编译失败' }
    & '.\build\ui_tests.exe'
    if ($LASTEXITCODE -ne 0) { throw '窗口模式检查失败' }
    & g++ @options 'tests\taskbar_panel.cpp' 'src\taskbar_panel.cpp' 'src\model.cpp' 'src\error.cpp' 'src\chrome.cpp' 'src\virtual_desktop.cpp' 'build\app.res' '-o' 'build\taskbar_panel_tests.exe' '-lgdi32' '-luser32' '-ldwmapi' '-lole32' '-luuid' '-ladvapi32'
    if ($LASTEXITCODE -ne 0) { throw '任务栏信息检查编译失败' }
    & '.\build\taskbar_panel_tests.exe'
    if ($LASTEXITCODE -ne 0) { throw '任务栏信息检查失败' }
    & g++ @options 'tests\window_probe.cpp' '-o' 'build\window_probe.exe' '-lpsapi' '-luser32'
    if ($LASTEXITCODE -ne 0) { throw '窗口诊断工具编译失败' }
}
Get-Item 'build\GlimmerTodo.exe' | Select-Object FullName,Length
