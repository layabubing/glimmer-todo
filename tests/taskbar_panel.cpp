#include "../src/taskbar_panel.hpp"
#include "../src/theme.hpp"
#include "../src/virtual_desktop.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <iostream>

namespace {
int checks = 0, expanded = 0, menuOpened = 0;

void check(bool condition,const char* description) {
    if(!condition){std::cerr<<"FAILED: "<<description<<'\n';ExitProcess(1);}
    ++checks;
}

LRESULT CALLBACK ownerProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_APP+3){++expanded;return 0;}
    if(message==WM_APP+4){++menuOpened;return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}

void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
}
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto slot=taskbarSlot();
    if(!slot.host){std::cout<<"No available Explorer taskbar area on this desktop.\n";return 0;}
    initializeVirtualDesktops();
    HINSTANCE instance=GetModuleHandleW(nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};check(InitCommonControlsEx(&controls),"initialize native controls");
    WNDCLASSEXW klass{sizeof(klass)};klass.hInstance=instance;klass.lpfnWndProc=ownerProc;klass.lpszClassName=L"GlimmerTaskbarTestOwner";
    check(RegisterClassExW(&klass),"register hidden owner");
    HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW,klass.lpszClassName,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
    check(owner!=nullptr,"create hidden owner");
    State state;state.addTask(L"安排日程");state.putSchedule(0,L"专注时间",nowSeconds()+3600,0);state.pomodoro.toggle(nowSeconds());
    initializeTaskbarPanel(instance,owner,state,WM_APP+3,WM_APP+4);
    auto previous=GetThreadDpiAwarenessContext();syncTaskbarPanel(true);pump();HWND panel=taskbarPanelWindow();
    check(panel && IsWindowVisible(panel),"show actual taskbar information panel");
    check(GetWindow(panel,GW_OWNER)==slot.host && (GetWindowLongPtrW(panel,GWL_EXSTYLE)&WS_EX_TOOLWINDOW),"panel belongs to the real Explorer taskbar");
    DWORD shellPid=0;GetWindowThreadProcessId(GetWindow(panel,GW_OWNER),&shellPid);
    check(shellPid!=GetCurrentProcessId(),"panel is owned by Explorer");
    check(AreDpiAwarenessContextsEqual(previous,GetThreadDpiAwarenessContext()),"preserve application DPI context");
    check(GetDpiForWindow(panel)==slot.dpi,"match taskbar DPI");
    check(followCurrentDesktop({panel}),"taskbar panel supports virtual desktop following");
    RECT actual{},expected=slot.bounds;GetWindowRect(panel,&actual);MapWindowPoints(slot.host,nullptr,reinterpret_cast<POINT*>(&expected),2);
    check(EqualRect(&actual,&expected),"panel occupies left taskbar slot");
    check((GetWindowLongPtrW(panel,GWL_EXSTYLE)&WS_EX_NOACTIVATE) && SendMessageW(panel,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,"preserve foreground focus");
    wchar_t title[1024]{};GetWindowTextW(panel,title,1024);
    check(std::wstring(title).find(L"待办 0/1")!=std::wstring::npos && std::wstring(title).find(L"专注时间")!=std::wstring::npos,"panel exposes live information");
    for(int theme=0;theme<3;++theme) {
        state.theme=theme;syncTaskbarPanel(true);RedrawWindow(panel,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);check(IsWindowVisible(panel),"redraw taskbar theme");
        check(SUCCEEDED(DwmFlush()),"wait for screen composition");
        HDC screen=GetDC(nullptr);auto displayed=GetPixel(screen,actual.left+MulDiv(6,slot.dpi,96),actual.top+(actual.bottom-actual.top)/2);ReleaseDC(nullptr,screen);
        if(displayed!=themePalette(theme).background)std::cerr<<"Screen pixel="<<displayed<<" expected="<<themePalette(theme).background<<'\n';
        check(displayed==themePalette(theme).background,"taskbar information reaches actual screen pixels");
    }
    SendMessageW(panel,WM_LBUTTONUP,0,0);pump();check(expanded==1,"click requests full window");
    SendMessageW(panel,WM_CONTEXTMENU,0,0);pump();check(menuOpened==1,"right click requests menu");
    syncTaskbarPanel(false);check(!IsWindowVisible(panel),"hide taskbar information");
    syncTaskbarPanel(true);check(IsWindowVisible(panel),"restore taskbar information");
    destroyTaskbarPanel();check(!IsWindow(panel),"remove panel without changing Explorer");
    syncTaskbarPanel(true);check(taskbarPanelWindow() && GetWindow(taskbarPanelWindow(),GW_OWNER)==slot.host,"recreate taskbar information");
    destroyTaskbarPanel();DestroyWindow(owner);shutdownVirtualDesktops();
    std::cout<<checks<<" real taskbar checks passed\n";
    return 0;
}
