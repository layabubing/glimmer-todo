#pragma once
#include "theme.hpp"
#include "../vendor/imgui/imgui.h"
#include <windows.h>
#include <d3d11.h>
#include <filesystem>

namespace ui {
enum class Icon { Add, Check, Close, Settings, Theme, Collapse, More, Back, Calendar, Timer, List, Folder, Arrow };

void initialize();
void shutdown();
float scale();
float p(float value);
ImVec2 size(float x,float y);
ImVec4 color(COLORREF value,float alpha=1.0f);
ImU32 ink(COLORREF value,float alpha=1.0f);
const Palette& palette();
void icon(ImDrawList* draw,Icon kind,ImVec2 center,float radius,ImU32 tint,float thickness=1.6f);
bool iconButton(const char* id,Icon kind,const char* hint,bool selected=false,float extent=30);
bool button(const char* text,ImVec2 extent={},bool primary=false);
bool toggle(const char* id,bool& value);
void text(const char* value,float fontSize=13,COLORREF tint=CLR_INVALID);
void muted(const char* value);
void section(const char* title,const char* subtitle=nullptr);
bool card(const char* id,ImVec2 extent,bool selected=false);
void endCard();

class Surface {
public:
    explicit Surface(HWND window);
    ~Surface();
    Surface(const Surface&)=delete;
    Surface& operator=(const Surface&)=delete;
    bool begin(int theme);
    void end();
    void print(HDC target);
    void resize(UINT width,UINT height);
    void select();
    ImGuiContext* context() const { return context_; }
    HWND window() const { return window_; }
    static bool message(HWND window,UINT message,WPARAM wParam,LPARAM lParam);
private:
    void target();
    HWND window_;
    ImGuiContext* context_;
    IDXGISwapChain* chain_ = nullptr;
    ID3D11RenderTargetView* target_ = nullptr;
    UINT resizeWidth_=0,resizeHeight_=0;
    int theme_=-1;
    float dpi_=0;
};
}
