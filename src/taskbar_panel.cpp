#include "taskbar_panel.hpp"
#include "theme.hpp"
#include "chrome.hpp"
#include <windowsx.h>

namespace {
HINSTANCE appInstance;
HWND ownerWindow, panelWindow;
chrome::Tooltip tooltip;
State* model;
UINT expandRequest, menuRequest, panelDpi = 96;
HFONT textFont;
std::wstring details;
int lastTheme = -1,lastFontPercent = 0;

int scale(int value) { return MulDiv(value, panelDpi, 96); }

void font() {
    if(textFont)DeleteObject(textFont);
    textFont=CreateFontW(-MulDiv(scale(11),model->fontPercent,100),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    require(textFont!=nullptr,L"无法创建任务栏信息字体。");
    lastFontPercent=model->fontPercent;
}

void draw(HWND window,HDC target) {
    RECT rect{};GetClientRect(window,&rect);
    HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,rect.right,rect.bottom);auto old=SelectObject(dc,bitmap);
    auto palette=themePalette(model->theme);auto brush=CreateSolidBrush(palette.background);FillRect(dc,&rect,brush);DeleteObject(brush);
    auto info=model->compactInfo(nowSeconds());auto previous=SelectObject(dc,textFont);SetBkMode(dc,TRANSPARENT);
    int middle=rect.bottom/2;
    RECT tasks{scale(6),scale(1),scale(70),middle};SetTextColor(dc,palette.text);
    DrawTextW(dc,info.tasks.c_str(),-1,&tasks,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    RECT timer{scale(72),scale(1),rect.right-scale(6),middle};SetTextColor(dc,model->pomodoro.running?palette.accent:palette.muted);
    DrawTextW(dc,info.timer.c_str(),-1,&timer,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_RIGHT|DT_NOPREFIX);
    RECT schedule{scale(6),middle,rect.right-scale(6),rect.bottom-scale(1)};SetTextColor(dc,palette.muted);
    DrawTextW(dc,info.scheduleShort.c_str(),-1,&schedule,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,previous);BitBlt(target,0,0,rect.right,rect.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}

LRESULT CALLBACK panelProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_CREATE:panelWindow=window;return 0;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_LBUTTONUP:PostMessageW(ownerWindow,expandRequest,0,0);return 0;
    case WM_CONTEXTMENU:PostMessageW(ownerWindow,menuRequest,0,0);return 0;
    case WM_DPICHANGED:panelDpi=HIWORD(wParam);font();return 0;
    case WM_SIZE: {
        RECT rect{};GetClientRect(window,&rect);HRGN region=CreateRoundRectRgn(0,0,rect.right+1,rect.bottom+1,scale(8),scale(8));
        require(region && SetWindowRgn(window,region,TRUE)!=0,L"无法设置任务栏信息形状。");return 0;
    }
    case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);draw(window,dc);EndPaint(window,&paint);return 0;}
    case WM_PRINTCLIENT:draw(window,reinterpret_cast<HDC>(wParam));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_DESTROY:
        tooltip.destroy();
        if(textFont)DeleteObject(textFont);
        textFont=nullptr;panelWindow=nullptr;details.clear();lastTheme=-1;return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}

TaskbarSlot taskbarSlot() {
    TaskbarSlot slot;slot.host=FindWindowW(L"Shell_TrayWnd",nullptr);
    if(!slot.host || !IsWindowVisible(slot.host))return {};
    RECT bounds{};if(!GetClientRect(slot.host,&bounds) || bounds.right<=bounds.bottom)return {};
    slot.dpi=GetDpiForWindow(slot.host);
    LONG limit=bounds.right;
    EnumChildWindows(slot.host,[](HWND child,LPARAM value)->BOOL {
        wchar_t name[64]{};GetClassNameW(child,name,64);
        if(lstrcmpW(name,L"Start")!=0 && lstrcmpW(name,L"ReBarWindow32")!=0 && lstrcmpW(name,L"TrayNotifyWnd")!=0)return TRUE;
        RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,GetAncestor(child,GA_ROOT),reinterpret_cast<POINT*>(&rect),2);
        if(rect.right>rect.left)*reinterpret_cast<LONG*>(value)=(std::min)(*reinterpret_cast<LONG*>(value),rect.left);
        return TRUE;
    },reinterpret_cast<LPARAM>(&limit));
    int margin=MulDiv(4,slot.dpi,96),width=MulDiv(152,slot.dpi,96);
    if(limit<width+margin*2 || bounds.bottom<margin*2+MulDiv(28,slot.dpi,96))return {};
    int height=(std::min)(MulDiv(42,slot.dpi,96),static_cast<int>(bounds.bottom)-margin*2);
    slot.bounds={margin,(bounds.bottom-height)/2,margin+width,(bounds.bottom+height)/2};
    return slot;
}

void initializeTaskbarPanel(HINSTANCE instance,HWND owner,State& state,UINT expandMessage,UINT menuMessage) {
    appInstance=instance;ownerWindow=owner;model=&state;expandRequest=expandMessage;menuRequest=menuMessage;
    WNDCLASSEXW klass{sizeof(klass)};klass.hInstance=instance;klass.lpfnWndProc=panelProc;klass.hCursor=LoadCursorW(nullptr,IDC_HAND);klass.lpszClassName=L"GlimmerTaskbarPanel";
    require(RegisterClassExW(&klass)!=0,L"无法注册任务栏信息窗口。");
}

void syncTaskbarPanel(bool visible) {
    require(model!=nullptr,L"任务栏信息尚未初始化。");
    if(panelWindow && !IsWindow(panelWindow)){panelWindow=nullptr;tooltip.destroy();}
    auto slot=visible?taskbarSlot():TaskbarSlot{};
    if(!slot.host) {
        tooltip.hide();if(panelWindow && IsWindowVisible(panelWindow))ShowWindow(panelWindow,SW_HIDE);
        return;
    }
    if(panelWindow && GetWindow(panelWindow,GW_OWNER)!=slot.host)destroyTaskbarPanel();
    if(!panelWindow) {
        panelDpi=slot.dpi;font();
        // 使用任务栏的 DPI 模式创建附属窗口，保留主窗口的缩放设置。
        auto previous=SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(slot.host));
        require(previous!=nullptr,L"无法设置任务栏信息的屏幕缩放。");
        HWND created=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"GlimmerTaskbarPanel",L"浮光待办 · 任务栏信息",WS_POPUP,0,0,1,1,slot.host,nullptr,appInstance,nullptr);
        require(SetThreadDpiAwarenessContext(previous)!=nullptr,L"无法恢复窗口缩放设置。");
        require(created!=nullptr,L"无法创建任务栏信息窗口。");
        require(SetLayeredWindowAttributes(created,0,255,LWA_ALPHA)!=FALSE,L"无法设置任务栏信息的合成绘制。");
    }
    bool fontChanged=panelDpi!=slot.dpi || lastFontPercent!=model->fontPercent;
    if(fontChanged){panelDpi=slot.dpi;font();}
    auto rect=slot.bounds;MapWindowPoints(slot.host,nullptr,reinterpret_cast<POINT*>(&rect),2);
    require(SetWindowPos(panelWindow,HWND_TOP,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE,L"无法显示任务栏信息。");
    auto info=model->compactInfo(nowSeconds());
    if(details!=info.details || lastTheme!=model->theme || fontChanged) {
        details=info.details;lastTheme=model->theme;SetWindowTextW(panelWindow,details.c_str());
        InvalidateRect(panelWindow,nullptr,FALSE);
    }
    tooltip.update(panelWindow,details,rect,model->theme,model->fontPercent);
}

void destroyTaskbarPanel() {
    if(panelWindow && IsWindow(panelWindow))DestroyWindow(panelWindow);
    else if(textFont){DeleteObject(textFont);textFont=nullptr;}
    tooltip.destroy();panelWindow=nullptr;details.clear();lastTheme=-1;
}

HWND taskbarPanelWindow() { return panelWindow; }
