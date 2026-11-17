#include "startup.hpp"
#include "model.hpp"

namespace startup {
namespace {
constexpr wchar_t RunKey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t ValueName[]=L"GlimmerTodo";

void checked(LSTATUS status,const wchar_t* message) {
    if(status!=ERROR_SUCCESS)fatal(std::wstring(message)+L"\nWindows 错误代码："+std::to_wstring(status));
}

std::wstring command() {
    wchar_t path[32768]{};
    DWORD length=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    require(length>0 && length<std::size(path),L"无法获取开机自启的程序路径。");
    auto value=L"\""+std::wstring(path,length)+L"\"";
    require(value.size()<=260,L"程序路径过长，请将浮光移动到较短路径后开启自启。");
    return value;
}
}

bool enabled() {
    DWORD bytes=0;
    auto status=RegGetValueW(HKEY_CURRENT_USER,RunKey,ValueName,RRF_RT_REG_SZ,nullptr,nullptr,&bytes);
    if(status==ERROR_FILE_NOT_FOUND)return false;
    checked(status,L"无法读取开机自启设置。");
    return bytes>sizeof(wchar_t);
}

void setEnabled(bool enable) {
    HKEY key=nullptr;
    if(enable) {
        auto value=command();
        checked(RegCreateKeyExW(HKEY_CURRENT_USER,RunKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr),L"无法打开开机自启设置。");
        auto status=RegSetValueExW(key,ValueName,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
        RegCloseKey(key);checked(status,L"无法开启开机自启。");
    } else {
        auto status=RegOpenKeyExW(HKEY_CURRENT_USER,RunKey,0,KEY_SET_VALUE,&key);
        if(status==ERROR_FILE_NOT_FOUND)return;
        checked(status,L"无法打开开机自启设置。");
        status=RegDeleteValueW(key,ValueName);RegCloseKey(key);
        if(status!=ERROR_FILE_NOT_FOUND)checked(status,L"无法关闭开机自启。");
    }
}

}
