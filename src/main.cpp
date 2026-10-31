#include "model.hpp"
#include "virtual_desktop.hpp"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>

namespace {
constexpr COLORREF Paper = RGB(248, 249, 246), Ink = RGB(39, 57, 49);
constexpr COLORREF Muted = RGB(118, 130, 121), Green = RGB(54, 108, 80), Soft = RGB(231, 239, 231);
constexpr UINT TrayMessage = WM_APP + 1, ForegroundMessage = WM_APP + 2;
constexpr UINT ShowRequest = WM_APP + 3;
constexpr int AddId = 101, InputId = 102, ListId = 103, SettingsId = 104, HideId = 105;
constexpr int AutoId = 201, TopId = 202, RulesId = 203, RuleInputId = 204, RuleAddId = 205, BrowseId = 206, RemoveId = 207;
constexpr int MenuShow = 301, MenuAuto = 302, MenuTop = 303, MenuSettings = 304, MenuExit = 305, MenuClear = 306;
HINSTANCE instance;
HWND mainWindow, taskList, input, addButton, settingsButton, hideButton;
HWND settingsWindow, rulesList, ruleInput, automaticBox, topmostBox;
HWND fileDialog;
HFONT bodyFont, smallFont, rowFont, doneFont;
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
    for (auto font : {bodyFont, smallFont, rowFont, doneFont}) if (font) DeleteObject(font);
    bodyFont = makeFont(12); smallFont = makeFont(10);
    rowFont = makeFont(12); doneFont = makeFont(12, FW_NORMAL, true);
    for (auto window : {input, addButton, settingsButton, hideButton}) if (window) SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
    if (taskList) { SendMessageW(taskList, WM_SETFONT, reinterpret_cast<WPARAM>(rowFont), TRUE); SendMessageW(taskList, LB_SETITEMHEIGHT, 0, px(32)); }
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
    if (message == WM_PAINT && state.tasks.empty()) {
        PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint);
        RECT rect{}; GetClientRect(window, &rect); FillRect(dc, &rect, paperBrush);
        label(dc, L"暂无待办", {0,rect.bottom / 2 - px(12),rect.right,rect.bottom / 2 + px(12)}, bodyFont, Muted, DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        EndPaint(window, &paint); return 0;
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
    HWND window = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    require(window != nullptr, L"无法创建界面控件。");
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
    return window;
}

void layout() {
    RECT bounds{}; GetClientRect(mainWindow, &bounds);
    int width = bounds.right, height = bounds.bottom;
    MoveWindow(settingsButton, width - px(84), px(6), px(38), px(26), TRUE);
    MoveWindow(hideButton, width - px(44), px(6), px(36), px(26), TRUE);
    MoveWindow(taskList, px(8), px(40), width - px(16), (std::max)(px(32), height - px(88)), TRUE);
    MoveWindow(input, px(16), height - px(33), width - px(66), px(20), TRUE);
    MoveWindow(addButton, width - px(43), height - px(36), px(28), px(26), TRUE);
    InvalidateRect(mainWindow, nullptr, TRUE);
}

void paintMain(HWND window) {
    PAINTSTRUCT paint{}; HDC target = BeginPaint(window, &paint);
    RECT rect{}; GetClientRect(window, &rect);
    HDC dc = CreateCompatibleDC(target); HBITMAP bitmap = CreateCompatibleBitmap(target, rect.right, rect.bottom);
    auto previous = SelectObject(dc, bitmap); FillRect(dc, &rect, paperBrush);
    const int width = rect.right, height = rect.bottom;
    COLORREF indicator = state.automatic ? Green : Muted;
    rounded(dc, {px(12), px(16), px(18), px(22)}, indicator, 6, indicator);
    label(dc, L"待办", {px(24), px(6), px(54), px(32)}, bodyFont, Ink);
    size_t done = std::count_if(state.tasks.begin(), state.tasks.end(), [](const Task& task) { return task.done; });
    label(dc, std::to_wstring(done) + L"/" + std::to_wstring(state.tasks.size()), {px(58), px(6), width - px(90), px(32)}, smallFont, Muted);
    rounded(dc, {px(8), height - px(42), width - px(8), height - px(7)}, RGB(255,255,255), 6, RGB(225,230,223));
    BitBlt(target, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); EndPaint(window, &paint);
}

void drawItem(const DRAWITEMSTRUCT* item) {
    if (item->CtlID == ListId) {
        if (item->itemID == static_cast<UINT>(-1) || item->itemID >= state.tasks.size()) return;
        const auto& task = state.tasks[item->itemID];
        RECT row = item->rcItem;
        fill(item->hDC, row, (item->itemState & ODS_SELECTED) ? Soft : Paper);
        fill(item->hDC, {row.left, row.bottom - 1, row.right, row.bottom}, RGB(235,238,233));
        int x = row.left + px(7), y = row.top + px(9);
        rounded(item->hDC, {x,y,x+px(14),y+px(14)}, task.done ? Green : Paper, 3, task.done ? Green : RGB(183,195,184));
        if (task.done) checkMark(item->hDC, x+px(2), y+px(2), RGB(255,255,255));
        label(item->hDC, task.text, {row.left+px(30),row.top,row.right-px(26),row.bottom}, task.done ? doneFont : rowFont, task.done ? Muted : Ink);
        label(item->hDC, L"×", {row.right-px(24),row.top,row.right-px(4),row.bottom}, bodyFont, Muted, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    } else if (item->CtlID == AddId || item->CtlID == SettingsId || item->CtlID == HideId) {
        bool primary = item->CtlID == AddId;
        COLORREF background = primary ? ((item->itemState & ODS_SELECTED) ? RGB(39,85,61) : Green) : Paper;
        rounded(item->hDC, item->rcItem, background, 5, background);
        label(item->hDC, textOf(item->hwndItem), item->rcItem, bodyFont, primary ? RGB(255,255,255) : Muted, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
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

void evaluateForeground() {
    if (!ready) return;
    HWND foreground = GetForegroundWindow();
    // 焦点切换期间的空句柄维持已有显示状态。
    if (!foreground) return;
    if (!isOnCurrentDesktop(foreground)) return;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(foreground, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return;
    bool hidden = state.matches(foregroundExe(foreground));
    bool changed = automaticallyHidden != hidden;
    automaticallyHidden = hidden;
    bool shouldShow = !manuallyHidden && !automaticallyHidden;
    if (!shouldShow) {
        if (fileDialog && IsWindowVisible(fileDialog)) ShowWindow(fileDialog, SW_HIDE);
        if (settingsWindow && IsWindowVisible(settingsWindow)) ShowWindow(settingsWindow, SW_HIDE);
        if (IsWindowVisible(mainWindow)) ShowWindow(mainWindow, SW_HIDE);
    } else if (followCurrentDesktop({mainWindow, settingsWindow, fileDialog})) {
        if (!IsWindowVisible(mainWindow)) ShowWindow(mainWindow, SW_SHOWNOACTIVATE);
        if (settingsWindow && !IsWindowVisible(settingsWindow)) ShowWindow(settingsWindow, SW_SHOWNOACTIVATE);
        if (fileDialog && !IsWindowVisible(fileDialog)) ShowWindow(fileDialog, SW_SHOWNOACTIVATE);
    }
    if (changed) updateTray();
}

void CALLBACK foregroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    PostMessageW(mainWindow, ForegroundMessage, 0, 0);
}

void CALLBACK desktopEvent(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD) {
    if (window == mainWindow) PostMessageW(mainWindow, ForegroundMessage, 0, 0);
}

void updateTray() {
    auto hint = automaticallyHidden ? L"浮光待办 · 已随前台软件隐藏" : manuallyHidden ? L"浮光待办 · 已收起" : L"浮光待办 · 双击显示";
    lstrcpynW(tray.szTip, hint, static_cast<int>(std::size(tray.szTip)));
    Shell_NotifyIconW(NIM_MODIFY, &tray);
}

void addTray() {
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

UINT_PTR CALLBACK fileDialogProc(HWND window, UINT message, WPARAM, LPARAM) {
    // 使用 Windows 经典文件选择窗口，控制文件浏览时的内存占用。
    if (message == WM_INITDIALOG) fileDialog = window;
    if (message == WM_DESTROY) fileDialog = nullptr;
    return FALSE;
}

void menu(HWND owner) {
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, MenuShow, manuallyHidden ? L"显示待办" : L"收起到托盘");
    AppendMenuW(popup, MF_STRING | (state.automatic ? MF_CHECKED : 0), MenuAuto, L"自动隐藏");
    AppendMenuW(popup, MF_STRING | (state.topmost ? MF_CHECKED : 0), MenuTop, L"保持置顶");
    AppendMenuW(popup, MF_STRING, MenuSettings, L"软件检测设置…");
    AppendMenuW(popup, MF_STRING, MenuClear, L"清理已完成待办");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, MenuExit, L"退出浮光待办");
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(owner);
    TrackPopupMenu(popup, TPM_RIGHTBUTTON, point.x, point.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0); DestroyMenu(popup);
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
        refreshRules(); return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor(reinterpret_cast<HDC>(wParam), Paper); SetTextColor(reinterpret_cast<HDC>(wParam), Ink); return reinterpret_cast<LRESULT>(paperBrush);
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
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
    case WM_DESTROY: settingsWindow=nullptr; rulesList=nullptr; ruleInput=nullptr; return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

void openSettings() {
    if (settingsWindow) { ShowWindow(settingsWindow,SW_SHOWNORMAL); SetForegroundWindow(settingsWindow); return; }
    RECT owner{}; GetWindowRect(mainWindow,&owner);
    RECT rect{0,0,px(440),px(420)};
    AdjustWindowRectExForDpi(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW,dpi);
    settingsWindow=CreateWindowExW(WS_EX_TOOLWINDOW,L"FloatingTodoSettings",L"浮光待办 · 自动隐藏设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        owner.left-px(80),owner.top+px(16),rect.right-rect.left,rect.bottom-rect.top,mainWindow,nullptr,instance,nullptr);
    require(settingsWindow!=nullptr,L"无法打开设置。");
    // 使用工作区约束，保证靠近屏幕边缘时仍能看到全部设置。
    HMONITOR monitor=MonitorFromWindow(mainWindow,MONITOR_DEFAULTTONEAREST); MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info);
    RECT bounds{}; GetWindowRect(settingsWindow,&bounds);
    int x=std::clamp(bounds.left,info.rcWork.left,(std::max)(info.rcWork.left,info.rcWork.right-(bounds.right-bounds.left)));
    int y=std::clamp(bounds.top,info.rcWork.top,(std::max)(info.rcWork.top,info.rcWork.bottom-(bounds.bottom-bounds.top)));
    SetWindowPos(settingsWindow,HWND_TOP,x,y,0,0,SWP_NOSIZE); ShowWindow(settingsWindow,SW_SHOW); SetForegroundWindow(settingsWindow);
}

void command(int id) {
    switch(id) {
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
        settingsButton=child(L"BUTTON",L"设置",BS_OWNERDRAW|WS_TABSTOP,window,SettingsId);
        hideButton=child(L"BUTTON",L"收起",BS_OWNERDRAW|WS_TABSTOP,window,HideId);
        taskList=child(L"LISTBOX",L"待办列表",WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,window,ListId);
        SendMessageW(taskList,LB_SETITEMHEIGHT,0,px(32)); SetWindowSubclass(taskList,listProc,1,0);
        input=child(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,window,InputId);
        SendMessageW(input,EM_SETLIMITTEXT,500,0); SetWindowSubclass(input,inputProc,1,0);
        addButton=child(L"BUTTON",L"＋",WS_TABSTOP|BS_OWNERDRAW,window,AddId);
        cancelEdit(); refreshTasks(); layout(); return 0;
    case WM_SIZE: if(taskList)layout(); return 0;
    case WM_EXITSIZEMOVE: {
        RECT bounds{}; GetWindowRect(window,&bounds); state.x=bounds.left;state.y=bounds.top;
        state.width=dip(bounds.right-bounds.left);state.height=dip(bounds.bottom-bounds.top);save(); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info=reinterpret_cast<MINMAXINFO*>(lParam);info->ptMinTrackSize={px(220),px(160)};info->ptMaxTrackSize={px(900),px(1200)};return 0;
    }
    case WM_NCCALCSIZE: return 0;
    case WM_NCHITTEST: {
        RECT bounds{};GetWindowRect(window,&bounds);int x=GET_X_LPARAM(lParam)-bounds.left,y=GET_Y_LPARAM(lParam)-bounds.top;
        int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top,edge=px(6);
        if(y<edge)return x<edge?HTTOPLEFT:x>=width-edge?HTTOPRIGHT:HTTOP;
        if(y>=height-edge)return x<edge?HTBOTTOMLEFT:x>=width-edge?HTBOTTOMRIGHT:HTBOTTOM;
        if(x<edge)return HTLEFT;
        if(x>=width-edge)return HTRIGHT;
        if(y<px(36) && x<width-px(86))return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DPICHANGED: {
        dpi=HIWORD(wParam);fonts();auto rect=reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
        if(settingsWindow) {DestroyWindow(settingsWindow);openSettings();}return 0;
    }
    case WM_PAINT: paintMain(window);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight=px(62);return TRUE;
    case WM_DRAWITEM:drawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;
    case WM_CTLCOLORLISTBOX:SetBkColor(reinterpret_cast<HDC>(wParam),Paper);return reinterpret_cast<LRESULT>(paperBrush);
    case WM_CTLCOLOREDIT:SetBkColor(reinterpret_cast<HDC>(wParam),RGB(255,255,255));SetTextColor(reinterpret_cast<HDC>(wParam),Ink);return reinterpret_cast<LRESULT>(whiteBrush);
    case WM_COMMAND:if(LOWORD(wParam)==ListId && HIWORD(wParam)==LBN_DBLCLK)editSelected();else command(LOWORD(wParam));return 0;
    case WM_CONTEXTMENU:menu(window);return 0;
    case ForegroundMessage:evaluateForeground();return 0;
    case ShowRequest:manuallyHidden=false;evaluateForeground();updateTray();return 0;
    case WM_TIMER:evaluateForeground();return 0;
    case TrayMessage:
        if(lParam==WM_LBUTTONDBLCLK) {manuallyHidden=false;evaluateForeground();updateTray();}
        if(lParam==WM_RBUTTONUP)menu(window);
        return 0;
    case WM_QUERYENDSESSION:save();return TRUE;
    case WM_CLOSE:manuallyHidden=true;evaluateForeground();updateTray();return 0;
    case WM_DESTROY:
        ready=false;KillTimer(window,1);if(foregroundHook)UnhookWinEvent(foregroundHook);if(desktopHook)UnhookWinEvent(desktopHook);Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
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
    paperBrush=CreateSolidBrush(Paper);whiteBrush=CreateSolidBrush(RGB(255,255,255));fonts();appIcon=createIcon();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};require(InitCommonControlsEx(&controls)!=FALSE,L"无法初始化界面。");
    WNDCLASSEXW klass{sizeof(klass)};klass.lpfnWndProc=mainProc;klass.hInstance=instance;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=appIcon;klass.hIconSm=appIcon;klass.lpszClassName=L"FloatingTodoMain";
    require(RegisterClassExW(&klass)!=0,L"无法注册待办窗口。");
    klass.lpfnWndProc=settingsProc;klass.lpszClassName=L"FloatingTodoSettings";klass.hbrBackground=paperBrush;
    require(RegisterClassExW(&klass)!=0,L"无法注册设置窗口。");
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
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");addTray();
    foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foregroundEvent,0,0,WINEVENT_OUTOFCONTEXT);
    require(foregroundHook!=nullptr,L"无法启用前台软件检测。");
    desktopHook=SetWinEventHook(EVENT_OBJECT_CLOAKED,EVENT_OBJECT_UNCLOAKED,nullptr,desktopEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
    require(desktopHook!=nullptr,L"无法启用虚拟桌面切换检测。");
    require(SetTimer(mainWindow,1,250,nullptr)!=0,L"无法启动前台状态检查。");
    ready=true;evaluateForeground();
    MSG message{};BOOL result;
    while((result=GetMessageW(&message,nullptr,0,0))>0) {
        if(message.message==WM_KEYDOWN && message.wParam=='N' && (GetKeyState(VK_CONTROL)&0x8000) && (message.hwnd==mainWindow || IsChild(mainWindow,message.hwnd)) && (!settingsWindow || !IsChild(settingsWindow,message.hwnd))) {SetFocus(input);continue;}
        if(settingsWindow && IsDialogMessageW(settingsWindow,&message))continue;
        if(IsDialogMessageW(mainWindow,&message))continue;
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    require(result!=-1,L"窗口消息处理失败。");
    for(auto font:{bodyFont,smallFont,rowFont,doneFont})DeleteObject(font);
    DeleteObject(paperBrush);DeleteObject(whiteBrush);DestroyIcon(appIcon);shutdownVirtualDesktops();CloseHandle(mutex);return 0;
}
