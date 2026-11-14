#include "model.hpp"
#include "virtual_desktop.hpp"
#include "updater.hpp"
#include "version.hpp"
#include "theme.hpp"
#include "taskbar_panel.hpp"
#include "chrome.hpp"
#include "ui.hpp"
#include "filedialog_config.hpp"
#include "../vendor/imgui/misc/cpp/imgui_stdlib.h"
#include "../vendor/filedialog/ImGuiFileDialog.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <array>
#include <chrono>
#include <memory>
#include <cmath>

namespace {
constexpr UINT TrayMessage=WM_APP+1,ForegroundMessage=WM_APP+2,ShowRequest=WM_APP+3,TaskbarMenuRequest=WM_APP+4;
constexpr int AddId=101,SettingsId=104,HideId=105,ScheduleAddId=112,ScheduleSaveId=113,ViewFirstId=114;
constexpr int PomoStartId=120,PomoResetId=121,PomoSkipId=122,PomoConfigId=123;
constexpr int ThemeFirstId=500,MenuShow=301,MenuAuto=302,MenuTop=303,MenuSettings=304,MenuExit=305,MenuClear=306;
constexpr int CollapseFirstId=320,BrowseId=206;
enum class WindowKind { Main,Settings,Schedule,Files };

HINSTANCE instance;
HWND mainWindow,settingsWindow,scheduleWindow,fileDialog,compactWindow;
std::array<std::unique_ptr<ui::Surface>,4> surfaces;
HWINEVENTHOOK foregroundHook,desktopHook;
HICON appIcon;
NOTIFYICONDATAW tray{};
UINT taskbarCreated;
State state;
std::filesystem::path dataFile;
bool manuallyHidden=false,automaticallyHidden=false,ready=false,applyingVisibility=false,rendering=false;
int dpi=96,compactDpi=96,activeView=0;
ULONGLONG transitionStarted=0,nextNotice=0;
double navigationPosition=0,navigationFrom=0;
int contentOffset=0;
uint64_t editingId=0,selectedTaskId=0,selectedScheduleId=0,scheduleEditingId=0,pendingScheduleId=0;
bool focusTaskInput=false,installConfirmation=false;
std::string taskInput,ruleInput,editorText,taskError,settingsError,scheduleError;
int editorYear=2026,editorMonth=1,editorDay=1,editorHour=9,editorMinute=0,editorRepeat=0;
int focusMinutes=25,shortMinutes=5,longMinutes=15;
std::unique_ptr<IGFD::FileDialog> filePicker;
HFONT compactFont,compactSmallFont;
std::wstring compactDetails,lastTrayTip;
chrome::Tooltip compactTooltip;

int dip(int value){return MulDiv(value,96,dpi);}
int cp(int value){return MulDiv(value,compactDpi,96);}
void save(){state.save(dataFile);}
void evaluateForeground();
void applyVisibility();
void command(int id);
void openSettings();
void openSchedule(uint64_t id=0);
void openFiles();
void renderWindows();
void positionCompact();
ui::Surface* surface(HWND window) {
    for(auto& item:surfaces)if(item && item->window()==window)return item.get();
    return nullptr;
}
void post(int id){PostMessageW(mainWindow,WM_COMMAND,id,0);}
std::string str(const std::wstring& value){return utf8(value);}
void fill(HDC dc,RECT rect,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}
void label(HDC dc,const std::wstring& text,RECT rect,HFONT font,COLORREF color,UINT flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS) {
    auto previous=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text.c_str(),-1,&rect,flags|DT_NOPREFIX);SelectObject(dc,previous);
}
void notify(const std::wstring& title,const std::wstring& message) {
    auto info=tray;info.uFlags=NIF_INFO;lstrcpynW(info.szInfoTitle,title.c_str(),static_cast<int>(std::size(info.szInfoTitle)));lstrcpynW(info.szInfo,message.c_str(),static_cast<int>(std::size(info.szInfo)));
    info.dwInfoFlags=NIIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&info);nextNotice=GetTickCount64()+6000;
}
void updateTray() {
    auto info=state.compactInfo(nowSeconds());std::wstring title=automaticallyHidden?L"浮光 · 自动隐藏":manuallyHidden?std::wstring(L"浮光 · ")+CollapseNames[static_cast<int>(state.collapseMode)]:L"浮光待办";
    auto hint=title+L"\n"+info.details;hint.resize((std::min)(hint.size(),std::size(tray.szTip)-1));if(lastTrayTip==hint)return;
    lastTrayTip=hint;lstrcpynW(tray.szTip,hint.c_str(),static_cast<int>(std::size(tray.szTip)));Shell_NotifyIconW(NIM_MODIFY,&tray);
}
void addTray() {
    lastTrayTip.clear();tray.cbSize=sizeof(tray);tray.hWnd=mainWindow;tray.uID=1;tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;tray.uCallbackMessage=TrayMessage;tray.hIcon=appIcon;lstrcpyW(tray.szTip,L"浮光待办");
    require(Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE,L"无法创建系统托盘图标。");
}
void selectView(int view) {
    if(view<0 || view>2)return;
    BOOL motion=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0);
    navigationFrom=navigationPosition;bool animate=ready && view!=activeView && motion && IsWindowVisible(mainWindow);
    activeView=view;transitionStarted=animate?GetTickCount64():0;contentOffset=animate?7:0;if(!animate)navigationPosition=view;
}
void animateView() {
    if(!transitionStarted)return;
    double progress=transitionProgress(GetTickCount64()-transitionStarted);navigationPosition=navigationFrom+(activeView-navigationFrom)*progress;contentOffset=static_cast<int>(7*(1-progress));
    if(progress>=1){transitionStarted=0;contentOffset=0;}
}
void cancelEdit(){editingId=0;taskInput.clear();taskError.clear();}
bool submitTask() {
    auto value=wide(taskInput);bool changed=editingId?state.editTask(editingId,value):state.addTask(value);
    if(!changed){taskError="请填写待办内容，最多 500 个字符。";return false;}
    save();cancelEdit();focusTaskInput=true;return true;
}
void editTask(uint64_t id) {
    for(const auto& task:state.tasks)if(task.id==id){editingId=id;selectedTaskId=id;taskInput=str(task.text);taskError.clear();focusTaskInput=true;return;}
}
void removeTask(uint64_t id){if(editingId==id)cancelEdit();state.removeTask(id);if(selectedTaskId==id)selectedTaskId=0;save();}
bool addRule(const std::wstring& path) {
    if(!state.addRule(path)){settingsError="请输入有效的 EXE 文件名称，已经添加的软件无需重复添加。";return false;}
    save();ruleInput.clear();settingsError.clear();evaluateForeground();return true;
}
bool saveSchedule() {
    using namespace std::chrono;
    year_month_day date{year{editorYear},month{static_cast<unsigned>(editorMonth)},day{static_cast<unsigned>(editorDay)}};
    if(editorYear<1601 || editorYear>9999 || !date.ok() || editorHour<0 || editorHour>23 || editorMinute<0 || editorMinute>59){scheduleError="请填写有效的日期与时间。";return false;}
    SYSTEMTIME local{};local.wYear=static_cast<WORD>(editorYear);local.wMonth=static_cast<WORD>(editorMonth);local.wDay=static_cast<WORD>(editorDay);local.wHour=static_cast<WORD>(editorHour);local.wMinute=static_cast<WORD>(editorMinute);
    auto due=timestampFromLocal(local);
    if(!state.putSchedule(scheduleEditingId,wide(editorText),due,editorRepeat)){scheduleError="请填写日程内容，最多 500 个字符。";return false;}
    save();scheduleError.clear();return true;
}
void scheduleNotices() {
    if(automaticallyHidden || GetTickCount64()<nextNotice)return;
    for(auto& event:state.schedules)if(!event.done && !event.notified && event.due<=nowSeconds()){notify(L"日程提醒",formatDate(event.due)+L"\n"+event.text);event.notified=true;save();return;}
}
void pomodoroTick() {
    if(state.pomodoro.tick(nowSeconds()))save();
    if(state.pomodoro.noticePending && !automaticallyHidden && GetTickCount64()>=nextNotice){notify(L"番茄钟",state.pomodoro.phase==0?L"休息结束，可以开始下一轮专注。":L"专注结束，请休息。点击专注页面的开始按钮计时。");state.pomodoro.noticePending=false;save();}
}
void errorText(const std::string& value) {
    if(value.empty())return;
    ImGui::PushStyleColor(ImGuiCol_Text,ui::color(state.theme==1?RGB(255,173,163):RGB(183,73,66)));ImGui::TextWrapped("%s",value.c_str());ImGui::PopStyleColor();
}
void numberFields(const char* id,const char* const* labels,int* const* values,int count) {
    ImGui::PushID(id);auto start=ImGui::GetCursorScreenPos();float width=(ImGui::GetContentRegionAvail().x-ui::p(8)*(count-1))/count,end=start.y;
    for(int i=0;i<count;++i) {
        float x=start.x+(width+ui::p(8))*i;ImGui::SetCursorScreenPos({x,start.y});ui::muted(labels[i]);ImGui::SetCursorScreenPos({x,ImGui::GetCursorScreenPos().y});ImGui::SetNextItemWidth(width);ImGui::PushID(i);ImGui::InputInt("##number",values[i],0,0);ImGui::PopID();end=(std::max)(end,ImGui::GetItemRectMax().y);
    }
    ImGui::SetCursorScreenPos({start.x,end+ImGui::GetStyle().ItemSpacing.y});ImGui::Dummy({0,0});ImGui::PopID();
}
void closeButton(HWND window) {
    ImGui::SameLine(ImGui::GetWindowWidth()-ui::p(44));if(ui::iconButton("##close",ui::Icon::Close,"关闭"))PostMessageW(window,WM_CLOSE,0,0);
}
void auxHeader(HWND window,const char* title,const char* subtitle) {
    ui::text(title,19);closeButton(window);if(subtitle)ui::muted(subtitle);ImGui::Spacing();ImGui::Spacing();
}
void drawThemePopup() {
    if(ImGui::BeginPopup("##themes")) {
        for(int i=0;i<3;++i){const char* names[]={"晨光 · 浅色","夜色 · 深色","森林 · 绿色"};if(ImGui::Selectable(names[i],state.theme==i))post(ThemeFirstId+i);}
        ImGui::EndPopup();
    }
}
void drawHeader() {
    auto start=ImGui::GetCursorScreenPos();auto draw=ImGui::GetWindowDrawList();const auto& c=ui::palette();
    draw->AddRectFilled(start,{start.x+ui::p(29),start.y+ui::p(29)},ui::ink(c.accent),ui::p(10));ui::icon(draw,ui::Icon::Check,{start.x+ui::p(14.5f),start.y+ui::p(14.5f)},7,ui::ink(c.onAccent),1.9f);
    ImGui::Dummy(ui::size(29,30));ImGui::SameLine(0,ui::p(9));ImGui::BeginGroup();ui::text("浮光",18);ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetWindowWidth()-ui::p(112));
    if(ui::iconButton("##theme",ui::Icon::Theme,"切换主题")){ImGui::OpenPopup("##themes");}
    ImGui::SameLine(0,ui::p(2));
    if(ui::iconButton("##settings",ui::Icon::Settings,"设置")){post(SettingsId);}
    ImGui::SameLine(0,ui::p(2));
    if(ui::iconButton("##collapse",ui::Icon::Collapse,"收起窗口"))post(HideId);
    drawThemePopup();ImGui::Spacing();
    float width=ImGui::GetContentRegionAvail().x;auto nav=ImGui::GetCursorScreenPos();float segment=width/3;
    draw->AddRectFilled(nav,{nav.x+width,nav.y+ui::p(38)},ui::ink(c.track),ui::p(12));
    float x=nav.x+static_cast<float>(navigationPosition)*segment+ui::p(3);draw->AddRectFilled({x,nav.y+ui::p(3)},{x+segment-ui::p(6),nav.y+ui::p(35)},ui::ink(c.surface),ui::p(9));
    const char* names[]={"待办","日程","专注"};const ui::Icon icons[]={ui::Icon::List,ui::Icon::Calendar,ui::Icon::Timer};
    for(int i=0;i<3;++i) {
        if(i){ImGui::SameLine(0,0);}
        ImGui::PushID(i);auto pos=ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##nav",{segment,ui::p(38)},ImGuiButtonFlags_EnableNav))selectView(i);
        if(ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow)))selectView((i+(ImGui::IsKeyPressed(ImGuiKey_RightArrow)?1:2))%3);
        auto tint=ui::ink(activeView==i?c.accent:c.muted);ui::icon(draw,icons[i],{pos.x+segment/2-ui::p(18),pos.y+ui::p(19)},6,tint);
        draw->AddText({pos.x+segment/2-ui::p(5),pos.y+(ui::p(38)-ImGui::GetFontSize())/2},tint,names[i]);ImGui::PopID();
    }
    ImGui::Spacing();
}
void emptyState(const char* title,const char* subtitle,ui::Icon kind) {
    auto region=ImGui::GetContentRegionAvail();float y=(std::max)(ui::p(8),(region.y-ui::p(96))/2);ImGui::Dummy({0,y});
    auto start=ImGui::GetCursorScreenPos();float x=start.x+region.x/2;auto draw=ImGui::GetWindowDrawList();
    draw->AddCircleFilled({x,start.y+ui::p(20)},ui::p(23),ui::ink(ui::palette().selected));ui::icon(draw,kind,{x,start.y+ui::p(20)},10,ui::ink(ui::palette().accent));ImGui::Dummy(ui::size(0,51));
    float width=ImGui::CalcTextSize(title).x;ImGui::SetCursorPosX((ImGui::GetWindowWidth()-width)/2);ui::text(title,13);
    ImGui::PushFont(nullptr,11);width=ImGui::CalcTextSize(subtitle).x;ImGui::SetCursorPosX((ImGui::GetWindowWidth()-width)/2);ui::muted(subtitle);ImGui::PopFont();
}

void drawTaskRow(const Task& task) {
    ImGui::PushID(static_cast<int>(task.id));auto origin=ImGui::GetCursorScreenPos();float width=ImGui::GetContentRegionAvail().x;const auto& c=ui::palette();auto draw=ImGui::GetWindowDrawList();
    auto background=selectedTaskId==task.id?c.selected:c.surface;draw->AddRectFilled(origin,{origin.x+width,origin.y+ui::p(58)},ui::ink(background),ui::p(12));draw->AddRect(origin,{origin.x+width,origin.y+ui::p(58)},ui::ink(selectedTaskId==task.id?c.accent:c.border),ui::p(12));
    ImGui::SetCursorScreenPos({origin.x+ui::p(12),origin.y+ui::p(19)});bool checked=task.done;
    if(ImGui::InvisibleButton("##check",ui::size(20,20),ImGuiButtonFlags_EnableNav)){state.toggleTask(task.id);save();}
    auto box=ImGui::GetItemRectMin();draw->AddCircle({box.x+ui::p(10),box.y+ui::p(10)},ui::p(8),ui::ink(checked?c.accent:c.border),20,ui::p(1.4f));
    if(checked){draw->AddCircleFilled({box.x+ui::p(10),box.y+ui::p(10)},ui::p(8),ui::ink(c.accent));ui::icon(draw,ui::Icon::Check,{box.x+ui::p(10),box.y+ui::p(10)},5,ui::ink(c.onAccent));}
    ImGui::SetCursorScreenPos({origin.x+ui::p(43),origin.y+ui::p(6)});ImGui::PushStyleColor(ImGuiCol_Header,ui::color(background));
    bool selected=ImGui::Selectable("##task",false,ImGuiSelectableFlags_AllowDoubleClick,{width-ui::p(77),ui::p(45)});ImGui::PopStyleColor();
    if(selected)selectedTaskId=task.id;
    if((ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) || (ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_F2))))editTask(task.id);
    if(ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Space)){state.toggleTask(task.id);save();}
    auto value=str(task.text);ImVec2 min{origin.x+ui::p(43),origin.y+ui::p(10)},max{origin.x+width-ui::p(35),origin.y+ui::p(50)};
    draw->PushClipRect(min,max,true);draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),min,ui::ink(task.done?c.muted:c.text),value.c_str(),nullptr,max.x-min.x);
    if(task.done){draw->AddLine({min.x,min.y+ImGui::GetFontSize()/2},{(std::min)(max.x,min.x+ImGui::CalcTextSize(value.c_str()).x),min.y+ImGui::GetFontSize()/2},ui::ink(c.muted));}
    draw->PopClipRect();
    ImGui::SetCursorScreenPos({origin.x+width-ui::p(31),origin.y+ui::p(15)});if(ui::iconButton("##remove",ui::Icon::Close,"删除待办",false,25)){selectedTaskId=task.id;post(600);}
    ImGui::SetCursorScreenPos({origin.x,origin.y+ui::p(66)});ImGui::Dummy({width,0});ImGui::PopID();
}
void drawTasks() {
    auto done=std::count_if(state.tasks.begin(),state.tasks.end(),[](const Task& task){return task.done;});
    ui::text("今天的清单",14);ImGui::SameLine(ImGui::GetWindowWidth()-ui::p(74));auto count=std::to_string(done)+" / "+std::to_string(state.tasks.size());ui::muted(count.c_str());
    auto start=ImGui::GetCursorScreenPos();float width=ImGui::GetContentRegionAvail().x;auto draw=ImGui::GetWindowDrawList();draw->AddRectFilled(start,{start.x+width,start.y+ui::p(3)},ui::ink(ui::palette().track),ui::p(2));
    if(!state.tasks.empty() && done){draw->AddRectFilled(start,{start.x+width*static_cast<float>(done)/state.tasks.size(),start.y+ui::p(3)},ui::ink(ui::palette().accent),ui::p(2));}
    ImGui::Dummy(ui::size(0,7));
    float listHeight=(std::max)(ui::p(42),ImGui::GetContentRegionAvail().y-ui::p(taskError.empty()?54:84));ImGui::PushStyleColor(ImGuiCol_ChildBg,ui::color(ui::palette().background));ImGui::BeginChild("##tasks",{0,listHeight},ImGuiChildFlags_None);
    if(state.tasks.empty())emptyState("给今天留一点空间","在下方记录下一件事",ui::Icon::Check);
    for(const auto& task:state.tasks)drawTaskRow(task);
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedTaskId)post(600);
    ImGui::EndChild();ImGui::PopStyleColor();errorText(taskError);
    bool cancel=ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
    if(focusTaskInput){ImGui::SetKeyboardFocusHere();focusTaskInput=false;}
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ui::p(editingId?61:45));
    if(ImGui::InputTextWithHint("##taskInput",editingId?"编辑待办，Escape 取消":"记录下一件事…",&taskInput,ImGuiInputTextFlags_EnterReturnsTrue))submitTask();
    if(cancel)cancelEdit();
    ImGui::SameLine(0,ui::p(7));if(ui::button(editingId?"保存":"＋",ui::size(editingId?54:38,32),true))submitTask();
}

void drawScheduleRow(const Schedule& event) {
    ImGui::PushID(static_cast<int>(event.id));auto pos=ImGui::GetCursorScreenPos();float width=ImGui::GetContentRegionAvail().x;auto draw=ImGui::GetWindowDrawList();const auto& c=ui::palette();
    draw->AddRectFilled(pos,{pos.x+width,pos.y+ui::p(70)},ui::ink(c.surface),ui::p(12));draw->AddRect(pos,{pos.x+width,pos.y+ui::p(70)},ui::ink(c.border),ui::p(12));
    ImGui::SetCursorScreenPos({pos.x+ui::p(12),pos.y+ui::p(23)});if(ImGui::InvisibleButton("##done",ui::size(20,20),ImGuiButtonFlags_EnableNav)){selectedScheduleId=event.id;post(602);}
    draw->AddCircle({pos.x+ui::p(22),pos.y+ui::p(33)},ui::p(8),ui::ink(event.done?c.accent:c.border),20,ui::p(1.4f));if(event.done)ui::icon(draw,ui::Icon::Check,{pos.x+ui::p(22),pos.y+ui::p(33)},5,ui::ink(c.accent));
    ImGui::SetCursorScreenPos({pos.x+ui::p(43),pos.y+ui::p(8)});if(ImGui::InvisibleButton("##event",{width-ui::p(80),ui::p(52)},ImGuiButtonFlags_EnableNav))selectedScheduleId=event.id;
    if((ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) || (ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_F2)))){pendingScheduleId=event.id;post(ScheduleAddId);}
    std::string title=str(event.text),date=str(formatDate(event.due))+(event.repeat==1?" · 每天":event.repeat==2?" · 每周":"");
    draw->PushClipRect({pos.x+ui::p(43),pos.y},{pos.x+width-ui::p(35),pos.y+ui::p(70)},true);draw->AddText({pos.x+ui::p(43),pos.y+ui::p(12)},ui::ink(event.done?c.muted:c.text),title.c_str());
    ImGui::PushFont(nullptr,10);draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{pos.x+ui::p(43),pos.y+ui::p(42)},ui::ink(event.due<nowSeconds() && !event.done?c.accent:c.muted),date.c_str());ImGui::PopFont();draw->PopClipRect();
    ImGui::SetCursorScreenPos({pos.x+width-ui::p(31),pos.y+ui::p(22)});if(ui::iconButton("##remove",ui::Icon::Close,"删除日程",false,25)){selectedScheduleId=event.id;post(601);}
    ImGui::SetCursorScreenPos({pos.x,pos.y+ui::p(78)});ImGui::Dummy({width,0});ImGui::PopID();
}
void drawSchedules() {
    ui::text("接下来的安排",14);ui::muted("留出时间，让重要的事如期发生");
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ui::color(ui::palette().background));ImGui::BeginChild("##schedules",{0,(std::max)(ui::p(40),ImGui::GetContentRegionAvail().y-ui::p(45))},ImGuiChildFlags_None);
    if(state.schedules.empty())emptyState("还没有日程","为下一件重要的事留出时间",ui::Icon::Calendar);
    for(const auto& event:state.schedules)drawScheduleRow(event);
    if(ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedScheduleId)post(601);
    ImGui::EndChild();ImGui::PopStyleColor();if(ui::button("＋  新增日程",{ImGui::GetContentRegionAvail().x,ui::p(34)},true)){pendingScheduleId=0;post(ScheduleAddId);}
}

void drawPomodoro() {
    auto& timer=state.pomodoro;int seconds=timer.secondsLeft(nowSeconds());char clock[32]{};snprintf(clock,sizeof(clock),"%02d:%02d",seconds/60,seconds%60);
    ImGui::BeginChild("##focusBody",{0,0},ImGuiChildFlags_None);ui::card("##clock",ui::size(0,154));
    const char* phase=timer.phase==0?"专注时间":timer.phase==1?"短暂休息":"长时间休息";float textWidth=ImGui::CalcTextSize(phase).x;ImGui::SetCursorPosX((ImGui::GetWindowWidth()-textWidth)/2);ui::text(phase,12,ui::palette().accent);
    ImGui::PushFont(nullptr,43);textWidth=ImGui::CalcTextSize(clock).x;ImGui::SetCursorPosX((ImGui::GetWindowWidth()-textWidth)/2);ui::text(clock,43);ImGui::PopFont();
    std::string count="今天已完成 "+std::to_string(timer.todayCount)+" 次专注";ImGui::PushFont(nullptr,11);textWidth=ImGui::CalcTextSize(count.c_str()).x;ImGui::SetCursorPosX((ImGui::GetWindowWidth()-textWidth)/2);ui::muted(count.c_str());ImGui::PopFont();
    auto pos=ImGui::GetCursorScreenPos();float width=ImGui::GetContentRegionAvail().x;auto draw=ImGui::GetWindowDrawList();draw->AddRectFilled(pos,{pos.x+width,pos.y+ui::p(3)},ui::ink(ui::palette().track),ui::p(2));float progress=1.0f-static_cast<float>(seconds)/timer.duration();
    if(progress>0){draw->AddRectFilled(pos,{pos.x+width*std::clamp(progress,0.0f,1.0f),pos.y+ui::p(3)},ui::ink(ui::palette().accent),ui::p(2));}
    ui::endCard();
    const char* action=timer.running?"暂停计时":timer.remaining<timer.duration()?"继续计时":timer.phase==0?"开始专注":"开始休息";
    if(ui::button(action,{ImGui::GetContentRegionAvail().x,ui::p(36)},true))post(PomoStartId);
    float half=(ImGui::GetContentRegionAvail().x-ui::p(8))/2;
    if(ui::button("重新开始",{half,ui::p(30)})){post(PomoResetId);}
    ImGui::SameLine();if(ui::button("跳过阶段",{half,ui::p(30)})){post(PomoSkipId);}
    if(ImGui::CollapsingHeader("计时设置")) {
        ui::muted("各阶段时长（分钟）");ImGui::BeginDisabled(timer.running);
        int* values[]={&focusMinutes,&shortMinutes,&longMinutes};const char* names[]={"专注","短休息","长休息"};numberFields("durations",names,values,3);
        if(ui::button("保存时长",{ImGui::GetContentRegionAvail().x,0})){post(PomoConfigId);}
        ImGui::EndDisabled();errorText(taskError);
    }
    ImGui::EndChild();
}
void drawMain() {
    animateView();drawHeader();if(contentOffset)ImGui::SetCursorPosY(ImGui::GetCursorPosY()+ui::p(static_cast<float>(contentOffset)));
    if(activeView==0){drawTasks();}
    if(activeView==1){drawSchedules();}
    if(activeView==2){drawPomodoro();}
    auto& io=ImGui::GetIO();if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)){selectView(0);focusTaskInput=true;}
}
void choiceTile(const char* id,const char* title,const char* subtitle,bool selected,ImVec2 extent,int commandId) {
    ImGui::PushID(id);auto pos=ImGui::GetCursorScreenPos();if(ImGui::InvisibleButton("##choice",extent,ImGuiButtonFlags_EnableNav))post(commandId);
    auto draw=ImGui::GetWindowDrawList();const auto& c=ui::palette();draw->AddRectFilled(pos,{pos.x+extent.x,pos.y+extent.y},ui::ink(selected?c.selected:c.surface),ui::p(10));draw->AddRect(pos,{pos.x+extent.x,pos.y+extent.y},ui::ink(selected || ImGui::IsItemFocused()?c.accent:c.border),ui::p(10));
    draw->AddText({pos.x+ui::p(12),pos.y+ui::p(9)},ui::ink(selected?c.accent:c.text),title);if(subtitle){ImGui::PushFont(nullptr,10);draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{pos.x+ui::p(12),pos.y+ui::p(31)},ui::ink(c.muted),subtitle);ImGui::PopFont();}ImGui::PopID();
}
void toggleRow(const char* id,const char* title,const char* detail,bool& value,int commandId) {
    auto start=ImGui::GetCursorPos();ImGui::BeginGroup();ui::text(title,13);if(detail)ui::muted(detail);ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetWindowWidth()-ui::p(52));ImGui::SetCursorPosY(start.y+ui::p(4));bool next=value;if(ui::toggle(id,next))post(commandId);ImGui::SetCursorPosY(start.y+ui::p(50));
}
void drawSettings() {
    auxHeader(settingsWindow,"设置","让浮光符合你的习惯");ImGui::BeginChild("##settingsBody",{0,0},ImGuiChildFlags_None);
    ui::section("外观主题");float third=(ImGui::GetContentRegionAvail().x-ui::p(16))/3;const char* themes[]={"晨光","夜色","森林"};const char* subtitles[]={"清爽浅色","柔和深色","自然绿色"};
    for(int i=0;i<3;++i){if(i)ImGui::SameLine();choiceTile(themes[i],themes[i],subtitles[i],state.theme==i,{third,ui::p(58)},ThemeFirstId+i);}
    ImGui::Spacing();ui::section("收起模式","点击标题栏的收起按钮时使用");float half=(ImGui::GetContentRegionAvail().x-ui::p(8))/2;
    const char* modes[]={"胶囊","屏幕顶部长条","完全隐藏","任务栏"};const char* notes[]={"可拖动的小窗","贴近屏幕上沿","保留系统托盘入口","左侧显示基本信息"};
    for(int i=0;i<4;++i){if(i%2)ImGui::SameLine();choiceTile(modes[i],modes[i],notes[i],static_cast<int>(state.collapseMode)==i,{half,ui::p(55)},700+i);}
    ImGui::Spacing();ui::section("窗口行为");toggleRow("##automatic","前台自动隐藏","游戏和指定软件处于前台时隐藏",state.automatic,MenuAuto);toggleRow("##topmost","保持窗口置顶","让待办与收起窗口保持可见",state.topmost,MenuTop);
    ui::section("需要检测的软件","修改后自动保存，按进程文件名称匹配");ImGui::BeginChild("##rules",ui::size(0,204),ImGuiChildFlags_Borders);
    for(size_t i=0;i<state.rules.size();++i) {
        auto& rule=state.rules[i];ImGui::PushID(static_cast<int>(i));auto pos=ImGui::GetCursorPos();bool enabled=rule.enabled;
        if(ui::toggle("##rule",enabled)){rule.enabled=enabled;save();evaluateForeground();}ImGui::SameLine();ImGui::BeginGroup();auto name=str(rule.label),exe=str(rule.exe);ui::text(name.c_str(),11);ui::muted(exe.c_str());ImGui::EndGroup();
        ImGui::SameLine(ImGui::GetWindowWidth()-ui::p(39));ImGui::SetCursorPosY(pos.y+ui::p(5));if(ui::iconButton("##delete",ui::Icon::Close,"移除软件",false,23))post(800+static_cast<int>(i));
        ImGui::SetCursorPosY(pos.y+ui::p(49));ImGui::Separator();ImGui::PopID();
    }
    ImGui::EndChild();ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ui::p(65));bool add=ImGui::InputTextWithHint("##ruleInput","输入软件.exe",&ruleInput,ImGuiInputTextFlags_EnterReturnsTrue);ImGui::SameLine();if(ui::button("添加",ui::size(57,32),true) || add)addRule(wide(ruleInput));
    if(ui::button("从程序文件选择…",{ImGui::GetContentRegionAvail().x,ui::p(31)})){post(BrowseId);}
    errorText(settingsError);
    ImGui::Spacing();ui::section("应用更新");auto update=updateStatus();auto status=update.phase==UpdatePhase::Idle?std::string("当前版本 ")+AppVersion:str(update.message);ImGui::TextWrapped("%s",status.c_str());
    ImGui::BeginDisabled(update.phase==UpdatePhase::Checking || update.phase==UpdatePhase::Downloading);const char* action=update.phase==UpdatePhase::Available?"下载新版本":update.phase==UpdatePhase::Ready?"安装并重启":"检查更新";
    if(ui::button(action,{ImGui::GetContentRegionAvail().x,ui::p(33)},true)) {
        if(update.phase==UpdatePhase::Available)downloadUpdate();else if(update.phase==UpdatePhase::Ready){installConfirmation=true;ImGui::OpenPopup("安装更新");}else checkUpdate();
    }
    ImGui::EndDisabled();ui::muted("Glimmer Todo · GPL-3.0-only");ImGui::EndChild();
    if(installConfirmation && !ImGui::IsPopupOpen("安装更新"))ImGui::OpenPopup("安装更新");
    if(ImGui::BeginPopupModal("安装更新",&installConfirmation,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("更新已通过 SHA256 校验。");ImGui::TextUnformatted("现在保存数据并安装、重启程序？");
        if(ui::button("安装更新",ui::size(120,33),true)){post(710);installConfirmation=false;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ui::button("稍后安装",ui::size(120,33))){installConfirmation=false;ImGui::CloseCurrentPopup();}ImGui::EndPopup();
    }
}
void drawCalendar() {
    using namespace std::chrono;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ui::size(4,4));float width=ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(width*0.4f);ImGui::InputInt("##year",&editorYear,0,0);ImGui::SameLine();ImGui::SetNextItemWidth(width*0.26f);ImGui::InputInt("##month",&editorMonth,0,0);ImGui::SameLine();ImGui::SetNextItemWidth(width*0.26f);ImGui::InputInt("##day",&editorDay,0,0);
    if(editorYear>=1601 && editorYear<=9999 && editorMonth>=1 && editorMonth<=12) {
        year_month_day first{year{editorYear},month{static_cast<unsigned>(editorMonth)},day{1}};int offset=static_cast<int>(weekday{sys_days{first}}.iso_encoding())-1;int days=static_cast<int>(static_cast<unsigned>(year_month_day_last{first.year(),month_day_last{first.month()}}.day()));
        const char* names[]={"周一","周二","周三","周四","周五","周六","周日"};float cell=(width-ui::p(24))/7;
        for(int i=0;i<7;++i){if(i)ImGui::SameLine();ImGui::SetNextItemWidth(cell);ImGui::BeginGroup();ImGui::Dummy({cell,0});ui::muted(names[i]);ImGui::EndGroup();}
        for(int index=0;index<42;++index) {
            if(index%7){ImGui::SameLine();}
            int date=index-offset+1;if(date<1 || date>days){ImGui::Dummy({cell,ui::p(25)});continue;}
            ImGui::PushID(index);auto name=std::to_string(date);if(ui::button(name.c_str(),{cell,ui::p(25)},editorDay==date))editorDay=date;ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();
}
void drawScheduleEditor() {
    auxHeader(scheduleWindow,scheduleEditingId?"编辑日程":"新增日程","为下一件重要的事留出时间");ImGui::BeginChild("##editorBody",{0,0},ImGuiChildFlags_None);
    ui::muted("日程内容");ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##description","计划做些什么？",&editorText);ui::muted("本地日期 · 年 / 月 / 日");drawCalendar();ImGui::Spacing();
    const char* labels[]={"小时","分钟"};int* values[]={&editorHour,&editorMinute};numberFields("time",labels,values,2);
    ui::muted("重复安排");ImGui::SetNextItemWidth(-1);const char* repeats[]={"不重复","每天","每周"};ImGui::Combo("##repeat",&editorRepeat,repeats,3);
    errorText(scheduleError);if(ui::button("保存日程",{ImGui::GetContentRegionAvail().x,ui::p(35)},true))post(ScheduleSaveId);ui::muted("游戏前台时延后提醒");ImGui::EndChild();
}
void drawFiles() {
    auxHeader(fileDialog,"选择程序文件","选择软件实际运行的 EXE 文件");
    if(filePicker && filePicker->Display("SelectExecutable",ImGuiWindowFlags_NoSavedSettings,ImGui::GetContentRegionAvail(),ImGui::GetContentRegionAvail())) {
        if(filePicker->IsOk()){addRule(wide(filePicker->GetFilePathName()));}
        filePicker->Close();PostMessageW(fileDialog,WM_CLOSE,0,0);
    }
}
void renderWindows() {
    for(size_t i=0;i<surfaces.size();++i)if(surfaces[i] && IsWindowVisible(surfaces[i]->window()) && !IsIconic(surfaces[i]->window())) {
        auto& view=*surfaces[i];bool visible=view.begin(state.theme);
        if(visible){switch(static_cast<WindowKind>(i)){case WindowKind::Main:drawMain();break;case WindowKind::Settings:drawSettings();break;case WindowKind::Schedule:drawScheduleEditor();break;case WindowKind::Files:drawFiles();break;}}
        view.end();
    }
}

std::wstring foregroundExe(HWND window) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(!pid || pid==GetCurrentProcessId())return {};
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return {};
    wchar_t path[32768]{};DWORD length=static_cast<DWORD>(std::size(path));BOOL queried=QueryFullProcessImageNameW(process,0,path,&length);CloseHandle(process);
    return queried?normalizeExe(std::wstring(path,length)):std::wstring{};
}
void applyVisibility() {
    if(!ready || applyingVisibility){return;}
    applyingVisibility=true;
    bool compact=manuallyHidden && !automaticallyHidden && (state.collapseMode==CollapseMode::Capsule || state.collapseMode==CollapseMode::TopBar);
    bool taskbar=manuallyHidden && !automaticallyHidden && state.collapseMode==CollapseMode::Taskbar;
    if(manuallyHidden || automaticallyHidden){for(auto window:{fileDialog,scheduleWindow,settingsWindow,mainWindow})if(window && IsWindowVisible(window))ShowWindow(window,SW_HIDE);}
    else if(followCurrentDesktop({mainWindow,settingsWindow,scheduleWindow,fileDialog})) {
        for(auto window:{mainWindow,settingsWindow,scheduleWindow,fileDialog})if(window && (!IsWindowVisible(window) || IsIconic(window)))ShowWindow(window,SW_SHOWNOACTIVATE);
    }
    if(compact){positionCompact();if(followCurrentDesktop({compactWindow}) && !IsWindowVisible(compactWindow))ShowWindow(compactWindow,SW_SHOWNOACTIVATE);}
    else if(compactWindow && IsWindowVisible(compactWindow)){compactTooltip.hide();ShowWindow(compactWindow,SW_HIDE);}
    syncTaskbarPanel(taskbar);if(taskbar && taskbarPanelWindow() && !followCurrentDesktop({taskbarPanelWindow()}))syncTaskbarPanel(false);
    auto info=state.compactInfo(nowSeconds());if(compactDetails!=info.details){compactDetails=info.details;SetWindowTextW(compactWindow,compactDetails.c_str());InvalidateRect(compactWindow,nullptr,FALSE);}
    if(compact){RECT rect{};GetWindowRect(compactWindow,&rect);compactTooltip.update(compactWindow,compactDetails,rect,state.theme);}
    updateTray();bool next=IsWindowVisible(mainWindow) || (settingsWindow && IsWindowVisible(settingsWindow)) || (scheduleWindow && IsWindowVisible(scheduleWindow)) || (fileDialog && IsWindowVisible(fileDialog));
    if(next!=rendering){rendering=next;if(next)require(SetTimer(mainWindow,2,16,nullptr)!=0,L"无法启动界面绘制。");else KillTimer(mainWindow,2);}
    applyingVisibility=false;
}
void evaluateForeground() {
    if(!ready){return;}
    HWND foreground=GetForegroundWindow();DWORD cloaked=0;if(!state.automatic)automaticallyHidden=false;
    if(foreground && isOnCurrentDesktop(foreground) && !(SUCCEEDED(DwmGetWindowAttribute(foreground,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked))automaticallyHidden=state.matches(foregroundExe(foreground));
    applyVisibility();
}
void CALLBACK foregroundEvent(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){PostMessageW(mainWindow,ForegroundMessage,0,0);}
void CALLBACK desktopEvent(HWINEVENTHOOK,DWORD,HWND window,LONG,LONG,DWORD,DWORD){if(window==mainWindow || window==compactWindow || window==taskbarPanelWindow())PostMessageW(mainWindow,ForegroundMessage,0,0);}
void menu(HWND target) {
    POINT point{};GetCursorPos(&point);std::vector<chrome::MenuItem> items={{MenuShow,manuallyHidden?L"展开完整窗口":L"收起窗口"},{0,L"收起模式"}};
    for(int i=0;i<4;++i)items.push_back({CollapseFirstId+i,CollapseNames[i],static_cast<int>(state.collapseMode)==i});
    items.push_back({MenuAuto,L"前台自动隐藏",state.automatic});items.push_back({MenuTop,L"保持窗口置顶",state.topmost});items.push_back({MenuSettings,L"设置"});items.push_back({MenuClear,L"清理已完成待办"});items.push_back({MenuExit,L"退出浮光待办"});chrome::menu(target,point,items,state.theme);
}
void compactFonts() {
    for(auto font:{compactFont,compactSmallFont})if(font)DeleteObject(font);
    compactFont=CreateFontW(-cp(12),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    compactSmallFont=CreateFontW(-cp(11),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");require(compactFont && compactSmallFont,L"无法创建收起界面字体。");
}
void positionCompact() {
    if(!compactWindow){return;}
    bool bar=state.collapseMode==CollapseMode::TopBar;RECT desired{state.capsuleX,state.capsuleY,state.capsuleX+1,state.capsuleY+1};
    HMONITOR monitor=bar || (state.capsuleX==-1 && state.capsuleY==-1)?MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST):MonitorFromRect(&desired,MONITOR_DEFAULTTONEAREST);MONITORINFO info{sizeof(info)};require(GetMonitorInfoW(monitor,&info),L"无法获取收起窗口的屏幕工作区。");
    int width=(std::min)(cp(bar?720:304),static_cast<int>(info.rcWork.right-info.rcWork.left)-cp(16)),height=cp(bar && width>=cp(480)?46:66);
    int x=bar?info.rcWork.left+(info.rcWork.right-info.rcWork.left-width)/2:state.capsuleX,y=bar?info.rcWork.top+cp(6):state.capsuleY;
    if(!bar && state.capsuleX==-1 && state.capsuleY==-1){RECT main{};GetWindowRect(mainWindow,&main);x=main.left;y=main.top;}
    x=std::clamp(x,static_cast<int>(info.rcWork.left),(std::max)(static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-width));y=std::clamp(y,static_cast<int>(info.rcWork.top),(std::max)(static_cast<int>(info.rcWork.top),static_cast<int>(info.rcWork.bottom)-height));
    SetWindowPos(compactWindow,state.topmost?HWND_TOPMOST:HWND_NOTOPMOST,x,y,width,height,SWP_NOACTIVATE);if(!bar){state.capsuleX=x;state.capsuleY=y;}
}
void drawCompact(HWND window,HDC dc) {
    RECT rect{};GetClientRect(window,&rect);auto c=themePalette(state.theme);fill(dc,rect,c.surface);auto info=state.compactInfo(nowSeconds());
    if(state.collapseMode==CollapseMode::TopBar && rect.right>=cp(480)) {
        label(dc,info.tasks,{cp(16),0,cp(114),rect.bottom},compactFont,c.text);label(dc,info.timer,{cp(120),0,cp(258),rect.bottom},compactFont,state.pomodoro.running?c.accent:c.muted);label(dc,info.schedule,{cp(268),0,rect.right-cp(94),rect.bottom},compactSmallFont,c.muted);
    } else {
        label(dc,info.tasks,{cp(16),cp(9),cp(102),cp(30)},compactFont,c.text);label(dc,info.timer,{cp(106),cp(9),rect.right-cp(88),cp(30)},compactSmallFont,state.pomodoro.running?c.accent:c.muted);label(dc,info.schedule,{cp(16),cp(34),rect.right-cp(16),cp(55)},compactSmallFont,c.muted);
    }
    label(dc,L"展开",{rect.right-cp(80),cp(8),rect.right-cp(35),cp(36)},compactSmallFont,c.accent,DT_CENTER|DT_VCENTER|DT_SINGLELINE);label(dc,L"···",{rect.right-cp(34),cp(8),rect.right-cp(8),cp(36)},compactFont,c.muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
LRESULT CALLBACK compactProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_CREATE:compactWindow=window;compactDpi=static_cast<int>(GetDpiForWindow(window));compactFonts();return 0;
    case WM_LBUTTONDOWN:{RECT rect{};GetClientRect(window,&rect);if(GET_X_LPARAM(lParam)<rect.right-cp(85) && state.collapseMode==CollapseMode::Capsule){POINT point{};GetCursorPos(&point);SendMessageW(window,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(point.x,point.y));}return 0;}
    case WM_LBUTTONUP:{RECT rect{};GetClientRect(window,&rect);if(GET_X_LPARAM(lParam)>rect.right-cp(35))menu(mainWindow);else if(GET_X_LPARAM(lParam)>rect.right-cp(85))PostMessageW(mainWindow,ShowRequest,0,0);return 0;}
    case WM_LBUTTONDBLCLK:case WM_NCLBUTTONDBLCLK:PostMessageW(mainWindow,ShowRequest,0,0);return 0;
    case WM_CONTEXTMENU:menu(mainWindow);return 0;
    case WM_EXITSIZEMOVE:{RECT rect{};GetWindowRect(window,&rect);state.capsuleX=rect.left;state.capsuleY=rect.top;positionCompact();save();return 0;}
    case WM_DPICHANGED:compactDpi=HIWORD(wParam);compactFonts();positionCompact();return 0;
    case WM_SIZE:{RECT rect{};GetClientRect(window,&rect);HRGN region=CreateRoundRectRgn(0,0,rect.right+1,rect.bottom+1,cp(24),cp(24));require(region && SetWindowRgn(window,region,TRUE),L"无法设置收起界面形状。");return 0;}
    case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);drawCompact(window,dc);EndPaint(window,&paint);return 0;}
    case WM_PRINTCLIENT:drawCompact(window,reinterpret_cast<HDC>(wParam));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_CLOSE:PostMessageW(mainWindow,ShowRequest,0,0);return 0;
    case WM_DESTROY:compactTooltip.destroy();for(auto font:{compactFont,compactSmallFont})if(font)DeleteObject(font);compactFont=compactSmallFont=nullptr;compactWindow=nullptr;return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
HWND createWindow(WindowKind kind,const wchar_t* className,const wchar_t* title,int width,int height) {
    int windowDpi=mainWindow?static_cast<int>(GetDpiForWindow(mainWindow)):static_cast<int>(GetDpiForSystem());auto scale=[&](int value){return MulDiv(value,windowDpi,96);};
    RECT owner{};if(mainWindow)GetWindowRect(mainWindow,&owner);else owner={state.x,state.y,state.x+scale(state.width),state.y+scale(state.height)};
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromRect(&owner,MONITOR_DEFAULTTONEAREST),&info);int w=scale(width),h=scale(height);
    int x=kind==WindowKind::Main?state.x:owner.left+scale(22),y=kind==WindowKind::Main?state.y:owner.top+scale(18);
    if(kind==WindowKind::Main && x==-1 && y==-1){x=info.rcWork.right-w-scale(24);y=info.rcWork.top+scale(50);}
    x=std::clamp(x,static_cast<int>(info.rcWork.left),(std::max)(static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-w));y=std::clamp(y,static_cast<int>(info.rcWork.top),(std::max)(static_cast<int>(info.rcWork.top),static_cast<int>(info.rcWork.bottom)-h));
    DWORD ex=kind==WindowKind::Main?WS_EX_APPWINDOW|(state.topmost?WS_EX_TOPMOST:0):WS_EX_TOOLWINDOW;
    HWND window=CreateWindowExW(ex,className,title,WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,x,y,w,h,kind==WindowKind::Main?nullptr:mainWindow,nullptr,instance,reinterpret_cast<void*>(static_cast<INT_PTR>(kind)+1));
    require(window!=nullptr,L"无法打开浮光界面。");int corner=2;DwmSetWindowAttribute(window,33,&corner,sizeof(corner));return window;
}
void openSettings() {
    if(manuallyHidden){manuallyHidden=false;applyVisibility();}
    if(!settingsWindow)createWindow(WindowKind::Settings,L"FloatingTodoSettings",L"浮光待办 · 设置",420,620);
    ShowWindow(settingsWindow,SW_SHOW);SetForegroundWindow(settingsWindow);applyVisibility();
}
void openSchedule(uint64_t id) {
    if(scheduleWindow){ShowWindow(scheduleWindow,SW_SHOW);SetForegroundWindow(scheduleWindow);return;}
    scheduleEditingId=id;editorText.clear();scheduleError.clear();int64_t due=nowSeconds()+3600;editorRepeat=0;
    if(id)for(const auto& event:state.schedules)if(event.id==id){due=event.due;editorText=str(event.text);editorRepeat=event.repeat;}
    auto time=localTime(due);editorYear=time.wYear;editorMonth=time.wMonth;editorDay=time.wDay;editorHour=time.wHour;editorMinute=time.wMinute;
    createWindow(WindowKind::Schedule,L"FloatingTodoSchedule",id?L"浮光待办 · 编辑日程":L"浮光待办 · 新增日程",360,620);ShowWindow(scheduleWindow,SW_SHOW);SetForegroundWindow(scheduleWindow);applyVisibility();
}
void openFiles() {
    if(fileDialog){ShowWindow(fileDialog,SW_SHOW);SetForegroundWindow(fileDialog);return;}
    createWindow(WindowKind::Files,L"FloatingTodoFiles",L"浮光待办 · 选择程序",720,500);surface(fileDialog)->select();filePicker=std::make_unique<IGFD::FileDialog>();
    IGFD::FileDialogConfig config;PWSTR path=nullptr;require(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&path)==S_OK,L"无法获取程序目录。");config.path=str(path);CoTaskMemFree(path);
    config.flags=ImGuiFileDialogFlags_NoDialog|ImGuiFileDialogFlags_DisableCreateDirectoryButton|ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering|ImGuiFileDialogFlags_ShowDevicesButton|ImGuiFileDialogFlags_HideColumnType;
    filePicker->OpenDialog("SelectExecutable","选择程序文件","可执行程序{.exe}",config);ShowWindow(fileDialog,SW_SHOW);SetForegroundWindow(fileDialog);applyVisibility();
}
void command(int id) {
    if(id>=ViewFirstId && id<ViewFirstId+3){selectView(id-ViewFirstId);return;}
    if(id>=ThemeFirstId && id<ThemeFirstId+3){state.theme=id-ThemeFirstId;save();InvalidateRect(compactWindow,nullptr,FALSE);return;}
    if(id>=CollapseFirstId && id<CollapseFirstId+4){state.collapseMode=static_cast<CollapseMode>(id-CollapseFirstId);manuallyHidden=true;save();evaluateForeground();return;}
    if(id>=700 && id<704){state.collapseMode=static_cast<CollapseMode>(id-700);save();return;}
    if(id>=800 && id<800+static_cast<int>(state.rules.size())){state.rules.erase(state.rules.begin()+id-800);save();evaluateForeground();return;}
    switch(id) {
    case AddId:submitTask();break;
    case SettingsId:case MenuSettings:openSettings();break;
    case ScheduleAddId:openSchedule(pendingScheduleId);pendingScheduleId=0;break;
    case ScheduleSaveId:if(saveSchedule() && scheduleWindow)PostMessageW(scheduleWindow,WM_CLOSE,0,0);break;
    case BrowseId:openFiles();break;
    case HideId:case MenuShow:manuallyHidden=!manuallyHidden;evaluateForeground();break;
    case PomoStartId:state.pomodoro.toggle(nowSeconds());save();break;
    case PomoResetId:state.pomodoro.reset();save();break;
    case PomoSkipId:state.pomodoro.skip();save();break;
    case PomoConfigId:if(state.pomodoro.configure(focusMinutes,shortMinutes,longMinutes)){taskError.clear();save();}else taskError="专注为 1–180 分钟，短休息为 1–60 分钟，长休息为 1–120 分钟。";break;
    case MenuAuto:state.automatic=!state.automatic;save();evaluateForeground();break;
    case MenuTop:state.topmost=!state.topmost;SetWindowPos(mainWindow,state.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);save();break;
    case MenuClear:std::erase_if(state.tasks,[](const Task& task){return task.done;});cancelEdit();save();break;
    case 600:if(selectedTaskId)removeTask(selectedTaskId);break;
    case 601:std::erase_if(state.schedules,[](const Schedule& event){return event.id==selectedScheduleId;});selectedScheduleId=0;save();break;
    case 602:state.completeSchedule(selectedScheduleId,nowSeconds());save();break;
    case 710:save();installUpdate();DestroyWindow(mainWindow);break;
    case MenuExit:DestroyWindow(mainWindow);break;
    }
}
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto stored=GetWindowLongPtrW(window,GWLP_USERDATA);auto kind=stored?static_cast<WindowKind>(stored-1):WindowKind::Main;
    if(message==WM_NCCREATE){auto create=reinterpret_cast<CREATESTRUCTW*>(lParam);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));kind=static_cast<WindowKind>(reinterpret_cast<INT_PTR>(create->lpCreateParams)-1);}
    if(ui::Surface::message(window,message,wParam,lParam))return 1;
    if(message==taskbarCreated && taskbarCreated){reconnectVirtualDesktops();addTray();evaluateForeground();return 0;}
    switch(message) {
    case WM_CREATE:
        if(kind==WindowKind::Main){mainWindow=window;}
        if(kind==WindowKind::Settings){settingsWindow=window;}
        if(kind==WindowKind::Schedule){scheduleWindow=window;}
        if(kind==WindowKind::Files){fileDialog=window;}
        surfaces[static_cast<size_t>(kind)]=std::make_unique<ui::Surface>(window);return 0;
    case WM_NCCALCSIZE:return 0;
    case WM_NCHITTEST:{RECT rect{};GetWindowRect(window,&rect);int localDpi=static_cast<int>(GetDpiForWindow(window));int edge=MulDiv(5,localDpi,96),x=GET_X_LPARAM(lParam)-rect.left,y=GET_Y_LPARAM(lParam)-rect.top,width=rect.right-rect.left,height=rect.bottom-rect.top;
        if(y<edge){return x<edge?HTTOPLEFT:x>=width-edge?HTTOPRIGHT:HTTOP;}
        if(y>=height-edge){return x<edge?HTBOTTOMLEFT:x>=width-edge?HTBOTTOMRIGHT:HTBOTTOM;}
        if(x<edge){return HTLEFT;}
        if(x>=width-edge){return HTRIGHT;}
        if(y<MulDiv(48,localDpi,96) && x<width-MulDiv(kind==WindowKind::Main?116:50,localDpi,96)){return HTCAPTION;}
        return HTCLIENT;}
    case WM_SIZE:if(wParam!=SIZE_MINIMIZED && surface(window))surface(window)->resize(LOWORD(lParam),HIWORD(lParam));return 0;
    case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lParam);int localDpi=static_cast<int>(GetDpiForWindow(window));int width=kind==WindowKind::Main?280:kind==WindowKind::Files?560:340,height=kind==WindowKind::Main?280:kind==WindowKind::Settings?400:kind==WindowKind::Files?360:440;info->ptMinTrackSize={MulDiv(width,localDpi,96),MulDiv(height,localDpi,96)};return 0;}
    case WM_DPICHANGED:{auto rect=reinterpret_cast<RECT*>(lParam);if(kind==WindowKind::Main)dpi=HIWORD(wParam);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
    case WM_EXITSIZEMOVE:if(kind==WindowKind::Main && !IsIconic(window)){RECT rect{};GetWindowRect(window,&rect);dpi=static_cast<int>(GetDpiForWindow(window));state.x=rect.left;state.y=rect.top;state.width=dip(rect.right-rect.left);state.height=dip(rect.bottom-rect.top);save();}return 0;
    case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(window,&paint);EndPaint(window,&paint);return 0;}
    case WM_PRINTCLIENT:if(surface(window))surface(window)->print(reinterpret_cast<HDC>(wParam));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SYSCOMMAND:
        if((wParam&0xFFF0)==SC_KEYMENU || (wParam&0xFFF0)==SC_MOUSEMENU){menu(mainWindow);return 0;}
        if(kind==WindowKind::Main && (wParam&0xFFF0)==SC_MINIMIZE){manuallyHidden=true;evaluateForeground();return 0;}
        if(kind==WindowKind::Main && (wParam&0xFFF0)==SC_RESTORE){manuallyHidden=false;evaluateForeground();return 0;}break;
    case WM_COMMAND:command(LOWORD(wParam));return 0;
    case WM_CONTEXTMENU:menu(mainWindow);return 0;
    case WM_TIMER:if(wParam==2)renderWindows();else{evaluateForeground();pomodoroTick();scheduleNotices();}return 0;
    case ForegroundMessage:evaluateForeground();return 0;
    case ShowRequest:manuallyHidden=false;evaluateForeground();return 0;
    case TaskbarMenuRequest:menu(mainWindow);return 0;
    case TrayMessage:if(lParam==WM_LBUTTONDBLCLK){manuallyHidden=false;evaluateForeground();}if(lParam==WM_RBUTTONUP)menu(mainWindow);return 0;
    case WM_QUERYENDSESSION:save();return TRUE;
    case WM_DISPLAYCHANGE:case WM_SETTINGCHANGE:positionCompact();return 0;
    case WM_CLOSE:if(kind==WindowKind::Main){manuallyHidden=true;evaluateForeground();}else DestroyWindow(window);return 0;
    case WM_DESTROY:
        if(kind==WindowKind::Main){ready=false;chrome::closeMenu();for(auto other:{fileDialog,scheduleWindow,settingsWindow})if(other)DestroyWindow(other);destroyTaskbarPanel();if(compactWindow)DestroyWindow(compactWindow);KillTimer(window,1);KillTimer(window,2);if(foregroundHook)UnhookWinEvent(foregroundHook);if(desktopHook)UnhookWinEvent(desktopHook);Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);}
        if(kind==WindowKind::Settings){settingsWindow=nullptr;}
        if(kind==WindowKind::Schedule){scheduleWindow=nullptr;}
        if(kind==WindowKind::Files){filePicker.reset();fileDialog=nullptr;}
        surfaces[static_cast<size_t>(kind)].reset();return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
HICON createIcon() {
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,32,32),mask=CreateBitmap(32,32,1,1,nullptr);auto old=SelectObject(dc,bitmap);fill(dc,{0,0,32,32},themePalette(0).accent);
    auto pen=CreatePen(PS_SOLID,3,RGB(255,255,255));auto previous=SelectObject(dc,pen);MoveToEx(dc,8,16,nullptr);LineTo(dc,14,22);LineTo(dc,25,10);SelectObject(dc,previous);DeleteObject(pen);SelectObject(dc,old);old=SelectObject(dc,mask);PatBlt(dc,0,0,32,32,BLACKNESS);SelectObject(dc,old);
    ICONINFO info{TRUE,0,0,mask,bitmap};HICON icon=CreateIconIndirect(&info);DeleteObject(bitmap);DeleteObject(mask);DeleteDC(dc);ReleaseDC(nullptr,screen);return icon;
}
void registerWindows() {
    WNDCLASSEXW klass{sizeof(klass)};klass.hInstance=instance;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=appIcon;klass.hIconSm=appIcon;klass.lpfnWndProc=windowProc;
    for(auto name:{L"FloatingTodoMain",L"FloatingTodoSettings",L"FloatingTodoSchedule",L"FloatingTodoFiles"}){klass.lpszClassName=name;require(RegisterClassExW(&klass),L"无法注册浮光界面。");}
    klass.lpszClassName=L"FloatingTodoCompact";klass.lpfnWndProc=compactProc;klass.style=CS_DBLCLKS;require(RegisterClassExW(&klass),L"无法注册收起界面。");
}
void initializeWindows() {
    chrome::initialize(instance);ui::initialize();appIcon=createIcon();registerWindows();createWindow(WindowKind::Main,L"FloatingTodoMain",L"浮光待办",state.width,state.height);dpi=static_cast<int>(GetDpiForWindow(mainWindow));
    initializeTaskbarPanel(instance,mainWindow,state,ShowRequest,TaskbarMenuRequest);RECT rect{};GetWindowRect(mainWindow,&rect);
    require(CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"FloatingTodoCompact",L"浮光待办 · 收起",WS_POPUP,rect.left,rect.top,304,66,nullptr,nullptr,instance,nullptr)!=nullptr,L"无法创建收起界面。");
    focusMinutes=state.pomodoro.focusMinutes;shortMinutes=state.pomodoro.shortMinutes;longMinutes=state.pomodoro.longMinutes;
}
}

int WINAPI wWinMain(HINSTANCE app,HINSTANCE,LPWSTR,int) {
    instance=app;if(runUpdateHelper())return 0;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);std::set_terminate([]{fatal(L"数据或程序状态无效，请检查本地数据与安装目录权限。");});
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\FloatingTodo.DesktopList.v1");require(mutex!=nullptr,L"无法检查运行实例。");
    if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(L"FloatingTodoMain",nullptr);require(existing!=nullptr,L"已有实例正在启动。");PostMessageW(existing,ShowRequest,0,0);CloseHandle(mutex);return 0;}
    initializeVirtualDesktops();PWSTR local=nullptr;require(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)==S_OK,L"无法获取本地数据目录。");dataFile=std::filesystem::path(local)/L"DesktopList"/L"state.json";CoTaskMemFree(local);state.load(dataFile);
    initializeWindows();taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");addTray();foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foregroundEvent,0,0,WINEVENT_OUTOFCONTEXT);require(foregroundHook!=nullptr,L"无法启用前台软件检测。");
    desktopHook=SetWinEventHook(EVENT_OBJECT_CLOAKED,EVENT_OBJECT_UNCLOAKED,nullptr,desktopEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);require(desktopHook!=nullptr,L"无法启用虚拟桌面检测。");require(SetTimer(mainWindow,1,250,nullptr),L"无法启动前台状态检查。");ready=true;evaluateForeground();
    MSG message{};BOOL result;while((result=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}require(result!=-1,L"窗口消息处理失败。");stopUpdater();ui::shutdown();DestroyIcon(appIcon);shutdownVirtualDesktops();CloseHandle(mutex);return 0;
}
