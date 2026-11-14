#include "updater.hpp"
#include "model.hpp"
#include "version.hpp"
#include "../vendor/json.hpp"
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <array>
#include <fstream>
#include <mutex>
#include <thread>

namespace {
std::mutex statusMutex;
UpdateStatus status;
std::thread worker;
std::filesystem::path executable() {
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    require(length > 0 && length < std::size(path), L"无法读取程序路径。");
    return std::wstring(path, length);
}
std::filesystem::path staging() { return executable().parent_path() / L".glimmer-update"; }
void setStatus(UpdateStatus value) { std::lock_guard lock(statusMutex); status = std::move(value); }
void error(const std::wstring& value) { setStatus({UpdatePhase::Error, value}); }
struct Internet {
    HINTERNET handle = nullptr;
    ~Internet() { if (handle) WinHttpCloseHandle(handle); }
};
bool get(const std::wstring& url, std::string& data, size_t limit) {
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts); parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) { error(L"更新下载地址无效。"); return false; }
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength); path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet session{WinHttpOpen(L"GlimmerTodo/1.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0)};
    if (!session.handle) { error(L"无法初始化更新网络连接。"); return false; }
    WinHttpSetTimeouts(session.handle, 10000, 10000, 15000, 30000);
    Internet connection{WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0)};
    if (!connection.handle) { error(L"无法连接 GitHub。"); return false; }
    Internet request{WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr, nullptr, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.handle || !WinHttpSendRequest(request.handle, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request.handle, nullptr)) {
        error(L"更新请求失败，网络错误代码：" + std::to_wstring(GetLastError())); return false;
    }
    DWORD code = 0, length = sizeof(code);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &length, nullptr) || code != 200) {
        error(L"GitHub 返回 HTTP " + std::to_wstring(code) + L"，请稍后重新检查。"); return false;
    }
    std::array<char, 32768> buffer{}; DWORD read = 0;
    for (;;) {
        if (!WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) { error(L"读取更新数据失败。"); return false; }
        if (!read) return true;
        if (data.size() + read > limit) { error(L"更新文件超过允许大小。"); return false; }
        data.append(buffer.data(), read);
    }
}
std::string sha256(const std::filesystem::path& file) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0, L"无法初始化 SHA256。");
    require(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0, L"无法创建 SHA256 校验。");
    std::ifstream input(file, std::ios::binary); require(input.good(), L"无法读取更新文件。");
    std::array<char, 32768> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        require(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()), 0) == 0, L"更新文件校验失败。");
    }
    require(input.eof(), L"读取更新文件失败。");
    std::array<unsigned char, 32> digest{};
    require(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0, L"无法完成 SHA256 校验。");
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
    std::string result;
    for (auto byte : digest) { result += "0123456789abcdef"[byte >> 4]; result += "0123456789abcdef"[byte & 15]; }
    return result;
}
void launch(const std::filesystem::path& program, const std::wstring& arguments) {
    std::wstring command = L"\"" + program.wstring() + L"\" " + arguments;
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    require(CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, program.parent_path().c_str(), &startup, &process) != FALSE, L"无法启动更新程序。");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
}

bool newerVersion(const std::string& candidate, const std::string& current) {
    auto a = wide(candidate.starts_with('v') ? candidate.substr(1) : candidate);
    auto b = wide(current.starts_with('v') ? current.substr(1) : current);
    return StrCmpLogicalW(a.c_str(), b.c_str()) > 0;
}

UpdateStatus updateStatus() { std::lock_guard lock(statusMutex); return status; }
void stopUpdater() { if (worker.joinable()) worker.join(); }
void checkUpdate() {
    auto current = updateStatus();
    if (current.phase == UpdatePhase::Checking || current.phase == UpdatePhase::Downloading) return;
    stopUpdater(); setStatus({UpdatePhase::Checking, L"正在检查 GitHub Release…"});
    worker = std::thread([] {
        std::string body;
        if (!get(L"https://api.github.com/repos/layabubing/glimmer-todo/releases/latest", body, 2 * 1024 * 1024)) return;
        auto release = nlohmann::json::parse(body, nullptr, false);
        if (release.is_discarded() || !release.is_object() || !release.contains("tag_name") || !release["tag_name"].is_string() || !release.contains("assets") || !release["assets"].is_array()) { error(L"GitHub Release 数据无效。"); return; }
        auto version = release["tag_name"].get<std::string>();
        if (!newerVersion(version, AppVersion)) { setStatus({UpdatePhase::Current, L"当前版本已是最新：" + wide(AppVersion)}); return; }
        for (const auto& asset : release["assets"]) {
            if (asset.value("name", std::string{}) != "GlimmerTodo.exe") continue;
            if (!asset.contains("digest") || !asset["digest"].is_string() || !asset.contains("browser_download_url") || !asset["browser_download_url"].is_string() || !asset.contains("size") || !asset["size"].is_number_unsigned()) break;
            auto digest = asset["digest"].get<std::string>(); auto url = asset["browser_download_url"].get<std::string>(); auto size = asset["size"].get<uint64_t>();
            if (!digest.starts_with("sha256:") || digest.size() != 71 || !url.starts_with("https://github.com/layabubing/glimmer-todo/releases/download/") || size == 0 || size > 16 * 1024 * 1024) break;
            setStatus({UpdatePhase::Available, L"发现新版本 " + wide(version), version, url, digest.substr(7), size}); return;
        }
        error(L"该 Release 缺少可校验的 GlimmerTodo.exe 附件。");
    });
}
void downloadUpdate() {
    auto available = updateStatus(); if (available.phase != UpdatePhase::Available) return;
    stopUpdater(); setStatus({UpdatePhase::Downloading, L"正在下载并校验更新…"});
    worker = std::thread([available] {
        std::string body;
        if (!get(wide(available.url), body, 16 * 1024 * 1024)) return;
        if (body.size() != available.size) { error(L"更新文件大小不符。"); return; }
        std::filesystem::create_directories(staging());
        std::ofstream output(staging() / L"download.exe", std::ios::binary | std::ios::trunc);
        output.write(body.data(), static_cast<std::streamsize>(body.size())); output.close();
        if (!output.good()) { error(L"无法保存更新，请检查程序目录的写入权限。"); return; }
        if (sha256(staging() / L"download.exe") != available.digest) { error(L"更新文件 SHA256 不符，已停止安装。"); return; }
        auto downloaded = available; downloaded.phase = UpdatePhase::Ready; downloaded.message = L"校验完成，点击安装并重启。"; setStatus(std::move(downloaded));
    });
}
void installUpdate() {
    auto downloaded = updateStatus(); require(downloaded.phase == UpdatePhase::Ready, L"更新尚未准备完成。"); stopUpdater();
    auto helper = staging() / L"apply.exe";
    require(CopyFileW(executable().c_str(), helper.c_str(), FALSE) != FALSE, L"无法创建更新助手。");
    std::wstring eventName = L"Local\\GlimmerTodo.Update." + std::to_wstring(GetCurrentProcessId());
    HANDLE started = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()); require(started != nullptr, L"无法初始化更新助手。");
    launch(helper, L"--apply-update " + std::to_wstring(GetCurrentProcessId()) + L" \"" + executable().wstring() + L"\" " + wide(downloaded.digest));
    require(WaitForSingleObject(started, 10000) == WAIT_OBJECT_0, L"更新助手未能启动。"); CloseHandle(started);
}
bool runUpdateHelper() {
    int count = 0; LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    require(arguments != nullptr, L"无法读取程序参数。");
    if (count < 2 || std::wstring(arguments[1]) != L"--apply-update") { LocalFree(arguments); return false; }
    require(count == 5, L"更新助手参数无效。");
    DWORD parent = std::stoul(arguments[2]); auto target = std::filesystem::path(arguments[3]); auto digest = utf8(arguments[4]); LocalFree(arguments);
    auto downloaded = executable().parent_path() / L"download.exe";
    require(target.is_absolute() && target.parent_path() == executable().parent_path().parent_path(), L"更新目标路径无效。");
    require(sha256(downloaded) == digest, L"安装前 SHA256 校验失败。");
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parent);
    require(process != nullptr, L"无法等待旧版本退出。");
    std::wstring eventName = L"Local\\GlimmerTodo.Update." + std::to_wstring(parent);
    HANDLE started = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str()); require(started != nullptr, L"更新会话已经结束。");
    SetEvent(started); CloseHandle(started);
    require(WaitForSingleObject(process, 120000) == WAIT_OBJECT_0, L"旧版本未能退出，更新已停止。"); CloseHandle(process);
    auto backup = target; backup += L".previous";
    require(ReplaceFileW(target.c_str(), downloaded.c_str(), backup.c_str(), 0, nullptr, nullptr) != FALSE, L"替换程序失败，旧版本保留在原位置或 .previous 文件中。");
    launch(target, L""); return true;
}
