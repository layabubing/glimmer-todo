#include "../src/main.cpp"
#include <iostream>

namespace {
int checks = 0;

void check(bool condition, const char* description) {
    if(!condition){std::cerr << "FAILED: " << description << '\n';ExitProcess(1);}
    ++checks;
}

void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
}
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto desktopName=L"GlimmerModes."+std::to_wstring(GetCurrentProcessId());
    HDESK original=GetThreadDesktop(GetCurrentThreadId());
    HDESK desktop=CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    check(desktop && SetThreadDesktop(desktop),"create isolated desktop");
    initializeVirtualDesktops();instance=GetModuleHandleW(nullptr);dpi=static_cast<int>(GetDpiForSystem());
    dataFile=std::filesystem::current_path()/L".work"/L"window-tests"/L"state.json";
    state=State{};state.automatic=false;
    colors=themePalette(0);fonts();paperBrush=CreateSolidBrush(Paper);whiteBrush=CreateSolidBrush(Surface);appIcon=createIcon();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_DATE_CLASSES};
    check(InitCommonControlsEx(&controls),"initialize real native controls");
    WNDCLASSEXW klass{sizeof(klass)};klass.hInstance=instance;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    klass.lpfnWndProc=mainProc;klass.lpszClassName=L"FloatingTodoMain";check(RegisterClassExW(&klass),"register main window");
    klass.lpfnWndProc=settingsProc;klass.lpszClassName=L"FloatingTodoSettings";check(RegisterClassExW(&klass),"register settings");
    klass.lpfnWndProc=scheduleProc;klass.lpszClassName=L"FloatingTodoSchedule";check(RegisterClassExW(&klass),"register schedule editor");
    klass.lpfnWndProc=compactProc;klass.lpszClassName=L"FloatingTodoCompact";klass.style=CS_DBLCLKS;check(RegisterClassExW(&klass),"register compact window");
    check(CreateWindowExW(WS_EX_APPWINDOW,L"FloatingTodoMain",L"浮光待办",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN,40,40,px(260),px(320),nullptr,nullptr,instance,nullptr),"create main window");
    check(CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"FloatingTodoCompact",L"浮光待办",WS_POPUP|WS_CLIPCHILDREN,40,40,cp(304),cp(66),nullptr,nullptr,instance,nullptr),"create compact window");
    ready=true;applyVisibility();pump();RECT normal{};GetWindowRect(mainWindow,&normal);
    check(IsWindowVisible(mainWindow) && !IsIconic(mainWindow) && !IsWindowVisible(compactWindow),"expanded visibility");
    state.addTask(L"完成日程安排");state.putSchedule(0,L"专注时间",nowSeconds()+3600,0);state.pomodoro.toggle(nowSeconds());
    for(int mode=0;mode<4;++mode) {
        command(CollapseFirstId+mode);pump();
        bool floating=mode<2;
        check(manuallyHidden && IsWindowVisible(compactWindow)==floating,"selected compact visibility");
        check(IsWindowVisible(mainWindow)==(mode==3),"taskbar is the only visible main window while collapsed");
        if(mode==3)check(IsIconic(mainWindow) && textOf(mainWindow).find(L"待办 0/1")!=std::wstring::npos && textOf(mainWindow).find(L"专注时间")!=std::wstring::npos,"taskbar minimized state and information");
        if(floating) {
            check((GetWindowLongPtrW(compactWindow,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)!=0 && GetWindow(compactWindow,GW_OWNER)==nullptr,"independent compact tool window");
            RECT rect{};GetWindowRect(compactWindow,&rect);MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(compactWindow,MONITOR_DEFAULTTONEAREST),&info);
            check(rect.left>=info.rcWork.left && rect.right<=info.rcWork.right && rect.top>=info.rcWork.top && rect.bottom<=info.rcWork.bottom,"compact bounds inside work area");
            if(mode==1)check(rect.top==info.rcWork.top+cp(6) && rect.right-rect.left>cp(304),"top bar position and size");
            check(textOf(compactWindow).find(L"待办 0/1")!=std::wstring::npos && textOf(compactWindow).find(L"专注时间")!=std::wstring::npos,"compact accessible information");
        }
        automaticallyHidden=true;applyVisibility();pump();
        check(!IsWindowVisible(mainWindow) && !IsWindowVisible(compactWindow),"automatic hiding overrides every collapse mode");
        automaticallyHidden=false;applyVisibility();pump();
        check(manuallyHidden && IsWindowVisible(compactWindow)==floating && IsWindowVisible(mainWindow)==(mode==3),"restore selected mode after automatic hiding");
        SendMessageW(mainWindow,ShowRequest,0,0);pump();
        check(!manuallyHidden && IsWindowVisible(mainWindow) && !IsIconic(mainWindow) && !IsWindowVisible(compactWindow),"expand every mode");
        RECT restored{};GetWindowRect(mainWindow,&restored);
        check(EqualRect(&normal,&restored),"expanded bounds survive mode changes");
    }
    command(CollapseFirstId+3);SendMessageW(mainWindow,WM_SYSCOMMAND,SC_RESTORE,0);pump();
    check(!manuallyHidden && !IsIconic(mainWindow),"taskbar click restore");
    SendMessageW(mainWindow,WM_SYSCOMMAND,SC_MINIMIZE,0);pump();
    check(manuallyHidden && IsIconic(mainWindow),"system minimize uses selected mode");
    openSettings();pump();
    check(!manuallyHidden && IsWindowVisible(settingsWindow) && SendMessageW(collapseChoice,CB_GETCOUNT,0,0)==4,"settings restores expanded window and lists four modes");
    DestroyWindow(settingsWindow);
    state.collapseMode=CollapseMode::Capsule;state.capsuleX=INT_MIN/2;state.capsuleY=INT_MIN/2;manuallyHidden=true;applyVisibility();pump();
    RECT clamped{};GetWindowRect(compactWindow,&clamped);
    check(clamped.left!=INT_MIN/2 && clamped.top!=INT_MIN/2,"capsule recovers after monitor removal");
    for(int theme=0;theme<3;++theme){state.theme=theme;applyTheme();RedrawWindow(compactWindow,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);check(IsWindowVisible(compactWindow),"theme redraw preserves compact mode");}
    check(followCurrentDesktop({mainWindow,compactWindow}),"compact and main windows participate in virtual desktop following");
    DestroyWindow(mainWindow);pump();
    for(auto font:{bodyFont,smallFont,rowFont,doneFont,clockFont,titleFont})DeleteObject(font);
    DeleteObject(paperBrush);DeleteObject(whiteBrush);DestroyIcon(appIcon);shutdownVirtualDesktops();
    check(SetThreadDesktop(original) && CloseDesktop(desktop),"close isolated desktop");
    std::cout << checks << " native window checks passed\n";
    return 0;
}
