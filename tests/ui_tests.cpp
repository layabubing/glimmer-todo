#include "../src/app.cpp"
#include <gdiplus.h>
#include <iostream>
#include <optional>

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
void rightClickMain(ImVec2 position) {
    auto point=MAKELPARAM(static_cast<int>(position.x),static_cast<int>(position.y));
    SendMessageW(mainWindow,WM_MOUSEMOVE,0,point);
    SendMessageW(mainWindow,WM_RBUTTONDOWN,MK_RBUTTON,point);
    SendMessageW(mainWindow,WM_RBUTTONUP,0,point);
    check(chrome::menuWindow()!=nullptr,"right-click opens a context menu");
}
void rightClickTask(uint64_t id) {
    auto row=std::find_if(taskRowBounds.begin(),taskRowBounds.end(),[id](const TaskRowBounds& bounds){return bounds.id==id;});
    check(row!=taskRowBounds.end(),"test task row is visible");
    rightClickMain(row->rect.GetCenter());
}
void chooseMenuRow(int row) {
    auto menu=chrome::menuWindow();
    for(int index=0;index<=row;++index)SendMessageW(menu,WM_KEYDOWN,VK_DOWN,0);
    SendMessageW(menu,WM_KEYDOWN,VK_RETURN,0);
    frames();
}
void activateMainItem(ImGuiID id) {
    surface(mainWindow)->select();
    ImGui::ActivateItemByID(id);
    frames(3);
}
void pressMainKey(ImGuiKey key) {
    surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(key,true);frames();
    surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(key,false);frames();
}
ImGuiWindow* mainPopup() {
    surface(mainWindow)->select();
    auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
    check(!popups.empty() && popups.back().Window,"show popup for selected control");
    return popups.back().Window;
}
float renderedTextWidth() {
    auto view=surface(mainWindow);
    if(view->begin(state.theme,state.fontPercent))drawMain();
    float width=ImGui::CalcTextSize("字体比例检查").x;
    view->end();return width;
}
std::optional<std::wstring> startupEntry() {
    constexpr auto key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    DWORD bytes=0;auto result=RegGetValueW(HKEY_CURRENT_USER,key,L"GlimmerTodo",RRF_RT_REG_SZ,nullptr,nullptr,&bytes);
    require(result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND,L"无法读取自启检查前的设置。");
    if(result==ERROR_FILE_NOT_FOUND)return std::nullopt;
    std::wstring value(bytes/sizeof(wchar_t),L'\0');
    require(RegGetValueW(HKEY_CURRENT_USER,key,L"GlimmerTodo",RRF_RT_REG_SZ,nullptr,value.data(),&bytes)==ERROR_SUCCESS,L"无法读取自启启动项。");
    return std::wstring(value.c_str());
}
void restoreStartup(const std::optional<std::wstring>& original) {
    if(!original){startup::setEnabled(false);return;}
    HKEY key=nullptr;
    require(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS,L"无法恢复自启设置。");
    auto result=RegSetValueExW(key,L"GlimmerTodo",0,REG_SZ,reinterpret_cast<const BYTE*>(original->c_str()),static_cast<DWORD>((original->size()+1)*sizeof(wchar_t)));RegCloseKey(key);
    require(result==ERROR_SUCCESS,L"无法恢复自启启动项。");
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
    auto originalStartup=startupEntry();startup::setEnabled(false);bool disabledBefore=!startup::enabled();
    menu(mainWindow);for(int index=0;index<8;++index)SendMessageW(chrome::menuWindow(),WM_KEYDOWN,VK_DOWN,0);
    SendMessageW(chrome::menuWindow(),WM_KEYDOWN,VK_RETURN,0);pump();bool enabledFromMenu=startup::enabled();auto registeredStartup=startupEntry();
    command(MenuStartup);bool disabledFromCommand=!startup::enabled() && !startupEntry().has_value();restoreStartup(originalStartup);
    wchar_t startupProgram[32768]{};GetModuleFileNameW(nullptr,startupProgram,static_cast<DWORD>(std::size(startupProgram)));
    check(disabledBefore && enabledFromMenu,"custom tray menu enables real Windows startup registration");
    check(registeredStartup==std::optional<std::wstring>(L"\""+std::wstring(startupProgram)+L"\""),"startup registration contains quoted executable path");
    check(disabledFromCommand,"shared settings action removes Windows startup registration");
    check(startupEntry()==originalStartup,"restore original startup preference after checks");
    taskInput="整理今天的想法 📝";check(submitTask(),"create task through UI action");auto first=state.tasks.front().id;
    taskInput="为专注留出半个小时";check(submitTask(),"create second task");auto second=state.tasks.back().id;state.toggleTask(first);save();
    taskInput="确认下一个日程";check(submitTask(),"create third task for compact list");
    auto fourth=state.nextId;taskInput="完成今天的阅读";check(submitTask(),"create fourth task for compact list");
    editTask(first);taskInput="整理今天的想法与笔记 📝";check(submitTask() && state.findTask(first)->text==L"整理今天的想法与笔记 📝","edit Unicode task through UI action");
    editTask(first);cancelEdit();check(!editingId && taskInput.empty(),"cancel task editing");
    frames();rightClickTask(second);
    check(selectedTaskId==second,"right-click selects the clicked task instead of the previous selection");
    snapshot(chrome::menuWindow(),L"task-context-menu");
    chooseMenuRow(0);check(editingId==second,"task context menu edits the clicked row");cancelEdit();
    rightClickTask(second);chooseMenuRow(2);check(state.findTask(second)->done,"task context menu completes the clicked row");
    rightClickTask(second);chooseMenuRow(2);check(!state.findTask(second)->done,"task context menu restores the clicked row");
    rightClickTask(fourth);chooseMenuRow(3);check(!state.findTask(fourth) && state.tasks.size()==3,"task context menu deletes only the clicked row");
    rightClickMain(ui::size(8,80));chooseMenuRow(0);check(manuallyHidden,"right-click on the main window background opens the application menu");
    SendMessageW(mainWindow,ShowRequest,0,0);frames();
    SendMessageW(mainWindow,TrayMessage,0,WM_RBUTTONUP);
    check(chrome::menuWindow()!=nullptr,"tray right-click opens the application menu");
    chooseMenuRow(0);check(manuallyHidden,"tray menu still controls the application window");
    SendMessageW(mainWindow,ShowRequest,0,0);frames();
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
    float normalTextWidth=renderedTextWidth();
    RECT normalSize{};GetWindowRect(mainWindow,&normalSize);int screenDpi=static_cast<int>(GetDpiForWindow(mainWindow));
    for(int choice:{0,7}) {
        command(FontFirstId+choice);frames(3);float width=renderedTextWidth();
        check(choice==0?width<normalTextWidth*0.9f:width>normalTextWidth*1.4f,"font preference changes actual text dimensions");
        for(int page=0;page<3;++page){selectView(page);frames(12);snapshot(mainWindow,L"font-"+std::to_wstring(state.fontPercent)+L"-page-"+std::to_wstring(page));}
        openSettings();frames(3);snapshot(settingsWindow,L"font-"+std::to_wstring(state.fontPercent)+L"-settings");DestroyWindow(settingsWindow);
        openSchedule();frames(3);snapshot(scheduleWindow,L"font-"+std::to_wstring(state.fontPercent)+L"-editor");DestroyWindow(scheduleWindow);
        openFiles();frames(3);snapshot(fileDialog,L"font-"+std::to_wstring(state.fontPercent)+L"-files");DestroyWindow(fileDialog);
        openImport();frames(3);noNativeControls(importWindow);snapshot(importWindow,L"font-"+std::to_wstring(state.fontPercent)+L"-import");
        automaticallyHidden=true;applyVisibility();check(!IsWindowVisible(importWindow),"automatically hide import window");automaticallyHidden=false;applyVisibility();check(IsWindowVisible(importWindow),"restore import window");DestroyWindow(importWindow);
        RECT after{};GetWindowRect(mainWindow,&after);check(EqualRect(&normalSize,&after),"font preference preserves compact window dimensions");
    }
    SetWindowPos(mainWindow,nullptr,0,0,MulDiv(240,screenDpi,96),MulDiv(200,screenDpi,96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT minimumSize{};GetWindowRect(mainWindow,&minimumSize);check(minimumSize.right-minimumSize.left==MulDiv(240,screenDpi,96) && minimumSize.bottom-minimumSize.top==MulDiv(200,screenDpi,96),"resize custom window to compact minimum");
    for(int page=0;page<3;++page){selectView(page);frames(12);snapshot(mainWindow,L"minimum-page-"+std::to_wstring(page));}
    command(FontFirstId+2);frames();
    SetWindowPos(mainWindow,nullptr,0,0,normalSize.right-normalSize.left,normalSize.bottom-normalSize.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);frames();
    openSchedule();editorText="保存日程的交互检查";editorYear=2026;editorMonth=2;editorDay=30;
    check(!saveSchedule() && !scheduleError.empty(),"invalid calendar date stays in custom editor");
    editorDay=28;editorHour=9;editorMinute=30;editorRepeat=2;check(saveSchedule(),"save calendar date through custom editor");DestroyWindow(scheduleWindow);
    auto edited=state.schedules.front().id;openSchedule(edited);editorText="更新安排";check(saveSchedule(),"edit schedule through custom editor");DestroyWindow(scheduleWindow);
    selectView(0);frames(12);
    auto priorityTaskId=state.tasks[1].id;
    editTask(priorityTaskId);frames();
    surface(mainWindow)->select();
    auto* root=ImGui::FindWindowByName("##surface");
    activateMainItem(root->GetID("##taskDetails"));
    auto* options=mainPopup();
    activateMainItem(options->GetID("##priority"));
    pressMainKey(ImGuiKey_RightArrow);
    int higherPriority=taskPriority;
    activateMainItem(options->GetID("设置截止日期"));
    check(taskHasDueDate && higherPriority>DefaultTaskPriority,"move the priority slider and enable the date from task options");
    taskYear=2028;taskMonth=2;taskDay=30;
    activateMainItem(options->GetID("确定"));
    check(!taskOptionsError.empty() && taskDetails.dueDate==0,"invalid deadline remains in the options popup");
    snapshot(mainWindow,L"task-options");
    taskDay=29;activateMainItem(options->GetID("确定"));
    activateMainItem(root->GetID("保存"));
    check(state.tasks.front().id==priorityTaskId && state.tasks.front().details.priority==higherPriority
        && state.tasks.front().details.dueDate==20280229,"save task details and reorder the displayed list");
    auto titleBefore=state.tasks.front().text;
    rightClickTask(priorityTaskId);chooseMenuRow(1);
    options=mainPopup();
    check(taskOptionsId==priorityTaskId && taskPriority==higherPriority,"context action opens saved priority for an existing task");
    activateMainItem(options->GetID("##priority"));pressMainKey(ImGuiKey_RightArrow);
    activateMainItem(options->GetID("确定"));
    check(!editingId && state.tasks.front().details.priority>higherPriority && state.tasks.front().details.dueDate==20280229
        && state.tasks.front().text==titleBefore,"context action saves changed priority while preserving the task and deadline");
    selectView(2);frames(12);
    surface(mainWindow)->select();
    auto focusBodyId=root->GetID("##focusBody");
    ImGuiWindow* focusBody=nullptr;
    for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->ChildId==focusBodyId)focusBody=window;
    check(focusBody!=nullptr,"show focus page controls");
    auto comboId=focusBody->GetID("##pomodoroTask");
    activateMainItem(comboId);
    auto taskId=std::to_string(second);
    auto taskScope=mainPopup()->GetID(taskId.c_str());
    activateMainItem(ImHashStr("##task",0,taskScope));
    check(state.pomodoro.taskId==second,"bind task from the focus page dropdown");
    snapshot(mainWindow,L"focus-bound-task");
    focusMinutes=30;shortMinutes=6;longMinutes=20;command(PomoConfigId);check(state.pomodoro.focusMinutes==30,"configure focus through UI action");command(PomoStartId);check(state.pomodoro.running,"start focus");command(PomoStartId);check(!state.pomodoro.running,"pause focus");command(PomoResetId);
    check(state.pomodoro.taskId==second,"keep task binding through start, pause and reset");
    selectView(0);frames(12);
    check(taskRowBounds.front().id==second && taskRowBounds[1].id==priorityTaskId && taskRowBounds.back().id==first,
        "show bound task, highest priority pending task, then completed task");
    snapshot(mainWindow,L"task-priority-order");
    selectView(2);frames(12);
    activateMainItem(comboId);
    activateMainItem(mainPopup()->GetID("不绑定任务"));
    check(state.pomodoro.taskId==0,"unbind from the focus page dropdown");
    check(state.tasks.front().id==priorityTaskId,"unbinding restores priority order");
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
    check(state.tasks.size()==previousCount+1 && state.tasks[state.tasks.size()-2].text==L"中文输入检查","Enter saves text through custom input");
    editTask(first);frames(2);surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);frames(2);surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);frames(2);
    check(!editingId && taskInput.empty(),"Escape cancels editing through custom input");
    command(CollapseFirstId+2);command(ImportId);frames(2);check(!manuallyHidden && IsWindowVisible(importWindow),"import command expands collapsed application");DestroyWindow(importWindow);
    surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,true);ImGui::GetIO().AddKeyEvent(ImGuiKey_I,true);frames(2);
    check(importWindow && IsWindowVisible(importWindow),"Ctrl+I opens import window");surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiKey_I,false);ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,false);
    auto beforeImport=state.tasks.size();importText="有效任务\n"+std::string(501,'x');
    check(!submitImport() && !importError.empty() && state.tasks.size()==beforeImport,"invalid import remains editable without adding partial tasks");
    importText.clear();importError.clear();focusImportInput=true;frames(2);surface(importWindow)->select();ImGui::GetIO().AddInputCharactersUTF8("批量任务甲\n\n批量任务乙 📝");frames(2);
    check(importText=="批量任务甲\n\n批量任务乙 📝","multiline import field accepts Unicode input");snapshot(importWindow,L"batch-import");
    surface(importWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,true);ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,true);frames(2);
    check(!importWindow && state.tasks.size()==beforeImport+2 && state.tasks[state.tasks.size()-2].text==L"批量任务乙 📝","Ctrl+Enter imports and closes the custom editor");
    surface(mainWindow)->select();ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,false);ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,false);frames(2);
    State persisted;persisted.load(dataFile);check(persisted.tasks.size()==state.tasks.size() && persisted.schedules.size()==state.schedules.size(),"persist UI changes to real data file");
    check(followCurrentDesktop({mainWindow,compactWindow}),"custom UI supports virtual desktop service");
    DestroyWindow(mainWindow);pump();ui::shutdown();DestroyIcon(appIcon);shutdownVirtualDesktops();Gdiplus::GdiplusShutdown(token);
    check(SetThreadDesktop(original) && CloseDesktop(desktop),"close isolated UI desktop");
    std::cout<<checks<<" custom UI checks passed\n";
    return 0;
}
