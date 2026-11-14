#include "model.hpp"
#include "virtual_desktop.hpp"
#include "updater.hpp"
#include "version.hpp"
#include "theme.hpp"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <array>

namespace {
Palette colors = themePalette(0);
COLORREF &Paper=colors.background, &Surface=colors.surface, &Ink=colors.text, &Muted=colors.muted;
COLORREF &Green=colors.accent, &Soft=colors.selected, &Border=colors.border, &Track=colors.track, &OnAccent=colors.onAccent;
constexpr UINT TrayMessage = WM_APP + 1, ForegroundMessage = WM_APP + 2;
constexpr UINT ShowRequest = WM_APP + 3;
constexpr int AddId = 101, InputId = 102, ListId = 103, SettingsId = 104, HideId = 105;
constexpr int AutoId = 201, TopId = 202, RulesId = 203, RuleInputId = 204, RuleAddId = 205, BrowseId = 206, RemoveId = 207;
constexpr int UpdateId = 208, ThemeId = 209, ThemeChoiceId = 210;
constexpr int CollapseChoiceId = 211, CollapseFirstId = 320, ExpandId = 324, CompactMenuId = 325;
constexpr int ScheduleListId = 111, ScheduleAddId = 112, ScheduleSaveId = 113, ViewFirstId = 114;
constexpr int PomoStartId = 120, PomoResetId = 121, PomoSkipId = 122, PomoConfigId = 123;
constexpr int MenuShow = 301, MenuAuto = 302, MenuTop = 303, MenuSettings = 304, MenuExit = 305, MenuClear = 306;
HINSTANCE instance;
HWND mainWindow, taskList, input, addButton, settingsButton, hideButton, themeButton, themeChoice;
HWND settingsWindow, rulesList, ruleInput, automaticBox, topmostBox;
HWND compactWindow, compactExpand, compactMenu, compactTooltip, collapseChoice;
HFONT compactFont, compactSmallFont;
int compactDpi = 96;
bool applyingVisibility = false;
std::wstring compactDetails, lastTrayTip;
HWND fileDialog;
HWND updateButton, updateLabel;
HWND scheduleList, scheduleAdd, scheduleWindow, scheduleText, scheduleDate, scheduleTime, scheduleRepeat;
std::array<HWND,3> viewButtons{};
int activeView = 0;
ULONGLONG transitionStarted = 0;
double navigationPosition = 0, navigationFrom = 0;
int contentOffset = 0;
uint64_t scheduleEditingId = 0;
ULONGLONG nextNotice = 0;
HWND pomoPhase, pomoClock, pomoCount, pomoStart, pomoReset, pomoSkip, pomoFocus, pomoShort, pomoLong, pomoConfig;
std::vector<HWND> pomoControls;
HFONT bodyFont, smallFont, rowFont, doneFont, clockFont, titleFont;
HBRUSH paperBrush, whiteBrush;
HICON appIcon;
HWINEVENTHOOK foregroundHook;
HWINEVENTHOOK desktopHook;
UINT taskbarCreated;
NOTIFYICONDATAW tray{};
State state;
std::filesystem::path dataFile;
bool manuallyHidden = false, automaticallyHidden = false, ready = false, refreshingRules = false;
uint64_t editingId = 0;
int dpi = 96;

int px(int value) { return MulDiv(value, dpi, 96); }
int dip(int value) { return MulDiv(value, 96, dpi); }

std::wstring textOf(HWND window) {
    int length = GetWindowTextLengthW(window);
    std::wstring value(length + 1, L'\0');
    GetWindowTextW(window, value.data(), length + 1);
    value.resize(length);
    return value;
}

HFONT makeFont(int height, int weight = FW_NORMAL, bool strike = false) {
    auto font = CreateFontW(-px(height), 0, 0, 0, weight, FALSE, FALSE, strike, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    require(font != nullptr, L"无法创建界面字体。");
    return font;
}

void fonts() {
    for (auto font : {bodyFont, smallFont, rowFont, doneFont, clockFont, titleFont}) if (font) DeleteObject(font);
    bodyFont = makeFont(12); smallFont = makeFont(10);
    rowFont = makeFont(13); doneFont = makeFont(13, FW_NORMAL, true);
    clockFont = makeFont(34,FW_SEMIBOLD); titleFont = makeFont(15,FW_SEMIBOLD);
    for (auto window : {input, addButton, settingsButton, hideButton,themeButton}) if (window) SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
    for(auto window:viewButtons)if(window)SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE);
    if (taskList) { SendMessageW(taskList, WM_SETFONT, reinterpret_cast<WPARAM>(rowFont), TRUE); SendMessageW(taskList, LB_SETITEMHEIGHT, 0, px(40)); }
    if (scheduleList) SendMessageW(scheduleList,LB_SETITEMHEIGHT,0,px(58));
    for(auto window:pomoControls)SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(window==pomoClock?clockFont:bodyFont),TRUE);
}

void fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void label(HDC dc, const std::wstring& value, RECT rect, HFONT font, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    auto previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect, flags | DT_NOPREFIX);
    SelectObject(dc, previous);
}

void rounded(HDC dc, RECT rect, COLORREF background, int radius, COLORREF border) {
    auto brush = CreateSolidBrush(background);
    auto pen = CreatePen(PS_SOLID, 1, border);
    auto oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, px(radius), px(radius));
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(brush); DeleteObject(pen);
}

void checkMark(HDC dc, int x, int y, COLORREF color) {
    auto pen = CreatePen(PS_SOLID, px(2), color);
    auto old = SelectObject(dc, pen);
    MoveToEx(dc, x, y + px(5), nullptr); LineTo(dc, x + px(4), y + px(9)); LineTo(dc, x + px(11), y);
    SelectObject(dc, old); DeleteObject(pen);
}

void save() { state.save(dataFile); }

void updateTray();
void evaluateForeground();
void openSchedule(uint64_t id = 0);
void layout();
void refreshPomodoro();
void applyTheme();
void chooseTheme();
void applyVisibility();
void positionCompact();
void refreshCompact();
void command(int id);
void menu(HWND owner);

void notify(const std::wstring& title, const std::wstring& message) {
    NOTIFYICONDATAW info = tray; info.uFlags = NIF_INFO;
    lstrcpynW(info.szInfoTitle, title.c_str(), static_cast<int>(std::size(info.szInfoTitle)));
    lstrcpynW(info.szInfo, message.c_str(), static_cast<int>(std::size(info.szInfo)));
    info.dwInfoFlags = NIIF_INFO; Shell_NotifyIconW(NIM_MODIFY, &info);
    nextNotice = GetTickCount64() + 6000;
}

void refreshSchedules() {
    SendMessageW(scheduleList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(scheduleList, LB_RESETCONTENT, 0, 0);
    for (const auto& event : state.schedules) {
        auto text = formatDate(event.due) + L" " + event.text + (event.done ? L"（已完成）" : L"");
        auto index = SendMessageW(scheduleList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        require(index != LB_ERR && index != LB_ERRSPACE, L"无法显示日程。");
    }
    SendMessageW(scheduleList, WM_SETREDRAW, TRUE, 0); InvalidateRect(scheduleList, nullptr, TRUE);
}

void scheduleNotices() {
    if (automaticallyHidden || GetTickCount64() < nextNotice) return;
    for (auto& event : state.schedules) {
        if (event.done || event.notified || event.due > nowSeconds()) continue;
        notify(L"日程提醒", formatDate(event.due) + L"\n" + event.text);
        event.notified = true; save(); return;
    }
}

void selectView(int view) {
    if (view < 0 || view > 2) return;
    bool animate = ready && view != activeView && IsWindowVisible(mainWindow);
    BOOL motion = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0);
    animate = animate && motion;
    navigationFrom = navigationPosition;
    activeView = view;
    transitionStarted = animate ? GetTickCount64() : 0;
    contentOffset = animate ? px(7) : 0;
    if(animate)require(SetTimer(mainWindow,2,16,nullptr)!=0,L"无法启动页面动画。");
    else {KillTimer(mainWindow,2); navigationPosition=view;}
    for (auto window : {taskList, input, addButton}) ShowWindow(window, view == 0 ? SW_SHOWNA : SW_HIDE);
    for (auto window : {scheduleList, scheduleAdd}) ShowWindow(window, view == 1 ? SW_SHOWNA : SW_HIDE);
    for(auto window:pomoControls)ShowWindow(window,view==2?SW_SHOWNA:SW_HIDE);
    if(view==2) {
        RECT bounds{};GetWindowRect(mainWindow,&bounds);
        if(bounds.bottom-bounds.top<px(320))SetWindowPos(mainWindow,nullptr,0,0,bounds.right-bounds.left,px(320),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        refreshPomodoro();
    }
    layout();
    for(auto button:viewButtons)InvalidateRect(button,nullptr,TRUE);
}

void animateView() {
    double progress=transitionProgress(GetTickCount64()-transitionStarted);
    navigationPosition=navigationFrom+(activeView-navigationFrom)*progress;
    contentOffset=static_cast<int>(px(7)*(1.0-progress));
    if(progress>=1.0) {KillTimer(mainWindow,2);transitionStarted=0;contentOffset=0;}
    layout();
}

void refreshTasks(int selection = -1) {
    const int oldTop = static_cast<int>(SendMessageW(taskList, LB_GETTOPINDEX, 0, 0));
    SendMessageW(taskList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(taskList, LB_RESETCONTENT, 0, 0);
    for (const auto& task : state.tasks) {
        const auto accessibleText = (task.done ? L"已完成：" : L"待完成：") + task.text;
        auto index = SendMessageW(taskList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(accessibleText.c_str()));
        require(index != LB_ERR && index != LB_ERRSPACE, L"无法显示待办列表。");
    }
    if (selection >= 0 && !state.tasks.empty()) SendMessageW(taskList, LB_SETCURSEL, (std::min)(selection, static_cast<int>(state.tasks.size()) - 1), 0);
    SendMessageW(taskList, LB_SETTOPINDEX, oldTop, 0);
    SendMessageW(taskList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(taskList, nullptr, TRUE);
    InvalidateRect(mainWindow, nullptr, FALSE);
}

void cancelEdit() {
    editingId = 0; SetWindowTextW(input, L""); SetWindowTextW(addButton, L"＋");
    SendMessageW(input, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"添加待办…"));
}

void submitTask() {
    auto value = textOf(input);
    bool changed = editingId ? state.editTask(editingId, value) : state.addTask(value);
    if (!changed) { MessageBeep(MB_ICONINFORMATION); return; }
    save(); cancelEdit(); refreshTasks(static_cast<int>(state.tasks.size()) - 1); SetFocus(input);
    SendMessageW(taskList, LB_SETTOPINDEX, state.tasks.size() > 4 ? state.tasks.size() - 4 : 0, 0);
}

int selectedTask() { return static_cast<int>(SendMessageW(taskList, LB_GETCURSEL, 0, 0)); }

void editSelected() {
    int selected = selectedTask();
    if (selected < 0 || selected >= static_cast<int>(state.tasks.size())) return;
    editingId = state.tasks[selected].id;
    SetWindowTextW(input, state.tasks[selected].text.c_str()); SetWindowTextW(addButton, L"保存");
    SendMessageW(input, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"编辑待办，Esc 取消"));
    SetFocus(input); SendMessageW(input, EM_SETSEL, 0, -1);
}

void toggleSelected() {
    int index = selectedTask();
    if (index < 0 || index >= static_cast<int>(state.tasks.size())) return;
    state.toggleTask(state.tasks[index].id); save(); refreshTasks(index);
}

void deleteSelected() {
    int index = selectedTask();
    if (index < 0 || index >= static_cast<int>(state.tasks.size())) return;
    if (editingId == state.tasks[index].id) cancelEdit();
    state.removeTask(state.tasks[index].id); save(); refreshTasks(index);
}

LRESULT CALLBACK inputProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_GETDLGCODE && (wParam == VK_RETURN || wParam == VK_ESCAPE)) return DLGC_WANTALLKEYS;
    if (message == WM_KEYDOWN && wParam == VK_RETURN) { submitTask(); return 0; }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE) { cancelEdit(); return 0; }
    if (message == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE)) return 0;
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK listProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_GETDLGCODE && wParam == VK_RETURN) return DLGC_WANTALLKEYS;
    if ((message == WM_PAINT||message==WM_PRINTCLIENT||message==WM_PRINT) && (window == taskList ? state.tasks.empty() : state.schedules.empty())) {
        PAINTSTRUCT paint{}; auto dc = message==WM_PAINT?BeginPaint(window, &paint):reinterpret_cast<HDC>(wParam);
        RECT rect{}; GetClientRect(window, &rect); FillRect(dc, &rect, paperBrush);
        int center=rect.bottom/2;
        if(rect.bottom>px(100)) {
            int x=rect.right/2;rounded(dc,{x-px(20),center-px(48),x+px(20),center-px(8)},Soft,40,Soft);checkMark(dc,x-px(5),center-px(32),Green);
            label(dc,L"在下方添加，安排今天",{0,center+px(20),rect.right,center+px(42)},smallFont,Muted,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        }
        label(dc, window == taskList ? L"暂无待办" : L"暂无日程", {0,center-px(4),rect.right,center+px(20)}, bodyFont, Ink, DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        if(message==WM_PAINT)EndPaint(window, &paint);
        return 0;
    }
    if (window == scheduleList) {
        auto act = [&](int action) {
            int index = static_cast<int>(SendMessageW(window, LB_GETCURSEL, 0, 0));
            if (index < 0 || index >= static_cast<int>(state.schedules.size())) return;
            auto id = state.schedules[index].id;
            if (action == 0) state.completeSchedule(id, nowSeconds());
            if (action == 1) std::erase_if(state.schedules, [id](const Schedule& event) { return event.id == id; });
            if (action == 2) { openSchedule(id); return; }
            save(); refreshSchedules();
        };
        if (message == WM_KEYDOWN) {
            if (wParam == VK_SPACE) { act(0); return 0; }
            if (wParam == VK_DELETE) { act(1); return 0; }
            if (wParam == VK_F2 || wParam == VK_RETURN) { act(2); return 0; }
        }
        if (message == WM_LBUTTONDOWN) {
            auto hit = SendMessageW(window, LB_ITEMFROMPOINT, 0, lParam);
            if (!HIWORD(hit)) {
                RECT rect{}; GetClientRect(window, &rect); SendMessageW(window, LB_SETCURSEL, LOWORD(hit), 0); SetFocus(window);
                if (GET_X_LPARAM(lParam) < px(30)) { act(0); return 0; }
                if (GET_X_LPARAM(lParam) > rect.right - px(26)) { act(1); return 0; }
            }
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (message == WM_KEYDOWN) {
        if (wParam == VK_SPACE) { toggleSelected(); return 0; }
        if (wParam == VK_DELETE) { deleteSelected(); return 0; }
        if (wParam == VK_F2 || wParam == VK_RETURN) { editSelected(); return 0; }
    }
    if (message == WM_LBUTTONDOWN) {
        auto hit = SendMessageW(window, LB_ITEMFROMPOINT, 0, lParam);
        if (!HIWORD(hit)) {
            RECT rect{}; GetClientRect(window, &rect);
            SendMessageW(window, LB_SETCURSEL, LOWORD(hit), 0); SetFocus(window);
            if (GET_X_LPARAM(lParam) < px(30)) { toggleSelected(); return 0; }
            if (GET_X_LPARAM(lParam) > rect.right - px(26)) { deleteSelected(); return 0; }
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

HWND child(const wchar_t* type, const wchar_t* text, DWORD style, HWND parent, int id) {
    bool button = lstrcmpW(type,L"BUTTON")==0;
    if(button && (style&BS_TYPEMASK)!=BS_AUTOCHECKBOX)style=(style&~BS_TYPEMASK)|BS_OWNERDRAW;
    HWND window = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    require(window != nullptr, L"无法创建界面控件。");
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
    return window;
}

LRESULT CALLBACK buttonProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR) {
    if(message==WM_MOUSEMOVE && !GetPropW(window,L"Glimmer.Hover")) {
        SetPropW(window,L"Glimmer.Hover",reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);InvalidateRect(window,nullptr,FALSE);
    }
    if(message==WM_MOUSELEAVE) {RemovePropW(window,L"Glimmer.Hover");InvalidateRect(window,nullptr,FALSE);}
    if(message==WM_NCDESTROY)RemovePropW(window,L"Glimmer.Hover");
    if(message==WM_KEYDOWN && GetDlgCtrlID(window)>=ViewFirstId && GetDlgCtrlID(window)<ViewFirstId+3 && (wParam==VK_LEFT||wParam==VK_RIGHT)) {
        int view=(activeView+(wParam==VK_RIGHT?1:2))%3;selectView(view);SetFocus(viewButtons[view]);return 0;
    }
    if((message==WM_PAINT||message==WM_PRINTCLIENT||message==WM_PRINT) && (GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX) {
        PAINTSTRUCT paint{};HDC dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(wParam);RECT bounds{};GetClientRect(window,&bounds);fill(dc,bounds,Paper);
        bool checked=Button_GetCheck(window)==BST_CHECKED;int y=(bounds.bottom-px(16))/2;
        rounded(dc,{0,y,px(16),y+px(16)},checked?Green:Surface,4,checked?Green:Border);
        if(checked)checkMark(dc,px(3),y+px(3),OnAccent);
        label(dc,textOf(window),{px(24),0,bounds.right,bounds.bottom},bodyFont,Ink);
        if(GetFocus()==window){RECT focus{px(22),px(2),bounds.right-px(2),bounds.bottom-px(2)};DrawFocusRect(dc,&focus);}
        if(message==WM_PAINT)EndPaint(window,&paint);
        return 0;
    }
    return DefSubclassProc(window,message,wParam,lParam);
}

LRESULT CALLBACK headerProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR) {
    if(message==WM_PAINT||message==WM_PRINT||message==WM_PRINTCLIENT) {
        PAINTSTRUCT paint{};HDC dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(wParam);
        RECT bounds{};GetClientRect(window,&bounds);fill(dc,bounds,Track);
        for(int i=0;i<Header_GetItemCount(window);++i) {
            wchar_t text[128]{};HDITEMW item{};item.mask=HDI_TEXT;item.pszText=text;item.cchTextMax=128;Header_GetItem(window,i,&item);
            RECT rect{};Header_GetItemRect(window,i,&rect);rect.left+=px(8);label(dc,text,rect,smallFont,Muted);
            fill(dc,{rect.right-1,rect.top,rect.right,rect.bottom},Border);
        }
        if(message==WM_PAINT)EndPaint(window,&paint);
        return 0;
    }
    return DefSubclassProc(window,message,wParam,lParam);
}

void themeWindow(HWND window) {
    if(!window)return;
    BOOL dark=state.theme==1;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
    DwmSetWindowAttribute(window,34,&Border,sizeof(Border));
    DwmSetWindowAttribute(window,35,&Paper,sizeof(Paper));DwmSetWindowAttribute(window,36,&Ink,sizeof(Ink));
    EnumChildWindows(window,[](HWND control,LPARAM)->BOOL {
        wchar_t name[64]{};GetClassNameW(control,name,64);
        if(lstrcmpiW(name,L"BUTTON")==0)SetWindowSubclass(control,buttonProc,10,0);
        if(lstrcmpiW(name,L"EDIT")==0||lstrcmpiW(name,L"COMBOBOX")==0)SetWindowTheme(control,L"",L"");
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(control==pomoClock?clockFont:bodyFont),FALSE);
        return TRUE;
    },0);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}

void applyTheme() {
    colors=themePalette(state.theme);
    if(paperBrush)DeleteObject(paperBrush);
    if(whiteBrush)DeleteObject(whiteBrush);
    paperBrush=CreateSolidBrush(Paper);whiteBrush=CreateSolidBrush(Surface);
    if(rulesList) {
        SetWindowTheme(rulesList,L"",L"");ListView_SetBkColor(rulesList,Surface);ListView_SetTextBkColor(rulesList,Surface);ListView_SetTextColor(rulesList,Ink);
        SetWindowSubclass(ListView_GetHeader(rulesList),headerProc,11,0);
    }
    for(auto window:{mainWindow,settingsWindow,scheduleWindow})themeWindow(window);
    if(compactWindow)RedrawWindow(compactWindow,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    if(themeChoice)SendMessageW(themeChoice,CB_SETCURSEL,state.theme,0);
    for(auto date:{scheduleDate,scheduleTime})if(IsWindow(date)) {
        DateTime_SetMonthCalColor(date,MCSC_BACKGROUND,Surface);DateTime_SetMonthCalColor(date,MCSC_MONTHBK,Surface);DateTime_SetMonthCalColor(date,MCSC_TEXT,Ink);
        DateTime_SetMonthCalColor(date,MCSC_TITLEBK,Green);DateTime_SetMonthCalColor(date,MCSC_TITLETEXT,OnAccent);
    }
}

void chooseTheme() {
    HMENU popup=CreatePopupMenu();
    const wchar_t* names[]={L"晨光 · 浅色",L"夜色 · 深色",L"森林 · 绿色"};
    for(int i=0;i<3;++i)AppendMenuW(popup,MF_STRING|(state.theme==i?MF_CHECKED:0),500+i,names[i]);
    RECT button{};GetWindowRect(themeButton,&button);
    int selected=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_RIGHTALIGN,button.right,button.bottom+px(4),0,mainWindow,nullptr);DestroyMenu(popup);
    if(selected>=500&&selected<503) {state.theme=selected-500;save();applyTheme();}
}

LRESULT controlColor(UINT message,WPARAM wParam,LPARAM lParam=0) {
    HDC dc=reinterpret_cast<HDC>(wParam);SetTextColor(dc,Ink);
    COLORREF background=message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX?Surface:Paper;
    HWND control=reinterpret_cast<HWND>(lParam);
    if(control==pomoClock||control==pomoPhase||control==pomoCount)background=Surface;
    SetBkColor(dc,background);return reinterpret_cast<LRESULT>(background==Surface?whiteBrush:paperBrush);
}

void drawItem(const DRAWITEMSTRUCT* item);

void refreshPomodoro() {
    auto& p=state.pomodoro;
    auto seconds=p.secondsLeft(nowSeconds()); wchar_t clock[32]; swprintf_s(clock,L"%02d:%02d",seconds/60,seconds%60);
    if(textOf(pomoClock)!=clock){SetWindowTextW(pomoClock,clock);InvalidateRect(mainWindow,nullptr,FALSE);}
    const wchar_t* phase=p.phase==0?L"专注":p.phase==1?L"短休息":L"长休息";
    SetWindowTextW(pomoPhase,phase);
    auto count=L"今日完成 "+std::to_wstring(p.todayCount)+L"  ·  本轮 "+std::to_wstring(p.cycle)+L"/4";
    SetWindowTextW(pomoCount,count.c_str());
    SetWindowTextW(pomoStart,p.running?L"暂停":L"开始");
    for(auto edit:{pomoFocus,pomoShort,pomoLong,pomoConfig})EnableWindow(edit,!p.running);
}

void pomodoroTick() {
    if(state.pomodoro.tick(nowSeconds()))save();
    if(activeView==2)refreshPomodoro();
    if(state.pomodoro.noticePending && !automaticallyHidden && GetTickCount64()>=nextNotice) {
        notify(L"番茄钟",state.pomodoro.phase==0?L"休息结束，可以开始下一轮专注。":L"专注结束，请休息。点击专注页面的开始按钮计时。");
        state.pomodoro.noticePending=false;save();
    }
}

void createPomodoro() {
    auto control=[&](const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int y,int width,int height) {
        HWND window=child(type,text,style,mainWindow,id);MoveWindow(window,px(x),px(y),px(width),px(height),TRUE);pomoControls.push_back(window);return window;
    };
    pomoPhase=control(L"STATIC",L"专注",SS_CENTER,-1,8,76,244,22);
    pomoClock=control(L"STATIC",L"25:00",SS_CENTER,-1,8,100,244,45);SendMessageW(pomoClock,WM_SETFONT,reinterpret_cast<WPARAM>(clockFont),TRUE);
    pomoCount=control(L"STATIC",L"",SS_CENTER,-1,8,150,244,22);
    pomoStart=control(L"BUTTON",L"开始",WS_TABSTOP,PomoStartId,8,178,76,28);
    pomoReset=control(L"BUTTON",L"重置",WS_TABSTOP,PomoResetId,92,178,76,28);
    pomoSkip=control(L"BUTTON",L"跳过",WS_TABSTOP,PomoSkipId,176,178,76,28);
    control(L"STATIC",L"专注 / 分钟",SS_CENTER,-1,8,216,76,18);
    control(L"STATIC",L"短休 / 分钟",SS_CENTER,-1,92,216,76,18);
    control(L"STATIC",L"长休 / 分钟",SS_CENTER,-1,176,216,76,18);
    pomoFocus=control(L"EDIT",std::to_wstring(state.pomodoro.focusMinutes).c_str(),WS_TABSTOP|WS_BORDER|ES_NUMBER|ES_CENTER,410,8,238,76,24);
    pomoShort=control(L"EDIT",std::to_wstring(state.pomodoro.shortMinutes).c_str(),WS_TABSTOP|WS_BORDER|ES_NUMBER|ES_CENTER,411,92,238,76,24);
    pomoLong=control(L"EDIT",std::to_wstring(state.pomodoro.longMinutes).c_str(),WS_TABSTOP|WS_BORDER|ES_NUMBER|ES_CENTER,412,176,238,76,24);
    for(auto edit:{pomoFocus,pomoShort,pomoLong})SendMessageW(edit,EM_SETLIMITTEXT,3,0);
    pomoConfig=control(L"BUTTON",L"保存时长",WS_TABSTOP,PomoConfigId,8,270,244,24);
}

LRESULT CALLBACK scheduleProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        scheduleWindow = window;
        auto title = child(L"STATIC", L"日程内容", 0, window, -1); MoveWindow(title,px(14),px(12),px(340),px(20),TRUE);
        scheduleText = child(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,window,401);
        MoveWindow(scheduleText,px(14),px(36),px(342),px(28),TRUE); SendMessageW(scheduleText,EM_SETLIMITTEXT,500,0);
        auto when = child(L"STATIC",L"日期与时间（本地时间）",0,window,-1); MoveWindow(when,px(14),px(76),px(342),px(20),TRUE);
        scheduleDate = child(DATETIMEPICK_CLASSW,L"日期",WS_TABSTOP|DTS_SHORTDATEFORMAT,window,402);
        scheduleTime = child(DATETIMEPICK_CLASSW,L"时间",WS_TABSTOP|DTS_TIMEFORMAT|DTS_UPDOWN,window,403);
        MoveWindow(scheduleDate,px(14),px(100),px(190),px(28),TRUE); MoveWindow(scheduleTime,px(214),px(100),px(142),px(28),TRUE);
        DateTime_SetFormat(scheduleDate,L"yyyy-MM-dd"); DateTime_SetFormat(scheduleTime,L"HH:mm");
        auto repeat = child(L"STATIC",L"重复",0,window,-1); MoveWindow(repeat,px(14),px(144),px(55),px(24),TRUE);
        scheduleRepeat = child(L"COMBOBOX",L"重复",WS_TABSTOP|CBS_DROPDOWNLIST,window,404);
        MoveWindow(scheduleRepeat,px(76),px(142),px(150),px(110),TRUE);
        for (auto option : {L"不重复",L"每天",L"每周"}) SendMessageW(scheduleRepeat,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(option));
        auto note = child(L"STATIC",L"到时通过系统托盘提醒；游戏前台时延后提醒。",0,window,-1); MoveWindow(note,px(14),px(184),px(342),px(36),TRUE);
        auto button = child(L"BUTTON",L"保存日程",WS_TABSTOP|BS_DEFPUSHBUTTON,window,ScheduleSaveId); MoveWindow(button,px(244),px(230),px(112),px(28),TRUE);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == ScheduleSaveId) {
        SYSTEMTIME date{}, time{};
        if (DateTime_GetSystemtime(scheduleDate,&date)!=GDT_VALID || DateTime_GetSystemtime(scheduleTime,&time)!=GDT_VALID) return 0;
        date.wHour=time.wHour; date.wMinute=time.wMinute; date.wSecond=0; date.wMilliseconds=0;
        auto due=timestampFromLocal(date); int repeat=static_cast<int>(SendMessageW(scheduleRepeat,CB_GETCURSEL,0,0));
        if (!state.putSchedule(scheduleEditingId,textOf(scheduleText),due,repeat)) { MessageBoxW(window,L"请填写日程内容和有效的日期时间。",L"保存日程",MB_OK|MB_ICONINFORMATION); return 0; }
        save(); refreshSchedules(); DestroyWindow(window); return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) { DestroyWindow(window); return 0; }
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { scheduleWindow=nullptr;scheduleDate=nullptr;scheduleTime=nullptr;return 0; }
    if (message == WM_DRAWITEM) {drawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;}
    if (message == WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORBTN||message==WM_CTLCOLORLISTBOX)return controlColor(message,wParam);
    if(message==WM_ERASEBKGND){RECT bounds{};GetClientRect(window,&bounds);FillRect(reinterpret_cast<HDC>(wParam),&bounds,paperBrush);return 1;}
    return DefWindowProcW(window,message,wParam,lParam);
}

void openSchedule(uint64_t id) {
    if (scheduleWindow) { SetForegroundWindow(scheduleWindow); return; }
    scheduleEditingId = id;
    RECT bounds{0,0,px(370),px(272)}; AdjustWindowRectExForDpi(&bounds,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW,dpi);
    RECT owner{}; GetWindowRect(mainWindow,&owner); MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST),&info);
    int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    int x=std::clamp<LONG>(owner.left,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-width));
    int y=std::clamp<LONG>(owner.top,info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-height));
    require(CreateWindowExW(WS_EX_TOOLWINDOW,L"FloatingTodoSchedule",id?L"编辑日程":L"新增日程",WS_CAPTION|WS_SYSMENU,x,y,width,height,mainWindow,nullptr,instance,nullptr)!=nullptr,L"无法打开日程编辑器。");
    int64_t due=nowSeconds()+3600; int repeat=0;
    if (id) for (const auto& event : state.schedules) if (event.id==id) { due=event.due; repeat=event.repeat; SetWindowTextW(scheduleText,event.text.c_str()); }
    auto local=localTime(due); DateTime_SetSystemtime(scheduleDate,GDT_VALID,&local); DateTime_SetSystemtime(scheduleTime,GDT_VALID,&local);
    SendMessageW(scheduleRepeat,CB_SETCURSEL,repeat,0);applyTheme(); ShowWindow(scheduleWindow,SW_SHOW); SetFocus(scheduleText);
}

void layout() {
    RECT bounds{}; GetClientRect(mainWindow, &bounds);
    int width = bounds.right, height = bounds.bottom;
    MoveWindow(themeButton,width-px(124),px(10),px(36),px(28),TRUE);
    MoveWindow(settingsButton, width - px(84), px(10), px(36), px(28), TRUE);
    MoveWindow(hideButton, width - px(44), px(10), px(34), px(28), TRUE);
    int segment=(width-px(28))/3;
    for(int i=0;i<3;++i)MoveWindow(viewButtons[i],px(14)+i*segment,px(49),segment,px(28),TRUE);
    MoveWindow(taskList, px(12), px(90)+contentOffset, width - px(24), (std::max)(px(32), height - px(142)), TRUE);
    MoveWindow(scheduleList, px(12), px(90)+contentOffset, width - px(24), (std::max)(px(32), height - px(142)), TRUE);
    MoveWindow(scheduleAdd, px(12), height - px(42)+contentOffset, width - px(24), px(32), TRUE);
    MoveWindow(input, px(22), height - px(34)+contentOffset, width - px(80), px(20), TRUE);
    MoveWindow(addButton, width - px(48), height - px(39)+contentOffset, px(32), px(30), TRUE);
    if(pomoClock) {
        const int positions[]={96,116,162,198,198,198,238,238,238,260,260,260,292};
        const int heights[]={18,42,18,30,30,30,18,18,18,26,26,26,26};
        int column=(width-px(32))/3;
        for(size_t index=0;index<pomoControls.size();++index) {
            if(index==0||index==1||index==2||index==12)MoveWindow(pomoControls[index],px(16),px(positions[index])+contentOffset,width-px(32),px(heights[index]),TRUE);
            else {int c=index>=9?static_cast<int>(index)-9:index>=6?static_cast<int>(index)-6:static_cast<int>(index)-3;MoveWindow(pomoControls[index],px(8)+c*(column+px(8)),px(positions[index])+contentOffset,column,px(heights[index]),TRUE);}
        }
    }
    InvalidateRect(mainWindow, nullptr, TRUE);
}

void drawMain(HWND window,HDC target) {
    RECT rect{}; GetClientRect(window, &rect);
    HDC dc = CreateCompatibleDC(target); HBITMAP bitmap = CreateCompatibleBitmap(target, rect.right, rect.bottom);
    auto previous = SelectObject(dc, bitmap); FillRect(dc, &rect, paperBrush);
    const int width = rect.right, height = rect.bottom;
    COLORREF indicator = state.automatic ? Green : Muted;
    rounded(dc, {px(14), px(20), px(20), px(26)}, indicator, 6, indicator);
    label(dc, L"浮光", {px(27), px(10), width-px(128), px(37)}, titleFont, Ink);
    rounded(dc,{px(12),px(46),width-px(12),px(81)},Track,12,Track);
    int segment=(width-px(28))/3;
    int marker=px(14)+static_cast<int>(navigationPosition*segment);
    rounded(dc,{marker+px(12),px(79),marker+segment-px(12),px(82)},Green,3,Green);
    if (activeView == 0) rounded(dc, {px(12), height - px(44)+contentOffset, width - px(12), height - px(8)+contentOffset}, Surface, 10, Border);
    if(activeView==2) {
        rounded(dc,{px(12),px(90)+contentOffset,width-px(12),px(186)+contentOffset},Surface,14,Border);
        int total=state.pomodoro.duration();double fraction=1.0-static_cast<double>(state.pomodoro.secondsLeft(nowSeconds()))/total;
        int end=px(24)+static_cast<int>((width-px(48))*std::clamp(fraction,0.0,1.0));
        rounded(dc,{px(24),px(181)+contentOffset,width-px(24),px(184)+contentOffset},Track,3,Track);
        if(end>px(24))rounded(dc,{px(24),px(181)+contentOffset,end,px(184)+contentOffset},Green,3,Green);
    }
    BitBlt(target, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
}

void paintMain(HWND window) {
    PAINTSTRUCT paint{};HDC target=BeginPaint(window,&paint);drawMain(window,target);EndPaint(window,&paint);
}

void drawItem(const DRAWITEMSTRUCT* item) {
    if (item->CtlID == ScheduleListId) {
        if (item->itemID == static_cast<UINT>(-1) || item->itemID >= state.schedules.size()) return;
        const auto& event = state.schedules[item->itemID]; auto row = item->rcItem;fill(item->hDC,row,Paper);row.bottom-=px(5);
        rounded(item->hDC,row,item->itemState&ODS_SELECTED?Soft:Surface,10,item->itemState&ODS_SELECTED?Green:Border);
        int x = row.left + px(9), y = row.top + px(19);
        rounded(item->hDC, {x,y,x+px(14),y+px(14)}, event.done ? Green : Surface, 4, event.done ? Green : Border);
        if (event.done) checkMark(item->hDC,x+px(2),y+px(2),OnAccent);
        label(item->hDC,event.text,{row.left+px(32),row.top+px(4),row.right-px(26),row.top+px(27)},event.done?doneFont:rowFont,event.done?Muted:Ink);
        auto time = formatDate(event.due) + (event.repeat == 1 ? L" · 每天" : event.repeat == 2 ? L" · 每周" : L"");
        label(item->hDC,time,{row.left+px(32),row.top+px(27),row.right-px(4),row.bottom-px(3)},smallFont,event.due<nowSeconds()&&!event.done?Green:Muted);
        label(item->hDC,L"×",{row.right-px(24),row.top,row.right-px(4),row.top+px(26)},bodyFont,Muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    } else if (item->CtlID == ListId) {
        if (item->itemID == static_cast<UINT>(-1) || item->itemID >= state.tasks.size()) return;
        const auto& task = state.tasks[item->itemID];
        RECT row = item->rcItem;
        fill(item->hDC,row,Paper);row.bottom-=px(5);
        rounded(item->hDC,row,item->itemState&ODS_SELECTED?Soft:Surface,10,item->itemState&ODS_SELECTED?Green:Border);
        int x = row.left + px(9), y = row.top + px(10);
        rounded(item->hDC, {x,y,x+px(14),y+px(14)}, task.done ? Green : Surface, 4, task.done ? Green : Border);
        if (task.done) checkMark(item->hDC, x+px(2), y+px(2), OnAccent);
        label(item->hDC, task.text, {row.left+px(32),row.top,row.right-px(26),row.bottom}, task.done ? doneFont : rowFont, task.done ? Muted : Ink);
        label(item->hDC, L"×", {row.right-px(24),row.top,row.right-px(4),row.bottom}, bodyFont, Muted, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    } else if (item->CtlType==ODT_BUTTON) {
        bool navigation=item->CtlID>=ViewFirstId&&item->CtlID<ViewFirstId+3;
        bool active=navigation&&static_cast<int>(item->CtlID)-ViewFirstId==activeView;
        bool toolbar=item->CtlID==ThemeId||item->CtlID==SettingsId||item->CtlID==HideId;
        bool primary=item->CtlID==AddId||item->CtlID==PomoStartId||item->CtlID==ScheduleSaveId||item->CtlID==ScheduleAddId||item->CtlID==RuleAddId;
        bool hovered=GetPropW(item->hwndItem,L"Glimmer.Hover")!=nullptr;
        bool pressed=item->itemState&ODS_SELECTED;
        auto background=navigation?(active?Surface:Track):primary?Green:(hovered||pressed)?Soft:toolbar?Paper:Surface;
        auto foreground=primary?OnAccent:active?Green:toolbar||navigation?Muted:Ink;
        if(item->itemState&ODS_DISABLED){background=Track;foreground=Muted;}
        fill(item->hDC,item->rcItem,navigation?Track:Paper);
        RECT button=item->rcItem;if(navigation)InflateRect(&button,-px(2),-px(2));
        if(pressed)OffsetRect(&button,0,px(1));
        rounded(item->hDC,button,background,10,navigation||toolbar||primary?background:Border);
        label(item->hDC, textOf(item->hwndItem), button, bodyFont, foreground, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if (item->itemState & ODS_FOCUS) { RECT focus = item->rcItem; InflateRect(&focus, -px(3), -px(3)); DrawFocusRect(item->hDC, &focus); }
    }
}

std::wstring foregroundExe(HWND window) {
    if (!window) return {};
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (!pid || pid == GetCurrentProcessId()) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    // 系统保护进程和已经退出的进程可能拒绝查询。
    if (!process) return {};
    wchar_t path[32768]; DWORD length = static_cast<DWORD>(std::size(path));
    BOOL queried = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    return queried ? normalizeExe(std::wstring(path, length)) : std::wstring{};
}

int cp(int value) { return MulDiv(value, compactDpi, 96); }

void compactFonts() {
    for(auto font:{compactFont,compactSmallFont})if(font)DeleteObject(font);
    compactFont=CreateFontW(-cp(12),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    compactSmallFont=CreateFontW(-cp(11),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    require(compactFont && compactSmallFont,L"无法创建收起窗口字体。");
}

void positionCompact() {
    if(!compactWindow)return;
    bool bar=state.collapseMode==CollapseMode::TopBar;
    RECT desired{state.capsuleX,state.capsuleY,state.capsuleX+1,state.capsuleY+1};
    HMONITOR monitor=bar || (state.capsuleX==-1 && state.capsuleY==-1)?MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST):MonitorFromRect(&desired,MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};require(GetMonitorInfoW(monitor,&info)!=FALSE,L"无法获取收起窗口的屏幕工作区。");
    int width=(std::min)(cp(bar?720:304),static_cast<int>(info.rcWork.right-info.rcWork.left)-cp(16));
    int height=cp(bar && width>=cp(480)?46:66);
    int x=bar?info.rcWork.left+(info.rcWork.right-info.rcWork.left-width)/2:state.capsuleX;
    int y=bar?info.rcWork.top+cp(6):state.capsuleY;
    if(!bar && state.capsuleX==-1 && state.capsuleY==-1) {
        RECT main{};GetWindowRect(mainWindow,&main);x=IsIconic(mainWindow)?info.rcWork.right-width-cp(20):main.left;y=IsIconic(mainWindow)?info.rcWork.top+cp(60):main.top;
    }
    x=std::clamp(x,static_cast<int>(info.rcWork.left),(std::max)(static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-width));
    y=std::clamp(y,static_cast<int>(info.rcWork.top),(std::max)(static_cast<int>(info.rcWork.top),static_cast<int>(info.rcWork.bottom)-height));
    RECT current{};GetWindowRect(compactWindow,&current);
    bool topmost=(GetWindowLongPtrW(compactWindow,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    if(current.left!=x || current.top!=y || current.right-current.left!=width || current.bottom-current.top!=height || topmost!=state.topmost)
        SetWindowPos(compactWindow,state.topmost?HWND_TOPMOST:HWND_NOTOPMOST,x,y,width,height,SWP_NOACTIVATE);
    MoveWindow(compactExpand,width-cp(82),cp(9),cp(44),cp(28),TRUE);
    MoveWindow(compactMenu,width-cp(34),cp(9),cp(24),cp(28),TRUE);
    if(!bar){state.capsuleX=x;state.capsuleY=y;}
}

void drawCompact(HWND window,HDC target) {
    RECT rect{};GetClientRect(window,&rect);
    HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,rect.right,rect.bottom);auto old=SelectObject(dc,bitmap);
    fill(dc,rect,Paper);
    auto brush=CreateSolidBrush(Surface);auto pen=CreatePen(PS_SOLID,1,Border);auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
    RoundRect(dc,0,0,rect.right,rect.bottom,cp(24),cp(24));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
    auto info=state.compactInfo(nowSeconds());
    if(state.collapseMode==CollapseMode::TopBar && rect.right>=cp(480)) {
        label(dc,info.tasks,{cp(16),0,cp(114),rect.bottom},compactFont,Ink);
        label(dc,info.timer,{cp(120),0,cp(258),rect.bottom},compactFont,state.pomodoro.running?Green:Muted);
        label(dc,info.schedule,{cp(268),0,rect.right-cp(94),rect.bottom},compactSmallFont,Muted);
    } else {
        label(dc,info.tasks,{cp(16),cp(9),cp(102),cp(30)},compactFont,Ink);
        label(dc,info.timer,{cp(106),cp(9),rect.right-cp(88),cp(30)},compactSmallFont,state.pomodoro.running?Green:Muted);
        label(dc,info.schedule,{cp(16),cp(34),rect.right-cp(16),cp(55)},compactSmallFont,Muted);
    }
    BitBlt(target,0,0,rect.right,rect.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}

LRESULT CALLBACK compactProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_CREATE:
        compactWindow=window;compactDpi=static_cast<int>(GetDpiForWindow(window));compactFonts();
        compactExpand=child(L"BUTTON",L"展开",WS_TABSTOP,window,ExpandId);
        compactMenu=child(L"BUTTON",L"⋯",WS_TABSTOP,window,CompactMenuId);
        SetWindowTextW(compactMenu,L"菜单");
        compactTooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,instance,nullptr);
        require(compactTooltip!=nullptr,L"无法创建收起信息提示。");
        { TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.hwnd=window;tool.uId=reinterpret_cast<UINT_PTR>(window);tool.lpszText=const_cast<wchar_t*>(L"浮光待办");SendMessageW(compactTooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool)); }
        SendMessageW(compactTooltip,TTM_SETMAXTIPWIDTH,0,cp(420));return 0;
    case WM_COMMAND:
        if(LOWORD(wParam)==ExpandId)PostMessageW(mainWindow,ShowRequest,0,0);
        if(LOWORD(wParam)==CompactMenuId)menu(window);
        return 0;
    case WM_CONTEXTMENU:menu(window);return 0;
    case WM_LBUTTONDBLCLK:PostMessageW(mainWindow,ShowRequest,0,0);return 0;
    case WM_LBUTTONDOWN:
        if(state.collapseMode==CollapseMode::Capsule){POINT point{};GetCursorPos(&point);SendMessageW(window,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(point.x,point.y));}
        return 0;
    case WM_NCLBUTTONDBLCLK:PostMessageW(mainWindow,ShowRequest,0,0);return 0;
    case WM_EXITSIZEMOVE: {
        RECT rect{};GetWindowRect(window,&rect);state.capsuleX=rect.left;state.capsuleY=rect.top;positionCompact();save();return 0;
    }
    case WM_DPICHANGED:compactDpi=HIWORD(wParam);compactFonts();positionCompact();return 0;
    case WM_DISPLAYCHANGE:case WM_SETTINGCHANGE:positionCompact();return 0;
    case WM_SIZE: {
        RECT rect{};GetClientRect(window,&rect);HRGN region=CreateRoundRectRgn(0,0,rect.right+1,rect.bottom+1,cp(24),cp(24));
        require(region && SetWindowRgn(window,region,TRUE)!=0,L"无法设置收起窗口形状。");return 0;
    }
    case WM_DRAWITEM: {
        auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lParam);fill(item->hDC,item->rcItem,Surface);
        label(item->hDC,item->CtlID==ExpandId?L"展开":L"···",item->rcItem,compactSmallFont,item->itemState&ODS_SELECTED?Ink:Green,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item->itemState&ODS_FOCUS){RECT focus=item->rcItem;InflateRect(&focus,-cp(2),-cp(2));DrawFocusRect(item->hDC,&focus);}return TRUE;
    }
    case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);drawCompact(window,dc);EndPaint(window,&paint);return 0;}
    case WM_PRINTCLIENT:drawCompact(window,reinterpret_cast<HDC>(wParam));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_CLOSE:PostMessageW(mainWindow,ShowRequest,0,0);return 0;
    case WM_DESTROY:
        for(auto font:{compactFont,compactSmallFont})if(font)DeleteObject(font);
        compactFont=nullptr;compactSmallFont=nullptr;compactWindow=nullptr;compactTooltip=nullptr;return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

void refreshCompact() {
    auto info=state.compactInfo(nowSeconds());
    if(compactWindow && compactDetails!=info.details) {
        compactDetails=info.details;SetWindowTextW(compactWindow,compactDetails.c_str());
        TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.hwnd=compactWindow;tool.uId=reinterpret_cast<UINT_PTR>(compactWindow);tool.lpszText=compactDetails.data();
        SendMessageW(compactTooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));InvalidateRect(compactWindow,nullptr,FALSE);
    }
    auto title=manuallyHidden && state.collapseMode==CollapseMode::Taskbar?L"浮光 · "+info.tasks+L" · "+info.timer+L" · "+info.schedule:L"浮光待办";
    if(textOf(mainWindow)!=title)SetWindowTextW(mainWindow,title.c_str());
    updateTray();
}

void applyVisibility() {
    if(!ready || applyingVisibility)return;
    applyingVisibility=true;
    bool compact=manuallyHidden && !automaticallyHidden && (state.collapseMode==CollapseMode::Capsule || state.collapseMode==CollapseMode::TopBar);
    bool taskbar=manuallyHidden && !automaticallyHidden && state.collapseMode==CollapseMode::Taskbar;
    if(automaticallyHidden || manuallyHidden) {
        for(auto window:{fileDialog,settingsWindow,scheduleWindow})if(window && IsWindowVisible(window))ShowWindow(window,SW_HIDE);
        if(taskbar) {
            if(followCurrentDesktop({mainWindow}) && (!IsWindowVisible(mainWindow) || !IsIconic(mainWindow)))ShowWindow(mainWindow,SW_SHOWMINNOACTIVE);
        } else if(IsWindowVisible(mainWindow))ShowWindow(mainWindow,SW_HIDE);
    } else if(followCurrentDesktop({mainWindow,settingsWindow,fileDialog,scheduleWindow})) {
        if(!IsWindowVisible(mainWindow) || IsIconic(mainWindow))ShowWindow(mainWindow,SW_SHOWNOACTIVATE);
        for(auto window:{settingsWindow,fileDialog,scheduleWindow})if(window && !IsWindowVisible(window))ShowWindow(window,SW_SHOWNOACTIVATE);
    }
    if(compact) {
        positionCompact();
        if(followCurrentDesktop({compactWindow}) && !IsWindowVisible(compactWindow))ShowWindow(compactWindow,SW_SHOWNOACTIVATE);
    } else if(compactWindow && IsWindowVisible(compactWindow)) {
        SendMessageW(compactTooltip,TTM_POP,0,0);ShowWindow(compactWindow,SW_HIDE);
    }
    refreshCompact();applyingVisibility=false;
}

void evaluateForeground() {
    if(!ready)return;
    HWND foreground=GetForegroundWindow();DWORD cloaked=0;
    if(!state.automatic)automaticallyHidden=false;
    // 焦点切换期间保留自动隐藏状态，手动收起仍然立即生效。
    if(foreground && isOnCurrentDesktop(foreground) && !(SUCCEEDED(DwmGetWindowAttribute(foreground,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked))
        automaticallyHidden=state.matches(foregroundExe(foreground));
    applyVisibility();
}

void CALLBACK foregroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    PostMessageW(mainWindow, ForegroundMessage, 0, 0);
}

void CALLBACK desktopEvent(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD) {
    if (window == mainWindow || window == compactWindow) PostMessageW(mainWindow, ForegroundMessage, 0, 0);
}

void updateTray() {
    auto info=state.compactInfo(nowSeconds());
    std::wstring hint=automaticallyHidden?L"浮光 · 自动隐藏":manuallyHidden?std::wstring(L"浮光 · ")+CollapseNames[static_cast<int>(state.collapseMode)]:L"浮光待办";
    hint+=L"\n"+info.details;
    hint.resize((std::min)(hint.size(),std::size(tray.szTip)-1));
    if(lastTrayTip==hint)return;
    lastTrayTip=hint;
    lstrcpynW(tray.szTip,hint.c_str(),static_cast<int>(std::size(tray.szTip)));
    Shell_NotifyIconW(NIM_MODIFY, &tray);
}

void addTray() {
    lastTrayTip.clear();
    tray.cbSize = sizeof(tray); tray.hWnd = mainWindow; tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; tray.uCallbackMessage = TrayMessage; tray.hIcon = appIcon;
    lstrcpyW(tray.szTip, L"浮光待办");
    require(Shell_NotifyIconW(NIM_ADD, &tray) != FALSE, L"无法创建系统托盘图标。");
}

void refreshRules() {
    refreshingRules = true;
    ListView_DeleteAllItems(rulesList);
    for (size_t i = 0; i < state.rules.size(); ++i) {
        auto& rule = state.rules[i];
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = rule.label.data();
        ListView_InsertItem(rulesList, &item);
        ListView_SetItemText(rulesList, static_cast<int>(i), 1, rule.exe.data());
        ListView_SetCheckState(rulesList, static_cast<int>(i), rule.enabled);
    }
    refreshingRules = false;
}

void addRule(const std::wstring& value) {
    if (!state.addRule(value)) { MessageBoxW(settingsWindow, L"请输入有效的 EXE 文件名，例如 notepad.exe。已经添加的软件无需重复添加。", L"添加软件", MB_OK | MB_ICONINFORMATION); return; }
    save(); refreshRules(); SetWindowTextW(ruleInput, L""); evaluateForeground();
}

LRESULT CALLBACK ruleInputProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_GETDLGCODE && wParam == VK_RETURN) return DLGC_WANTALLKEYS;
    if (message == WM_KEYDOWN && wParam == VK_RETURN) { addRule(textOf(window)); return 0; }
    if (message == WM_CHAR && wParam == VK_RETURN) return 0;
    return DefSubclassProc(window, message, wParam, lParam);
}

void openSettings();

void updateAction() {
    auto update = updateStatus();
    if (update.phase == UpdatePhase::Available) downloadUpdate();
    else if (update.phase == UpdatePhase::Ready) {
        if (MessageBoxW(settingsWindow, L"更新已校验。现在保存数据并安装、重启程序？", L"安装更新", MB_YESNO|MB_ICONQUESTION) == IDYES) {
            save(); installUpdate(); DestroyWindow(mainWindow);
        }
    } else checkUpdate();
}

void refreshUpdate() {
    if (!settingsWindow) return;
    auto update = updateStatus();
    auto text = update.phase == UpdatePhase::Idle ? L"当前版本 " + wide(AppVersion) : update.message;
    if (textOf(updateLabel) != text) SetWindowTextW(updateLabel, text.c_str());
    const wchar_t* button = update.phase == UpdatePhase::Available ? L"下载更新" : update.phase == UpdatePhase::Ready ? L"安装并重启" : L"检查更新";
    SetWindowTextW(updateButton, button);
    EnableWindow(updateButton, update.phase != UpdatePhase::Checking && update.phase != UpdatePhase::Downloading);
}

UINT_PTR CALLBACK fileDialogProc(HWND window, UINT message, WPARAM, LPARAM) {
    // 使用 Windows 经典文件选择窗口，控制文件浏览时的内存占用。
    if (message == WM_INITDIALOG) fileDialog = window;
    if (message == WM_DESTROY) fileDialog = nullptr;
    return FALSE;
}

void menu(HWND owner) {
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, MenuShow, manuallyHidden ? L"展开完整窗口" : L"收起窗口");
    HMENU modes=CreatePopupMenu();
    for(int i=0;i<4;++i)AppendMenuW(modes,MF_STRING|(static_cast<int>(state.collapseMode)==i?MF_CHECKED:0),CollapseFirstId+i,CollapseNames[i]);
    AppendMenuW(popup,MF_POPUP,reinterpret_cast<UINT_PTR>(modes),L"收起模式");
    AppendMenuW(popup, MF_STRING | (state.automatic ? MF_CHECKED : 0), MenuAuto, L"自动隐藏");
    AppendMenuW(popup, MF_STRING | (state.topmost ? MF_CHECKED : 0), MenuTop, L"保持置顶");
    AppendMenuW(popup, MF_STRING, MenuSettings, L"设置…");
    AppendMenuW(popup, MF_STRING, MenuClear, L"清理已完成待办");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, MenuExit, L"退出浮光待办");
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(owner);
    int selected=TrackPopupMenu(popup, TPM_RIGHTBUTTON|TPM_RETURNCMD, point.x, point.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0); DestroyMenu(popup);
    if(selected)command(selected);
}

LRESULT CALLBACK settingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        settingsWindow = window;
        auto text = child(L"STATIC", L"前台运行时隐藏，切换离开后自动显示。", 0, window, -1);
        MoveWindow(text,px(14),px(12),px(412),px(20),TRUE);
        automaticBox = child(L"BUTTON", L"启用自动隐藏", BS_AUTOCHECKBOX|WS_TABSTOP, window, AutoId);
        topmostBox = child(L"BUTTON", L"保持待办窗口置顶", BS_AUTOCHECKBOX|WS_TABSTOP, window, TopId);
        MoveWindow(automaticBox,px(14),px(38),px(170),px(24),TRUE);
        MoveWindow(topmostBox,px(210),px(38),px(214),px(24),TRUE);
        Button_SetCheck(automaticBox, state.automatic ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(topmostBox, state.topmost ? BST_CHECKED : BST_UNCHECKED);
        rulesList = child(WC_LISTVIEWW,L"软件检测列表",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|WS_BORDER,window,RulesId);
        ListView_SetExtendedListViewStyle(rulesList,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        MoveWindow(rulesList,px(14),px(76),px(412),px(206),TRUE);
        LVCOLUMNW column{}; column.mask=LVCF_TEXT|LVCF_WIDTH; column.cx=px(235); column.pszText=const_cast<wchar_t*>(L"检测软件"); ListView_InsertColumn(rulesList,0,&column);
        column.cx=px(155); column.pszText=const_cast<wchar_t*>(L"进程文件名"); ListView_InsertColumn(rulesList,1,&column);
        ruleInput = child(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,window,RuleInputId);
        MoveWindow(ruleInput,px(14),px(294),px(220),px(28),TRUE);
        SendMessageW(ruleInput,EM_SETLIMITTEXT,32767,0);
        SendMessageW(ruleInput,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"输入软件.exe 或选择程序文件"));
        SetWindowSubclass(ruleInput,ruleInputProc,1,0);
        auto add=child(L"BUTTON",L"添加软件",WS_TABSTOP,window,RuleAddId);
        MoveWindow(add,px(244),px(294),px(76),px(28),TRUE);
        auto browse=child(L"BUTTON",L"选择 EXE…",WS_TABSTOP,window,BrowseId);
        MoveWindow(browse,px(330),px(294),px(96),px(28),TRUE);
        auto remove=child(L"BUTTON",L"移除选中软件",WS_TABSTOP,window,RemoveId);
        MoveWindow(remove,px(14),px(334),px(120),px(28),TRUE);
        auto note=child(L"STATIC",L"勾选启用检测 · 修改后自动保存\nCS 1.6 / Source 与同名 Half-Life 进程共用规则。",0,window,-1);
        MoveWindow(note,px(14),px(372),px(412),px(34),TRUE);
        auto appearance=child(L"STATIC",L"外观主题",0,window,-1);MoveWindow(appearance,px(14),px(420),px(68),px(26),TRUE);
        themeChoice=child(L"COMBOBOX",L"外观主题",WS_TABSTOP|CBS_DROPDOWNLIST,window,ThemeChoiceId);
        MoveWindow(themeChoice,px(88),px(418),px(230),px(130),TRUE);
        for(auto text:{L"晨光 · 浅色",L"夜色 · 深色",L"森林 · 绿色"})SendMessageW(themeChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        SendMessageW(themeChoice,CB_SETCURSEL,state.theme,0);
        auto collapseLabel=child(L"STATIC",L"收起模式",0,window,-1);MoveWindow(collapseLabel,px(14),px(460),px(68),px(26),TRUE);
        collapseChoice=child(L"COMBOBOX",L"收起模式",WS_TABSTOP|CBS_DROPDOWNLIST,window,CollapseChoiceId);
        MoveWindow(collapseChoice,px(88),px(458),px(230),px(140),TRUE);
        for(auto name:CollapseNames)SendMessageW(collapseChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(collapseChoice,CB_SETCURSEL,static_cast<int>(state.collapseMode),0);
        updateLabel=child(L"STATIC",L"",0,window,-1);
        MoveWindow(updateLabel,px(14),px(508),px(290),px(42),TRUE);
        updateButton=child(L"BUTTON",L"检查更新",WS_TABSTOP,window,UpdateId);
        MoveWindow(updateButton,px(316),px(508),px(110),px(30),TRUE);
        refreshUpdate();
        refreshRules(); return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return controlColor(message,wParam,lParam);
    case WM_DRAWITEM:drawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ThemeChoiceId: if(HIWORD(wParam)==CBN_SELCHANGE){state.theme=static_cast<int>(SendMessageW(themeChoice,CB_GETCURSEL,0,0));save();applyTheme();}break;
        case CollapseChoiceId: if(HIWORD(wParam)==CBN_SELCHANGE){state.collapseMode=static_cast<CollapseMode>(SendMessageW(collapseChoice,CB_GETCURSEL,0,0));save();}break;
        case UpdateId: updateAction(); break;
        case AutoId: state.automatic=Button_GetCheck(automaticBox)==BST_CHECKED; save(); evaluateForeground(); InvalidateRect(mainWindow,nullptr,FALSE); break;
        case TopId: state.topmost=Button_GetCheck(topmostBox)==BST_CHECKED; SetWindowPos(mainWindow,state.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); save(); break;
        case RuleAddId: addRule(textOf(ruleInput)); break;
        case BrowseId: {
            wchar_t file[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=window;
            dialog.lpstrFilter=L"可执行程序 (*.exe)\0*.exe\0"; dialog.lpstrFile=file; dialog.nMaxFile=static_cast<DWORD>(std::size(file));
            dialog.lpstrTitle=L"选择需要检测的软件主程序";
            dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_ENABLEHOOK|OFN_HIDEREADONLY|OFN_DONTADDTORECENT;
            dialog.lpfnHook=fileDialogProc;
            if (GetOpenFileNameW(&dialog)) addRule(file);
            else require(CommDlgExtendedError()==0,L"无法打开程序选择窗口。");
            break;
        }
        case RemoveId: {
            int selected=ListView_GetNextItem(rulesList,-1,LVNI_SELECTED);
            if(selected>=0) { state.rules.erase(state.rules.begin()+selected); save(); refreshRules(); evaluateForeground(); }
            break;
        }
        } return 0;
    case WM_NOTIFY: {
        auto header=reinterpret_cast<NMHDR*>(lParam);
        if(header->idFrom==RulesId && header->code==LVN_ITEMCHANGED && !refreshingRules) {
            auto change=reinterpret_cast<NMLISTVIEW*>(lParam);
            if(change->iItem>=0 && (change->uChanged&LVIF_STATE) && ((change->uNewState^change->uOldState)&LVIS_STATEIMAGEMASK)) {
                state.rules.at(change->iItem).enabled=ListView_GetCheckState(rulesList,change->iItem)!=FALSE; save(); evaluateForeground();
            }
        } return 0;
    }
    case WM_ERASEBKGND: { RECT rect{}; GetClientRect(window,&rect); FillRect(reinterpret_cast<HDC>(wParam),&rect,paperBrush); return 1; }
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: settingsWindow=nullptr; rulesList=nullptr; ruleInput=nullptr;themeChoice=nullptr;collapseChoice=nullptr; return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

void openSettings() {
    if(manuallyHidden){manuallyHidden=false;applyVisibility();}
    if (settingsWindow) { ShowWindow(settingsWindow,SW_SHOWNORMAL); SetForegroundWindow(settingsWindow); return; }
    RECT owner{}; GetWindowRect(mainWindow,&owner);
    RECT rect{0,0,px(440),px(560)};
    AdjustWindowRectExForDpi(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW,dpi);
    settingsWindow=CreateWindowExW(WS_EX_TOOLWINDOW,L"FloatingTodoSettings",L"浮光待办 · 设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        owner.left-px(80),owner.top+px(16),rect.right-rect.left,rect.bottom-rect.top,mainWindow,nullptr,instance,nullptr);
    require(settingsWindow!=nullptr,L"无法打开设置。");
    applyTheme();
    // 使用工作区约束，保证靠近屏幕边缘时仍能看到全部设置。
    HMONITOR monitor=MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST); MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info);
    RECT bounds{}; GetWindowRect(settingsWindow,&bounds);
    int x=std::clamp(bounds.left,info.rcWork.left,(std::max)(info.rcWork.left,info.rcWork.right-(bounds.right-bounds.left)));
    int y=std::clamp(bounds.top,info.rcWork.top,(std::max)(info.rcWork.top,info.rcWork.bottom-(bounds.bottom-bounds.top)));
    SetWindowPos(settingsWindow,HWND_TOP,x,y,0,0,SWP_NOSIZE); ShowWindow(settingsWindow,SW_SHOW); SetForegroundWindow(settingsWindow);
}

void command(int id) {
    if(id>=ViewFirstId&&id<ViewFirstId+3){selectView(id-ViewFirstId);return;}
    if(id>=CollapseFirstId&&id<CollapseFirstId+4) {
        state.collapseMode=static_cast<CollapseMode>(id-CollapseFirstId);manuallyHidden=true;save();evaluateForeground();
        if(collapseChoice)SendMessageW(collapseChoice,CB_SETCURSEL,static_cast<int>(state.collapseMode),0);
        return;
    }
    switch(id) {
    case ThemeId:chooseTheme();break;
    case PomoStartId: state.pomodoro.toggle(nowSeconds());save();refreshPomodoro();break;
    case PomoResetId: state.pomodoro.reset();save();refreshPomodoro();break;
    case PomoSkipId: state.pomodoro.skip();save();refreshPomodoro();break;
    case PomoConfigId: {
        BOOL focusValid=FALSE,shortValid=FALSE,longValid=FALSE;
        int focus=GetDlgItemInt(mainWindow,410,&focusValid,FALSE),shortBreak=GetDlgItemInt(mainWindow,411,&shortValid,FALSE),longBreak=GetDlgItemInt(mainWindow,412,&longValid,FALSE);
        if(!focusValid||!shortValid||!longValid||!state.pomodoro.configure(focus,shortBreak,longBreak))MessageBoxW(mainWindow,L"专注时间为 1–180 分钟，短休息为 1–60 分钟，长休息为 1–120 分钟。",L"番茄钟",MB_OK|MB_ICONINFORMATION);
        else {save();refreshPomodoro();}break;
    }
    case ScheduleAddId: openSchedule(); break;
    case AddId: submitTask(); break;
    case SettingsId: case MenuSettings: openSettings(); break;
    case HideId: case MenuShow: manuallyHidden=!manuallyHidden; evaluateForeground(); updateTray(); break;
    case MenuAuto: state.automatic=!state.automatic; if(settingsWindow)Button_SetCheck(automaticBox,state.automatic?BST_CHECKED:BST_UNCHECKED); save(); evaluateForeground(); InvalidateRect(mainWindow,nullptr,FALSE); break;
    case MenuTop: state.topmost=!state.topmost; SetWindowPos(mainWindow,state.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); if(settingsWindow)Button_SetCheck(topmostBox,state.topmost?BST_CHECKED:BST_UNCHECKED); save(); break;
    case MenuClear: std::erase_if(state.tasks,[](const Task& task){return task.done;}); cancelEdit(); save(); refreshTasks(); break;
    case MenuExit: DestroyWindow(mainWindow); break;
    }
}

LRESULT CALLBACK mainProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==taskbarCreated && taskbarCreated) { reconnectVirtualDesktops(); addTray(); evaluateForeground(); return 0; }
    switch(message) {
    case WM_CREATE:
        mainWindow=window;
        { const wchar_t* names[]={L"待办",L"日程",L"专注"};for(int i=0;i<3;++i)viewButtons[i]=child(L"BUTTON",names[i],WS_TABSTOP,window,ViewFirstId+i); }
        themeButton=child(L"BUTTON",L"主题",WS_TABSTOP,window,ThemeId);
        settingsButton=child(L"BUTTON",L"设置",BS_OWNERDRAW|WS_TABSTOP,window,SettingsId);
        hideButton=child(L"BUTTON",L"收起",BS_OWNERDRAW|WS_TABSTOP,window,HideId);
        taskList=child(L"LISTBOX",L"待办列表",WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,window,ListId);
        SendMessageW(taskList,LB_SETITEMHEIGHT,0,px(40)); SetWindowSubclass(taskList,listProc,1,0);
        input=child(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,window,InputId);
        SendMessageW(input,EM_SETLIMITTEXT,500,0); SetWindowSubclass(input,inputProc,1,0);
        addButton=child(L"BUTTON",L"＋",WS_TABSTOP|BS_OWNERDRAW,window,AddId);
        scheduleList=child(L"LISTBOX",L"日程列表",WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,window,ScheduleListId);
        SendMessageW(scheduleList,LB_SETITEMHEIGHT,0,px(58)); SetWindowSubclass(scheduleList,listProc,2,0);
        scheduleAdd=child(L"BUTTON",L"＋ 新增日程",WS_TABSTOP,window,ScheduleAddId);
        createPomodoro();cancelEdit(); refreshTasks(); refreshSchedules(); selectView(0);themeWindow(mainWindow); return 0;
    case WM_SIZE:
        if(wParam!=SIZE_MINIMIZED && taskList) {
            if(!applyingVisibility && manuallyHidden && state.collapseMode==CollapseMode::Taskbar){manuallyHidden=false;applyVisibility();}
            layout();
        }
        return 0;
    case WM_SYSCOMMAND:
        if((wParam&0xFFF0)==SC_MINIMIZE){manuallyHidden=true;evaluateForeground();return 0;}
        if((wParam&0xFFF0)==SC_RESTORE && manuallyHidden){manuallyHidden=false;applyVisibility();return 0;}
        break;
    case WM_EXITSIZEMOVE: {
        if(IsIconic(window))return 0;
        RECT bounds{}; GetWindowRect(window,&bounds); state.x=bounds.left;state.y=bounds.top;
        state.width=dip(bounds.right-bounds.left);state.height=dip(bounds.bottom-bounds.top);save(); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info=reinterpret_cast<MINMAXINFO*>(lParam);info->ptMinTrackSize={px(240),px(activeView==2?320:200)};info->ptMaxTrackSize={px(900),px(1200)};return 0;
    }
    case WM_NCCALCSIZE: return 0;
    case WM_NCHITTEST: {
        RECT bounds{};GetWindowRect(window,&bounds);int x=GET_X_LPARAM(lParam)-bounds.left,y=GET_Y_LPARAM(lParam)-bounds.top;
        int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top,edge=px(6);
        if(y<edge)return x<edge?HTTOPLEFT:x>=width-edge?HTTOPRIGHT:HTTOP;
        if(y>=height-edge)return x<edge?HTBOTTOMLEFT:x>=width-edge?HTBOTTOMRIGHT:HTBOTTOM;
        if(x<edge)return HTLEFT;
        if(x>=width-edge)return HTRIGHT;
        if(y<px(44) && x<width-px(128))return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DPICHANGED: {
        dpi=HIWORD(wParam);fonts();auto rect=reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
        if(settingsWindow) {DestroyWindow(settingsWindow);openSettings();}return 0;
    }
    case WM_PAINT: paintMain(window);return 0;
    case WM_PRINTCLIENT:drawMain(window,reinterpret_cast<HDC>(wParam));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_MEASUREITEM: { auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(lParam); item->itemHeight=px(item->CtlID==ScheduleListId?58:40);return TRUE; }
    case WM_DRAWITEM:drawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;
    case WM_CTLCOLORLISTBOX:SetBkColor(reinterpret_cast<HDC>(wParam),Paper);return reinterpret_cast<LRESULT>(paperBrush);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:return controlColor(message,wParam,lParam);
    case WM_COMMAND:
        if(LOWORD(wParam)==ScheduleListId && HIWORD(wParam)==LBN_DBLCLK) {
            int selected=static_cast<int>(SendMessageW(scheduleList,LB_GETCURSEL,0,0));
            if(selected>=0 && selected<static_cast<int>(state.schedules.size()))openSchedule(state.schedules[selected].id);
        } else if(LOWORD(wParam)==ListId && HIWORD(wParam)==LBN_DBLCLK) {
            editSelected();
        } else {
            command(LOWORD(wParam));
        }
        return 0;
    case WM_CONTEXTMENU:menu(window);return 0;
    case WM_DISPLAYCHANGE:case WM_SETTINGCHANGE:positionCompact();return 0;
    case ForegroundMessage:evaluateForeground();return 0;
    case ShowRequest:manuallyHidden=false;evaluateForeground();updateTray();return 0;
    case WM_TIMER:if(wParam==2){animateView();return 0;}evaluateForeground();refreshUpdate();pomodoroTick();scheduleNotices();return 0;
    case TrayMessage:
        if(lParam==WM_LBUTTONDBLCLK) {manuallyHidden=false;evaluateForeground();updateTray();}
        if(lParam==WM_RBUTTONUP)menu(window);
        return 0;
    case WM_QUERYENDSESSION:save();return TRUE;
    case WM_CLOSE:manuallyHidden=true;evaluateForeground();updateTray();return 0;
    case WM_DESTROY:
        if(compactWindow)DestroyWindow(compactWindow);
        ready=false;KillTimer(window,1);KillTimer(window,2);if(foregroundHook)UnhookWinEvent(foregroundHook);if(desktopHook)UnhookWinEvent(desktopHook);Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

HICON createIcon() {
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP color=CreateCompatibleBitmap(screen,32,32);
    HBITMAP mask=CreateBitmap(32,32,1,1,nullptr);auto old=SelectObject(dc,color);
    fill(dc,{0,0,32,32},Green);
    auto pen=CreatePen(PS_SOLID,3,RGB(255,255,255));auto previous=SelectObject(dc,pen);
    MoveToEx(dc,8,16,nullptr);LineTo(dc,14,22);LineTo(dc,25,10);SelectObject(dc,previous);DeleteObject(pen);
    SelectObject(dc,old);auto maskOld=SelectObject(dc,mask);PatBlt(dc,0,0,32,32,BLACKNESS);SelectObject(dc,maskOld);
    ICONINFO info{TRUE,0,0,mask,color};HICON icon=CreateIconIndirect(&info);
    DeleteObject(color);DeleteObject(mask);DeleteDC(dc);ReleaseDC(nullptr,screen);return icon;
}
}

int WINAPI wWinMain(HINSTANCE app,HINSTANCE,LPWSTR,int) {
    instance=app;
    if (runUpdateHelper()) return 0;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::set_terminate([]{fatal(L"数据或程序状态无效，应用已停止。请检查数据文件与安装目录权限。");});
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\FloatingTodo.DesktopList.v1");
    require(mutex!=nullptr,L"无法检查运行实例。");
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(L"FloatingTodoMain",nullptr);
        require(existing!=nullptr,L"已有实例正在启动，请稍后再次打开。");
        require(PostMessageW(existing,ShowRequest,0,0)!=FALSE,L"无法打开已有实例。");
        CloseHandle(mutex);return 0;
    }
    initializeVirtualDesktops();
    PWSTR local=nullptr;require(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)==S_OK,L"无法获取本地数据目录。");
    dataFile=std::filesystem::path(local)/L"DesktopList"/L"state.json";CoTaskMemFree(local);
    state.load(dataFile);
    dpi=static_cast<int>(GetDpiForSystem());
    colors=themePalette(state.theme);paperBrush=CreateSolidBrush(Paper);whiteBrush=CreateSolidBrush(Surface);fonts();appIcon=createIcon();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_DATE_CLASSES|ICC_TAB_CLASSES};require(InitCommonControlsEx(&controls)!=FALSE,L"无法初始化界面。");
    WNDCLASSEXW klass{sizeof(klass)};klass.lpfnWndProc=mainProc;klass.hInstance=instance;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=appIcon;klass.hIconSm=appIcon;klass.lpszClassName=L"FloatingTodoMain";
    require(RegisterClassExW(&klass)!=0,L"无法注册待办窗口。");
    klass.lpfnWndProc=settingsProc;klass.lpszClassName=L"FloatingTodoSettings";klass.hbrBackground=paperBrush;
    require(RegisterClassExW(&klass)!=0,L"无法注册设置窗口。");
    klass.lpfnWndProc=scheduleProc;klass.lpszClassName=L"FloatingTodoSchedule";
    require(RegisterClassExW(&klass)!=0,L"无法注册日程编辑器。");
    klass.lpfnWndProc=compactProc;klass.lpszClassName=L"FloatingTodoCompact";klass.hbrBackground=nullptr;klass.style=CS_DBLCLKS;
    require(RegisterClassExW(&klass)!=0,L"无法注册收起窗口。");
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=px(state.width),height=px(state.height);
    RECT desired{state.x,state.y,state.x+width,state.y+height};
    HMONITOR monitor=MonitorFromRect(&desired,MONITOR_DEFAULTTONEAREST);MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);
    if(state.x==-1 && state.y==-1) {info.rcWork=work;state.x=work.right-width-px(28);state.y=work.top+px(60);}
    int x=std::clamp<LONG>(state.x,info.rcWork.left,(std::max)(info.rcWork.left,info.rcWork.right-width));
    int y=std::clamp<LONG>(state.y,info.rcWork.top,(std::max)(info.rcWork.top,info.rcWork.bottom-height));
    mainWindow=CreateWindowExW(WS_EX_APPWINDOW|(state.topmost?WS_EX_TOPMOST:0),L"FloatingTodoMain",L"浮光待办",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN,x,y,width,height,nullptr,nullptr,instance,nullptr);
    require(mainWindow!=nullptr,L"无法创建待办窗口。");
    SetWindowPos(mainWindow,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    int corner=2;DwmSetWindowAttribute(mainWindow,33,&corner,sizeof(corner));
    require(CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"FloatingTodoCompact",L"浮光待办 · 收起",WS_POPUP|WS_CLIPCHILDREN,x,y,cp(304),cp(66),nullptr,nullptr,instance,nullptr)!=nullptr,L"无法创建收起窗口。");
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");addTray();
    foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foregroundEvent,0,0,WINEVENT_OUTOFCONTEXT);
    require(foregroundHook!=nullptr,L"无法启用前台软件检测。");
    desktopHook=SetWinEventHook(EVENT_OBJECT_CLOAKED,EVENT_OBJECT_UNCLOAKED,nullptr,desktopEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
    require(desktopHook!=nullptr,L"无法启用虚拟桌面切换检测。");
    require(SetTimer(mainWindow,1,250,nullptr)!=0,L"无法启动前台状态检查。");
    ready=true;evaluateForeground();
    MSG message{};BOOL result;
    while((result=GetMessageW(&message,nullptr,0,0))>0) {
        if(message.message==WM_KEYDOWN && message.wParam=='N' && (GetKeyState(VK_CONTROL)&0x8000) && (message.hwnd==mainWindow || IsChild(mainWindow,message.hwnd)) && (!settingsWindow || !IsChild(settingsWindow,message.hwnd))) {selectView(0);SetFocus(input);continue;}
        if(scheduleWindow && IsDialogMessageW(scheduleWindow,&message))continue;
        if(settingsWindow && IsDialogMessageW(settingsWindow,&message))continue;
        if(IsDialogMessageW(mainWindow,&message))continue;
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    require(result!=-1,L"窗口消息处理失败。");
    stopUpdater();
    for(auto font:{bodyFont,smallFont,rowFont,doneFont,clockFont,titleFont})DeleteObject(font);
    DeleteObject(paperBrush);DeleteObject(whiteBrush);DestroyIcon(appIcon);shutdownVirtualDesktops();CloseHandle(mutex);return 0;
}
