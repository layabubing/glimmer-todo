#include "ui.hpp"
#include "model.hpp"
#include "../vendor/imgui/backends/imgui_impl_win32.h"
#include "../vendor/imgui/backends/imgui_impl_dx11.h"
#include <dxgi.h>
#include <cmath>
#include <vector>
#include <array>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

namespace ui {
namespace {
ID3D11Device* device;
ID3D11DeviceContext* deviceContext;
IDXGIFactory* factory;
struct FontMapping {HANDLE file=INVALID_HANDLE_VALUE,mapping=nullptr;void* data=nullptr;DWORD bytes=0;};
std::array<FontMapping,2> fonts;
Palette currentPalette=themePalette(0);
float currentScale=1;

void style(int theme,float dpi) {
    currentPalette=themePalette(theme);currentScale=dpi;
    ImGui::GetStyle()=ImGuiStyle{};auto& s=ImGui::GetStyle();const auto& c=currentPalette;
    s.WindowPadding={10,10};s.FramePadding={7,5};s.ItemSpacing={6,6};s.ItemInnerSpacing={5,4};
    s.WindowRounding=9;s.ChildRounding=7;s.FrameRounding=6;s.PopupRounding=7;s.ScrollbarRounding=5;s.GrabRounding=4;
    s.WindowBorderSize=0;s.ChildBorderSize=1;s.FrameBorderSize=0;s.PopupBorderSize=1;s.ScrollbarSize=5;
    s.TabRounding=8;s.WindowMenuButtonPosition=ImGuiDir_None;
    s.Colors[ImGuiCol_Text]=color(c.text);s.Colors[ImGuiCol_TextDisabled]=color(c.muted);
    s.Colors[ImGuiCol_WindowBg]=color(c.background);s.Colors[ImGuiCol_ChildBg]=color(c.surface);
    s.Colors[ImGuiCol_PopupBg]=color(c.surface);s.Colors[ImGuiCol_Border]=color(c.border);
    s.Colors[ImGuiCol_FrameBg]=color(c.track);s.Colors[ImGuiCol_FrameBgHovered]=color(c.selected);s.Colors[ImGuiCol_FrameBgActive]=color(c.selected);
    s.Colors[ImGuiCol_Button]=color(c.track);s.Colors[ImGuiCol_ButtonHovered]=color(c.selected);s.Colors[ImGuiCol_ButtonActive]=color(c.border);
    s.Colors[ImGuiCol_Header]=color(c.selected);s.Colors[ImGuiCol_HeaderHovered]=color(c.selected);s.Colors[ImGuiCol_HeaderActive]=color(c.border);
    s.Colors[ImGuiCol_CheckMark]=color(c.accent);s.Colors[ImGuiCol_SliderGrab]=color(c.accent);s.Colors[ImGuiCol_SliderGrabActive]=color(c.accent);
    s.Colors[ImGuiCol_Separator]=color(c.border);s.Colors[ImGuiCol_ResizeGrip]=color(c.border,0.2f);
    s.Colors[ImGuiCol_ResizeGripHovered]=color(c.accent,0.5f);s.Colors[ImGuiCol_ResizeGripActive]=color(c.accent);
    s.Colors[ImGuiCol_ScrollbarBg]=color(c.background,0);s.Colors[ImGuiCol_ScrollbarGrab]=color(c.border);
    s.Colors[ImGuiCol_ScrollbarGrabHovered]=color(c.muted);s.Colors[ImGuiCol_ScrollbarGrabActive]=color(c.accent);
    s.Colors[ImGuiCol_TextSelectedBg]=color(c.accent,0.22f);s.Colors[ImGuiCol_NavCursor]=color(c.accent);
    s.Colors[ImGuiCol_TableHeaderBg]=color(c.track);s.Colors[ImGuiCol_TableRowBg]=color(c.surface);s.Colors[ImGuiCol_TableRowBgAlt]=color(c.background);
    s.Colors[ImGuiCol_TableBorderStrong]=color(c.border);s.Colors[ImGuiCol_TableBorderLight]=color(c.border,0.5f);
    s.Colors[ImGuiCol_TitleBg]=color(c.surface);s.Colors[ImGuiCol_TitleBgActive]=color(c.surface);s.Colors[ImGuiCol_TitleBgCollapsed]=color(c.track);
    s.Colors[ImGuiCol_ModalWindowDimBg]={0,0,0,0.25f};
    s.ScaleAllSizes(dpi);s.FontSizeBase=12;s.FontScaleDpi=dpi;
}
}

void initialize() {
    D3D_FEATURE_LEVEL level;
    require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_SINGLETHREADED,nullptr,0,D3D11_SDK_VERSION,&device,&level,&deviceContext)),L"无法初始化 Direct3D 11 界面绘制。");
    IDXGIDevice* dxgi=nullptr;IDXGIAdapter* adapter=nullptr;
    require(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))),L"无法连接图形设备。");
    require(SUCCEEDED(dxgi->GetAdapter(&adapter)),L"无法获取显示适配器。");
    require(SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))),L"无法创建窗口绘制服务。");adapter->Release();dxgi->Release();
    wchar_t directory[MAX_PATH]{};require(GetWindowsDirectoryW(directory,MAX_PATH)!=0,L"无法获取 Windows 字体目录。");
    const wchar_t* names[]={L"msyh.ttc",L"seguiemj.ttf"};
    for(size_t i=0;i<fonts.size();++i) {
        auto& font=fonts[i];auto path=std::filesystem::path(directory)/L"Fonts"/names[i];
        font.file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);require(font.file!=INVALID_HANDLE_VALUE,L"无法读取界面字体。");
        font.bytes=GetFileSize(font.file,nullptr);require(font.bytes!=INVALID_FILE_SIZE && font.bytes>0,L"字体文件无效。");
        font.mapping=CreateFileMappingW(font.file,nullptr,PAGE_READONLY,0,0,nullptr);require(font.mapping!=nullptr,L"无法映射界面字体。");
        font.data=MapViewOfFile(font.mapping,FILE_MAP_READ,0,0,0);require(font.data!=nullptr,L"无法访问界面字体。");
    }
    IMGUI_CHECKVERSION();
}

void shutdown() {
    for(auto& font:fonts){if(font.data)UnmapViewOfFile(font.data);if(font.mapping)CloseHandle(font.mapping);if(font.file!=INVALID_HANDLE_VALUE)CloseHandle(font.file);font=FontMapping{};}
    if(factory){factory->Release();}
    if(deviceContext){deviceContext->Release();}
    if(device){device->Release();}
    factory=nullptr;deviceContext=nullptr;device=nullptr;
}

float scale(){return currentScale;}
float p(float value){return value*currentScale;}
ImVec2 size(float x,float y){return {p(x),p(y)};}
ImVec4 color(COLORREF value,float alpha){return {GetRValue(value)/255.0f,GetGValue(value)/255.0f,GetBValue(value)/255.0f,alpha};}
ImU32 ink(COLORREF value,float alpha){return ImGui::ColorConvertFloat4ToU32(color(value,alpha));}
const Palette& palette(){return currentPalette;}

Surface::Surface(HWND window):window_(window) {
    require(device!=nullptr,L"界面绘制尚未初始化。");
    auto previous=ImGui::GetCurrentContext();context_=ImGui::CreateContext();ImGui::SetCurrentContext(context_);auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsResizeFromEdges=false;io.ConfigInputTextEnterKeepActive=true;
    for(size_t i=0;i<fonts.size();++i) {
        ImFontConfig config;config.FontDataOwnedByAtlas=false;config.MergeMode=i>0;
        require(io.Fonts->AddFontFromMemoryTTF(fonts[i].data,static_cast<int>(fonts[i].bytes),12,&config)!=nullptr,L"无法加载界面字体。");
    }
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=2;desc.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    require(SUCCEEDED(factory->CreateSwapChain(device,&desc,&chain_)),L"无法创建窗口绘制缓冲。");
    target();require(ImGui_ImplWin32_Init(window),L"无法初始化界面输入。");require(ImGui_ImplDX11_Init(device,deviceContext),L"无法初始化界面图形。");
    SetPropW(window,L"Glimmer.Surface",reinterpret_cast<HANDLE>(this));ImGui::SetCurrentContext(previous);
}

Surface::~Surface() {
    RemovePropW(window_,L"Glimmer.Surface");auto previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(context_);
    ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(context_);
    ImGui::SetCurrentContext(previous==context_?nullptr:previous);if(target_)target_->Release();if(chain_)chain_->Release();
}

void Surface::target() {
    ID3D11Texture2D* texture=nullptr;require(SUCCEEDED(chain_->GetBuffer(0,IID_PPV_ARGS(&texture))),L"无法访问窗口缓冲。");
    require(SUCCEEDED(device->CreateRenderTargetView(texture,nullptr,&target_)),L"无法创建界面绘制目标。");texture->Release();
}
void Surface::select(){ImGui::SetCurrentContext(context_);currentScale=static_cast<float>(GetDpiForWindow(window_))/96.0f;}
void Surface::resize(UINT width,UINT height){resizeWidth_=width;resizeHeight_=height;}

bool Surface::begin(int theme) {
    select();float dpi=static_cast<float>(GetDpiForWindow(window_))/96.0f;
    if(theme_!=theme || dpi_!=dpi){style(theme,dpi);theme_=theme;dpi_=dpi;}else{currentPalette=themePalette(theme);currentScale=dpi;}
    if(resizeWidth_ && resizeHeight_) {
        deviceContext->OMSetRenderTargets(0,nullptr,nullptr);target_->Release();target_=nullptr;
        require(SUCCEEDED(chain_->ResizeBuffers(0,resizeWidth_,resizeHeight_,DXGI_FORMAT_UNKNOWN,0)),L"无法调整界面绘制缓冲。");target();resizeWidth_=resizeHeight_=0;
    }
    ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
    ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    return ImGui::Begin("##surface",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
}

void Surface::end() {
    ImGui::End();ImGui::Render();float clear[]={0,0,0,1};deviceContext->OMSetRenderTargets(1,&target_,nullptr);deviceContext->ClearRenderTargetView(target_,clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());require(SUCCEEDED(chain_->Present(0,0)),L"无法显示界面绘制结果。");
}

void Surface::print(HDC targetDc) {
    select();deviceContext->OMSetRenderTargets(1,&target_,nullptr);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ID3D11Texture2D* source=nullptr;require(SUCCEEDED(chain_->GetBuffer(0,IID_PPV_ARGS(&source))),L"无法读取界面图像。");
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ID3D11Texture2D* staging=nullptr;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),L"无法创建界面读取缓冲。");
    deviceContext->CopyResource(staging,source);source->Release();D3D11_MAPPED_SUBRESOURCE mapped{};
    require(SUCCEEDED(deviceContext->Map(staging,0,D3D11_MAP_READ,0,&mapped)),L"无法读取界面像素。");
    std::vector<unsigned char> pixels(static_cast<size_t>(desc.Width)*desc.Height*4);
    for(UINT row=0;row<desc.Height;++row)memcpy(pixels.data()+static_cast<size_t>(row)*desc.Width*4,static_cast<unsigned char*>(mapped.pData)+static_cast<size_t>(row)*mapped.RowPitch,static_cast<size_t>(desc.Width)*4);
    deviceContext->Unmap(staging,0);staging->Release();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=static_cast<LONG>(desc.Width);info.bmiHeader.biHeight=-static_cast<LONG>(desc.Height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    StretchDIBits(targetDc,0,0,desc.Width,desc.Height,0,0,desc.Width,desc.Height,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
}

bool Surface::message(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto surface=reinterpret_cast<Surface*>(GetPropW(window,L"Glimmer.Surface"));if(!surface)return false;
    auto previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(surface->context_);
    auto handled=ImGui_ImplWin32_WndProcHandler(window,message,wParam,lParam);ImGui::SetCurrentContext(previous);return handled!=0;
}

void icon(ImDrawList* draw,Icon kind,ImVec2 c,float r,ImU32 tint,float thickness) {
    r=p(r);thickness=p(thickness);auto point=[&](float x,float y){return ImVec2(c.x+x*r,c.y+y*r);};
    auto line=[&](float x1,float y1,float x2,float y2){draw->AddLine(point(x1,y1),point(x2,y2),tint,thickness);};
    switch(kind) {
    case Icon::Add:line(-0.65f,0,0.65f,0);line(0,-0.65f,0,0.65f);break;
    case Icon::Close:line(-0.5f,-0.5f,0.5f,0.5f);line(-0.5f,0.5f,0.5f,-0.5f);break;
    case Icon::Check:line(-0.65f,0,-0.15f,0.5f);line(-0.15f,0.5f,0.7f,-0.5f);break;
    case Icon::Collapse:line(-0.65f,0.1f,0.65f,0.1f);break;
    case Icon::Back:line(0.3f,-0.6f,-0.3f,0);line(-0.3f,0,0.3f,0.6f);break;
    case Icon::Arrow:line(-0.4f,-0.25f,0,0.25f);line(0,0.25f,0.4f,-0.25f);break;
    case Icon::More:for(int i=-1;i<=1;++i)draw->AddCircleFilled(point(i*0.6f,0),p(1.4f),tint);break;
    case Icon::Theme:draw->AddCircle(c,r*0.75f,tint,20,thickness);draw->AddCircleFilled(point(-0.25f,-0.25f),p(1.3f),tint);draw->AddCircleFilled(point(0.35f,-0.2f),p(1.3f),tint);draw->AddCircleFilled(point(-0.1f,0.35f),p(1.3f),tint);break;
    case Icon::Settings:draw->AddCircle(c,r*0.5f,tint,16,thickness);for(int i=0;i<8;++i){float a=i*3.14159265f/4;line(std::cos(a)*0.7f,std::sin(a)*0.7f,std::cos(a),std::sin(a));}break;
    case Icon::Calendar:draw->AddRect(point(-0.7f,-0.6f),point(0.7f,0.8f),tint,p(2),0,thickness);line(-0.65f,-0.15f,0.65f,-0.15f);line(-0.3f,-0.9f,-0.3f,-0.4f);line(0.3f,-0.9f,0.3f,-0.4f);break;
    case Icon::Timer:draw->AddCircle(c,r*0.75f,tint,22,thickness);line(0,0,0,-0.4f);line(0,0,0.35f,0.2f);break;
    case Icon::List:for(int i=-1;i<=1;++i){draw->AddCircleFilled(point(-0.7f,i*0.55f),p(1.1f),tint);line(-0.25f,i*0.55f,0.7f,i*0.55f);}break;
    case Icon::Folder:draw->AddRect(point(-0.8f,-0.5f),point(0.8f,0.65f),tint,p(2),0,thickness);line(-0.75f,-0.7f,-0.05f,-0.7f);break;
    }
}

bool iconButton(const char* id,Icon kind,const char* hint,bool selected,float extent) {
    auto pos=ImGui::GetCursorScreenPos();bool pressed=ImGui::InvisibleButton(id,size(extent,extent),ImGuiButtonFlags_EnableNav);bool hover=ImGui::IsItemHovered();auto& c=palette();auto draw=ImGui::GetWindowDrawList();
    if(hover || selected)draw->AddRectFilled(pos,{pos.x+p(extent),pos.y+p(extent)},ink(selected?c.selected:c.track),p(9));
    icon(draw,kind,{pos.x+p(extent/2),pos.y+p(extent/2)},7,ink(selected?c.accent:c.muted));
    if(ImGui::IsItemFocused())draw->AddRect(pos,{pos.x+p(extent),pos.y+p(extent)},ink(c.accent),p(9));
    if(hover && hint)ImGui::SetTooltip("%s",hint);
    return pressed;
}

bool button(const char* value,ImVec2 extent,bool primary) {
    if(primary){ImGui::PushStyleColor(ImGuiCol_Button,color(palette().accent));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,color(palette().accent,0.85f));ImGui::PushStyleColor(ImGuiCol_ButtonActive,color(palette().accent,0.7f));ImGui::PushStyleColor(ImGuiCol_Text,color(palette().onAccent));}
    bool clicked=ImGui::Button(value,extent);if(primary)ImGui::PopStyleColor(4);return clicked;
}

bool toggle(const char* id,bool& value) {
    auto pos=ImGui::GetCursorScreenPos();bool clicked=ImGui::InvisibleButton(id,size(34,20),ImGuiButtonFlags_EnableNav);if(clicked)value=!value;
    auto draw=ImGui::GetWindowDrawList();draw->AddRectFilled(pos,{pos.x+p(34),pos.y+p(20)},ink(value?palette().accent:palette().border),p(10));
    draw->AddCircleFilled({pos.x+p(value?24:10),pos.y+p(10)},p(7),ink(value?palette().onAccent:palette().surface));
    if(ImGui::IsItemFocused()){draw->AddRect({pos.x-p(2),pos.y-p(2)},{pos.x+p(36),pos.y+p(22)},ink(palette().accent),p(11));}
    return clicked;
}
void text(const char* value,float fontSize,COLORREF tint){ImGui::PushFont(nullptr,fontSize);ImGui::PushStyleColor(ImGuiCol_Text,color(tint==CLR_INVALID?palette().text:tint));ImGui::TextUnformatted(value);ImGui::PopStyleColor();ImGui::PopFont();}
void muted(const char* value){text(value,11,palette().muted);}
void section(const char* title,const char* subtitle){text(title,13);if(subtitle)muted(subtitle);}
bool card(const char* id,ImVec2 extent,bool selected){ImGui::PushStyleColor(ImGuiCol_ChildBg,color(selected?palette().selected:palette().surface));return ImGui::BeginChild(id,extent,ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);}
void endCard(){ImGui::EndChild();ImGui::PopStyleColor();}
}
