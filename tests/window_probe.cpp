#include <windows.h>
#include <psapi.h>
#include <iostream>

BOOL CALLBACK inspect(HWND window, LPARAM) {
    wchar_t name[256]{};
    GetClassNameW(window, name, 256);
    if (lstrcmpW(name, L"FloatingTodoMain") != 0 && lstrcmpW(name, L"FloatingTodoSettings") != 0) return TRUE;
    DWORD id = 0;
    GetWindowThreadProcessId(window, &id);
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, id);
    if (!process) { std::cerr << "Cannot inspect process\n"; ExitProcess(1); }
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) ExitProcess(1);
    RECT rect{}; GetWindowRect(window, &rect);
    std::cout << "{\"pid\":" << id << ",\"visible\":" << (IsWindowVisible(window) ? "true" : "false")
        << ",\"settings\":" << (lstrcmpW(name,L"FloatingTodoSettings")==0 ? "true" : "false")
        << ",\"workingSetBytes\":" << memory.WorkingSetSize << ",\"peakWorkingSetBytes\":" << memory.PeakWorkingSetSize
        << ",\"privateBytes\":" << memory.PrivateUsage << ",\"gdiObjects\":" << GetGuiResources(process,GR_GDIOBJECTS)
        << ",\"userObjects\":" << GetGuiResources(process,GR_USEROBJECTS)
        << ",\"left\":" << rect.left << ",\"top\":" << rect.top << ",\"width\":" << rect.right-rect.left << ",\"height\":" << rect.bottom-rect.top << "}\n";
    CloseHandle(process);
    return TRUE;
}

int main() {
    EnumWindows(inspect, 0);
    return 0;
}
