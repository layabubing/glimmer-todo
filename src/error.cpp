#include "model.hpp"
#include <windowsx.h>

namespace {
std::wstring detail;
LRESULT CALLBACK errorProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);HBRUSH brush=CreateSolidBrush(RGB(246,247,251));FillRect(dc,&rect,brush);DeleteObject(brush);
        auto font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");auto previous=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(34,41,56));
        RECT title{24,20,476,50};DrawTextW(dc,L"浮光待办 · 无法继续运行",-1,&title,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        RECT text{24,65,476,215};DrawTextW(dc,detail.c_str(),-1,&text,DT_WORDBREAK|DT_NOPREFIX);
        RECT button{360,237,476,275};brush=CreateSolidBrush(RGB(93,91,224));FillRect(dc,&button,brush);DeleteObject(brush);SetTextColor(dc,RGB(255,255,255));DrawTextW(dc,L"关闭程序",-1,&button,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_NOPREFIX);
        SelectObject(dc,previous);DeleteObject(font);EndPaint(window,&paint);return 0;
    }
    if(message==WM_CLOSE || (message==WM_KEYDOWN && (wParam==VK_RETURN || wParam==VK_ESCAPE)))ExitProcess(1);
    if(message==WM_LBUTTONUP){RECT button{360,237,476,275};POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};if(PtInRect(&button,point))ExitProcess(1);}
    if(message==WM_NCHITTEST){RECT rect{};GetWindowRect(window,&rect);if(GET_Y_LPARAM(lParam)<rect.top+50)return HTCAPTION;}
    return DefWindowProcW(window,message,wParam,lParam);
}
}

[[noreturn]] void fatal(const std::wstring& message) {
    detail=message;WNDCLASSW klass{};klass.hInstance=GetModuleHandleW(nullptr);klass.lpfnWndProc=errorProc;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.lpszClassName=L"GlimmerFatal";RegisterClassW(&klass);
    RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
    HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,klass.lpszClassName,L"浮光待办",WS_POPUP,(area.right+area.left-500)/2,(area.bottom+area.top-300)/2,500,300,nullptr,nullptr,klass.hInstance,nullptr);
    if(!window){OutputDebugStringW(message.c_str());ExitProcess(1);}
    ShowWindow(window,SW_SHOW);SetForegroundWindow(window);MSG event{};
    while(GetMessageW(&event,nullptr,0,0)>0){TranslateMessage(&event);DispatchMessageW(&event);}ExitProcess(1);
}
