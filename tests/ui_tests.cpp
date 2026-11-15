#include "../src/app.cpp"
#include <gdiplus.h>
#include <iostream>

namespace {
int checks=0;
void check(bool condition,const char* description) {
    if(!condition){std::cerr<<"FAILED: "<<description<<'\n';ExitProcess(1);}
    ++checks;
}
void pump() {
    MSG event{};while(PeekMessageW(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessageW(&event);}
}
void frames(int count=2) {
    for(int index=0;index<count;++index){pump();renderWindows();Sleep(18);}
}
void noNativeControls(HWND window) {
    int count=0;EnumChildWindows(window,[](HWND child,LPARAM value)->BOOL {
        if(IsWindowVisible(child)){++*reinterpret_cast<int*>(value);}
        return TRUE;
    },reinterpret_cast<LPARAM>(&count));
    check(count==0,"no visible native child controls");
    check(GetMenu(window)==nullptr,"no native menu bar");
}
void snapshot(HWND window,const std::wstring& name) {
    RECT rect{};GetClientRect(window,&rect);HDC screen=GetDC(window),dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,rect.right,rect.bottom);auto previous=SelectObject(dc,bitmap);
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);SelectObject(dc,previous);
    UINT count=0,size=0;Gdiplus::GetImageEncodersSize(&count,&size);std::vector<BYTE> storage(size);auto encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());Gdiplus::GetImageEncoders(count,size,encoders);
    CLSID png{};for(UINT index=0;index<count;++index)if(wcscmp(encoders[index].MimeType,L"image/png")==0)png=encoders[index].Clsid;
    auto destination=std::filesystem::current_path()/L".work"/L"ui-renders"/(name+L".png");
    {Gdiplus::Bitmap output(bitmap,nullptr);check(output.Save(destination.c_str(),&png,nullptr)==Gdiplus::Ok,"capture rendered UI");}
    DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);
}
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto desktopName=L"GlimmerUI."+std::to_wstring(GetCurrentProcessId());HDESK original=GetThreadDesktop(GetCurrentThreadId());
    HDESK desktop=CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);check(desktop && SetThreadDesktop(desktop),"create isolated UI desktop");
    initializeVirtualDesktops();instance=GetModuleHandleW(nullptr);dataFile=std::filesystem::current_path()/L".work"/L"ui-tests"/L"state.json";
    std::filesystem::create_directories(std::filesystem::current_path()/L".work"/L"ui-renders");
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR token=0;check(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok,"initialize image output");
    state=State{};state.automatic=false;state.x=40;state.y=40;initializeWindows();ready=true;applyVisibility();frames();noNativeControls(mainWindow);noNativeControls(compactWindow);
    taskInput="整理今天的想法 📝";check(submitTask(),"create task through UI action");auto first=state.tasks.front().id;
    taskInput="为专注留出半个小时";check(submitTask(),"create second task");state.toggleTask(first);save();
    taskInput="确认下一个日程";check(submitTask(),"create third task for compact list");
    taskInput="完成今天的阅读";check(submitTask(),"create fourth task for compact list");
    editTask(first);taskInput="整理今天的想法与笔记 📝";check(submitTask() && state.tasks.front().text==L"整理今天的想法与笔记 📝","edit Unicode task through UI action");
    editTask(first);cancelEdit();check(!editingId && taskInput.empty(),"cancel task editing");
    auto now=nowSeconds();check(state.putSchedule(0,L"阅读与整理记录",now+3600,1),"create real schedule fixture");
    check(state.putSchedule(0,L"复盘今天的进展",now+7200,0),"create second schedule fixture");save();
    for(int theme=0;theme<3;++theme) {
        state.theme=theme;
        for(int page=0;page<3;++page){selectView(page);frames(12);snapshot(mainWindow,L"theme-"+std::to_wstring(theme)+L"-page-"+std::to_wstring(page));}
        RECT location{};GetWindowRect(mainWindow,&location);chrome::menu(mainWindow,{location.left+20,location.top+20},{{ThemeFirstId+1,L"夜色主题"},{MenuShow,L"收起窗口"}},theme);pump();noNativeControls(chrome::menuWindow());snapshot(chrome::menuWindow(),L"theme-"+std::to_wstring(theme)+L"-menu");
        if(theme==0){SendMessageW(chrome::menuWindow(),WM_KEYDOWN,VK_DOWN,0);SendMessageW(chrome::menuWindow(),WM_KEYDOWN,VK_RETURN,0);pump();check(state.theme==1,"custom menu keyboard activation");state.theme=theme;}else chrome::closeMenu();
        openSettings();frames(3);noNativeControls(settingsWindow);snapshot(settingsWindow,L"theme-"+std::to_wstring(theme)+L"-settings");
        openFiles();frames(3);noNativeControls(fileDialog);snapshot(fileDialog,L"theme-"+std::to_wstring(theme)+L"-files");DestroyWindow(fileDialog);DestroyWindow(settingsWindow);
        openSchedule();frames(3);noNativeControls(scheduleWindow);snapshot(scheduleWindow,L"theme-"+std::to_wstring(theme)+L"-editor");DestroyWindow(scheduleWindow);
    }
    RECT normalSize{};GetWindowRect(mainWindow,&normalSize);int screenDpi=static_cast<int>(GetDpiForWindow(mainWindow));
    SetWindowPos(mainWindow,nullptr,0,0,MulDiv(240,screenDpi,96),MulDiv(200,screenDpi,96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT minimumSize{};GetWindowRect(mainWindow,&minimumSize);check(minimumSize.right-minimumSize.left==MulDiv(240,screenDpi,96) && minimumSize.bottom-minimumSize.top==MulDiv(200,screenDpi,96),"resize custom window to compact minimum");
    for(int page=0;page<3;++page){selectView(page);frames(12);snapshot(mainWindow,L"minimum-page-"+std::to_wstring(page));}
    SetWindowPos(mainWindow,nullptr,0,0,normalSize.right-normalSize.left,normalSize.bottom-normalSize.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);frames();
    openSchedule();editorText="保存日程的交互检查";editorYear=2026;editorMonth=2;editorDay=30;
    check(!saveSchedule() && !scheduleError.empty(),"invalid calendar date stays in custom editor");
    editorDay=28;editorHour=9;editorMinute=30;editorRepeat=2;check(saveSchedule(),"save calendar date through custom editor");DestroyWindow(scheduleWindow);
    auto edited=state.schedules.front().id;openSchedule(edited);editorText="更新安排";check(saveSchedule(),"edit schedule through custom editor");DestroyWindow(scheduleWindow);
    focusMinutes=30;shortMinutes=6;longMinutes=20;command(PomoConfigId);check(state.pomodoro.focusMinutes==30,"configure focus through UI action");command(PomoStartId);check(state.pomodoro.running,"start focus");command(PomoStartId);check(!state.pomodoro.running,"pause focus");command(PomoResetId);
    check(addRule(L"C:\\Windows\\System32\\notepad.exe"),"custom executable rule");check(!addRule(L"notepad.exe"),"duplicate rule displays inline validation");settingsError.clear();
    RECT normal{};GetWindowRect(mainWindow,&normal);
    for(int mode=0;mode<4;++mode) {
        command(CollapseFirstId+mode);frames();bool floating=mode<2;
        check(manuallyHidden && !IsWindowVisible(mainWindow) && IsWindowVisible(compactWindow)==floating,"collapse mode visibility");
        automaticallyHidden=true;applyVisibility();check(!IsWindowVisible(compactWindow) && !IsWindowVisible(mainWindow),"automatic hiding overrides custom UI");
        automaticallyHidden=false;applyVisibility();check(IsWindowVisible(compactWindow)==floating,"restore selected collapse mode");
        SendMessageW(mainWindow,ShowRequest,0,0);frames();check(!manuallyHidden && IsWindowVisible(mainWindow),"expand complete custom UI");
        RECT restored{};GetWindowRect(mainWindow,&restored);check(EqualRect(&normal,&restored),"preserve main window bounds");
    }
    openSettings();frames();automaticallyHidden=true;applyVisibility();check(!IsWindowVisible(settingsWindow),"hide custom settings");automaticallyHidden=false;applyVisibility();check(IsWindowVisible(settingsWindow),"restore custom settings");DestroyWindow(settingsWindow);
    selectView(0);auto until=GetTickCount64()+280;while(GetTickCount64()<until){frames(1);}check(!transitionStarted && contentOffset==0 && navigationPosition==0,"finish navigation animation");
    surface(mainWindow)->select();focusTaskInput=true;frames(2);surface(mainWindow)->select();ImGui::GetIO().AddInputCharactersUTF8("中文输入检查");frames(2);
    check(taskInput=="中文输入检查","custom text field accepts Chinese input events");
    auto previousCount=state.tasks.size();surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,true);frames(2);surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,false);frames(2);
    check(state.tasks.size()==previousCount+1 && state.tasks.back().text==L"中文输入检查","Enter saves text through custom input");
    editTask(first);frames(2);surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);frames(2);surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);frames(2);
    check(!editingId && taskInput.empty(),"Escape cancels editing through custom input");
    State persisted;persisted.load(dataFile);check(persisted.tasks.size()==state.tasks.size() && persisted.schedules.size()==state.schedules.size(),"persist UI changes to real data file");
    check(followCurrentDesktop({mainWindow,compactWindow}),"custom UI supports virtual desktop service");
    DestroyWindow(mainWindow);pump();ui::shutdown();DestroyIcon(appIcon);shutdownVirtualDesktops();Gdiplus::GdiplusShutdown(token);
    check(SetThreadDesktop(original) && CloseDesktop(desktop),"close isolated UI desktop");
    std::cout<<checks<<" custom UI checks passed\n";
    return 0;
}
