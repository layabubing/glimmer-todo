#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace chrome {
struct MenuItem { int id;std::wstring title;bool checked=false; };
void initialize(HINSTANCE instance);
void menu(HWND target,POINT point,const std::vector<MenuItem>& items,int theme,int fontPercent=100);
void closeMenu();
HWND menuWindow();

class Tooltip {
public:
    ~Tooltip();
    void update(HWND owner,const std::wstring& text,RECT hoverBounds,int theme,int fontPercent=100);
    void hide();
    void destroy();
    HWND window=nullptr;
    std::wstring content;
    int theme=0,dpi=96,fontPercent=100;
private:
    ULONGLONG hoverSince_=0;
};
}
