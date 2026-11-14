#include "chrome.hpp"
#include "model.hpp"
#include "theme.hpp"
#include <windowsx.h>
#include <algorithm>

namespace chrome {
namespace {
HINSTANCE app;
HWND popup,targetWindow;
std::vector<MenuItem> rows;
int popupTheme=0,popupDpi=96,highlight=-1;

int scaled(int value,int dpi){return MulDiv(value,dpi,96);}
void fill(HDC dc,RECT rect,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}
void label(HDC dc,const std::wstring& text,RECT rect,int dpi,COLORREF color,UINT flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS) {
    auto font=CreateFontW(-scaled(12,dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    auto old=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text.c_str(),-1,&rect,flags|DT_NOPREFIX);SelectObject(dc,old);DeleteObject(font);
}
void shape(HWND window,int dpi) {
    RECT rect{};GetClientRect(window,&rect);auto region=CreateRoundRectRgn(0,0,rect.right+1,rect.bottom+1,scaled(16,dpi),scaled(16,dpi));
    require(region && SetWindowRgn(window,region,TRUE),L"无法设置弹出界面形状。");
}

LRESULT CALLBACK tipProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto tip=reinterpret_cast<Tooltip*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){tip=static_cast<Tooltip*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(tip));}
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_PAINT || message==WM_PRINTCLIENT) {
        PAINTSTRUCT paint{};auto dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(wParam);RECT rect{};GetClientRect(window,&rect);auto c=themePalette(tip->theme);fill(dc,rect,c.surface);
        InflateRect(&rect,-scaled(12,tip->dpi),-scaled(10,tip->dpi));label(dc,tip->content,rect,tip->dpi,c.text,DT_WORDBREAK);
        if(message==WM_PAINT){EndPaint(window,&paint);}
        return 0;
    }
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_DESTROY && tip)tip->window=nullptr;
    return DefWindowProcW(window,message,wParam,lParam);
}

void choose() {
    if(highlight<0 || highlight>=static_cast<int>(rows.size()) || !rows[highlight].id)return;
    int id=rows[highlight].id;PostMessageW(targetWindow,WM_COMMAND,id,0);DestroyWindow(popup);
}

LRESULT CALLBACK menuProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_PAINT || message==WM_PRINTCLIENT) {
        PAINTSTRUCT paint{};auto dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(wParam);RECT rect{};GetClientRect(window,&rect);auto c=themePalette(popupTheme);fill(dc,rect,c.surface);
        for(size_t i=0;i<rows.size();++i) {
            RECT row{scaled(6,popupDpi),scaled(6+static_cast<int>(i)*30,popupDpi),rect.right-scaled(6,popupDpi),scaled(36+static_cast<int>(i)*30,popupDpi)};
            if(static_cast<int>(i)==highlight && rows[i].id)fill(dc,row,c.selected);
            auto text=row;text.left+=scaled(27,popupDpi);label(dc,rows[i].title,text,popupDpi,rows[i].id?c.text:c.muted);
            if(rows[i].checked){auto box=row;box.left+=scaled(8,popupDpi);box.right=box.left+scaled(14,popupDpi);label(dc,L"✓",box,popupDpi,c.accent);}
        }
        if(message==WM_PAINT){EndPaint(window,&paint);}
        return 0;
    }
    if(message==WM_MOUSEMOVE){int next=(GET_Y_LPARAM(lParam)-scaled(6,popupDpi))/scaled(30,popupDpi);if(next!=highlight){highlight=next;InvalidateRect(window,nullptr,FALSE);}return 0;}
    if(message==WM_LBUTTONUP){choose();return 0;}
    if(message==WM_KEYDOWN) {
        if(wParam==VK_ESCAPE){DestroyWindow(window);return 0;}
        if(wParam==VK_RETURN || wParam==VK_SPACE){choose();return 0;}
        if(wParam==VK_DOWN || wParam==VK_UP){int direction=wParam==VK_DOWN?1:-1;do{highlight=highlight<0?(direction>0?0:static_cast<int>(rows.size())-1):(highlight+direction+static_cast<int>(rows.size()))%static_cast<int>(rows.size());}while(!rows[highlight].id);InvalidateRect(window,nullptr,FALSE);return 0;}
    }
    if(message==WM_ACTIVATE && LOWORD(wParam)==WA_INACTIVE){DestroyWindow(window);return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){popup=nullptr;return 0;}
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(window,message,wParam,lParam);
}
}

void initialize(HINSTANCE instance) {
    app=instance;WNDCLASSEXW klass{sizeof(klass)};klass.hInstance=instance;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.lpfnWndProc=tipProc;klass.lpszClassName=L"GlimmerTooltip";
    require(RegisterClassExW(&klass)!=0,L"无法注册信息提示界面。");klass.lpfnWndProc=menuProc;klass.lpszClassName=L"GlimmerMenu";
    require(RegisterClassExW(&klass)!=0,L"无法注册菜单界面。");
}

Tooltip::~Tooltip(){destroy();}
void Tooltip::hide(){hoverSince_=0;if(window)ShowWindow(window,SW_HIDE);}
void Tooltip::destroy(){if(window)DestroyWindow(window);window=nullptr;}
void Tooltip::update(HWND owner,const std::wstring& text,RECT hoverBounds,int selectedTheme) {
    POINT cursor{};GetCursorPos(&cursor);
    if(!IsWindowVisible(owner) || !PtInRect(&hoverBounds,cursor)){hide();return;}
    if(!hoverSince_){hoverSince_=GetTickCount64();return;}
    if(GetTickCount64()-hoverSince_<550)return;
    int nextDpi=static_cast<int>(GetDpiForWindow(owner));bool changed=content!=text || theme!=selectedTheme || dpi!=nextDpi;content=text;theme=selectedTheme;dpi=nextDpi;
    if(!window) {
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,L"GlimmerTooltip",L"",WS_POPUP,0,0,1,1,owner,nullptr,app,this);
        require(window!=nullptr,L"无法创建信息提示界面。");changed=true;
    }
    int width=scaled(330,dpi);MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&info);
    HDC dc=GetDC(owner);auto font=CreateFontW(-scaled(12,dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");auto old=SelectObject(dc,font);
    RECT measured{0,0,width-scaled(24,dpi),0};DrawTextW(dc,content.c_str(),-1,&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);DeleteObject(font);ReleaseDC(owner,dc);
    int height=(std::min)(static_cast<int>(measured.bottom)+scaled(20,dpi),static_cast<int>(info.rcWork.bottom-info.rcWork.top));
    int x=std::clamp(static_cast<int>(cursor.x)+scaled(10,dpi),static_cast<int>(info.rcWork.left),(std::max)(static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-width));
    int y=cursor.y-height-scaled(12,dpi);if(y<info.rcWork.top)y=cursor.y+scaled(20,dpi);
    SetWindowPos(window,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);shape(window,dpi);if(changed)InvalidateRect(window,nullptr,FALSE);
}

void menu(HWND target,POINT point,const std::vector<MenuItem>& items,int theme) {
    closeMenu();targetWindow=target;rows=items;popupTheme=theme;highlight=-1;popupDpi=static_cast<int>(GetDpiForWindow(target));
    if(popupDpi==0)popupDpi=96;
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&info);
    int width=scaled(230,popupDpi),height=scaled(12+static_cast<int>(rows.size())*30,popupDpi);
    int x=std::clamp(static_cast<int>(point.x),static_cast<int>(info.rcWork.left),(std::max)(static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-width));
    int y=std::clamp(static_cast<int>(point.y),static_cast<int>(info.rcWork.top),(std::max)(static_cast<int>(info.rcWork.top),static_cast<int>(info.rcWork.bottom)-height));
    popup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"GlimmerMenu",L"浮光菜单",WS_POPUP,x,y,width,height,target,nullptr,app,nullptr);
    require(popup!=nullptr,L"无法打开菜单。");shape(popup,popupDpi);ShowWindow(popup,SW_SHOW);SetForegroundWindow(popup);SetFocus(popup);
}
void closeMenu(){if(popup)DestroyWindow(popup);}
HWND menuWindow(){return popup;}
}
