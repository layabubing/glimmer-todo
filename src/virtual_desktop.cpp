#include "virtual_desktop.hpp"
#include "model.hpp"
#include <shobjidl.h>

namespace {
IVirtualDesktopManager* manager = nullptr;
std::wstring desktopRegistryPath;

void checkResult(HRESULT result, const wchar_t* operation) {
    if (FAILED(result)) {
        fatal(std::wstring(operation) + L"\nHRESULT：" + std::to_wstring(static_cast<unsigned long>(result)));
    }
}

GUID currentDesktopId() {
    GUID desktop{};
    DWORD size = sizeof(desktop);
    const LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, desktopRegistryPath.c_str(),
        L"CurrentVirtualDesktop", RRF_RT_REG_BINARY, nullptr, &desktop, &size);
    if (result != ERROR_SUCCESS) {
        SetLastError(result);
        fatal(L"无法读取当前虚拟桌面，Windows 错误代码：" + std::to_wstring(result));
    }
    require(size == sizeof(desktop) && !IsEqualGUID(desktop, GUID_NULL), L"当前虚拟桌面标识无效。");
    return desktop;
}
}

void initializeVirtualDesktops() {
    checkResult(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), L"无法初始化 Windows 桌面接口。");
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    version.dwBuildNumber = 22000;
    const bool windows11 = VerifyVersionInfoW(&version, VER_BUILDNUMBER,
        VerSetConditionMask(0, VER_BUILDNUMBER, VER_GREATER_EQUAL)) != FALSE;
    if (windows11) {
        desktopRegistryPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";
    } else {
        DWORD session = 0;
        require(ProcessIdToSessionId(GetCurrentProcessId(), &session) != FALSE, L"无法读取 Windows 会话编号。");
        desktopRegistryPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\" +
            std::to_wstring(session) + L"\\VirtualDesktops";
    }
    reconnectVirtualDesktops();
}

void reconnectVirtualDesktops() {
    if (manager) manager->Release();
    manager = nullptr;
    checkResult(CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&manager)), L"无法连接 Windows 虚拟桌面服务。");
}

void shutdownVirtualDesktops() {
    if (manager) manager->Release();
    manager = nullptr;
    CoUninitialize();
}

bool isOnCurrentDesktop(HWND window) {
    BOOL current = FALSE;
    checkResult(manager->IsWindowOnCurrentVirtualDesktop(window, &current), L"无法查询窗口所在的虚拟桌面。");
    return current != FALSE;
}

bool followCurrentDesktop(std::initializer_list<HWND> windows) {
    GUID target{};
    bool targetRead = false;
    for (HWND window : windows) {
        if (!window || isOnCurrentDesktop(window)) continue;
        if (!targetRead) {
            target = currentDesktopId();
            targetRead = true;
        }
        GUID previous{};
        checkResult(manager->GetWindowDesktopId(window, &previous), L"无法读取待办窗口的虚拟桌面标识。");
        // 桌面切换动画与注册表更新可能分属相邻的消息周期。
        if (IsEqualGUID(previous, target)) return false;
        checkResult(manager->MoveWindowToDesktop(window, target), L"无法将待办窗口移动到当前虚拟桌面。");
        if (!isOnCurrentDesktop(window)) return false;
    }
    return true;
}
