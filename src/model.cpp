#include "model.hpp"
#include "../vendor/json.hpp"
#include <fstream>
#include <cwctype>
#include <ctime>
#include <chrono>

using nlohmann::json;

int64_t nowSeconds() { return static_cast<int64_t>(std::time(nullptr)); }

SYSTEMTIME localTime(int64_t timestamp) {
    ULARGE_INTEGER value{}; value.QuadPart = (timestamp + 11644473600LL) * 10000000ULL;
    FILETIME file{value.LowPart, value.HighPart}; SYSTEMTIME utc{}, local{};
    require(FileTimeToSystemTime(&file, &utc) != FALSE && SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local) != FALSE, L"日程时间无效。");
    return local;
}

int64_t timestampFromLocal(const SYSTEMTIME& local) {
    SYSTEMTIME utc{}; FILETIME file{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) || !SystemTimeToFileTime(&utc, &file)) return -1;
    ULARGE_INTEGER value{}; value.LowPart = file.dwLowDateTime; value.HighPart = file.dwHighDateTime;
    return static_cast<int64_t>(value.QuadPart / 10000000ULL) - 11644473600LL;
}

std::wstring formatDate(int64_t timestamp) {
    auto local = localTime(timestamp); wchar_t result[64];
    swprintf_s(result, L"%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute);
    return result;
}

bool State::putSchedule(uint64_t id, const std::wstring& value, int64_t due, int repeat) {
    auto text = trim(value);
    if (text.empty() || text.size() > 500 || due <= 0 || due > 253402214400LL || repeat < 0 || repeat > 2) return false;
    if (id == 0) schedules.push_back({nextId++, std::move(text), due, repeat});
    else {
        auto existing = std::find_if(schedules.begin(), schedules.end(), [id](const Schedule& event) { return event.id == id; });
        if (existing == schedules.end()) return false;
        *existing = {id, std::move(text), due, repeat};
    }
    std::stable_sort(schedules.begin(), schedules.end(), [](const Schedule& a, const Schedule& b) { return a.due < b.due; });
    return true;
}

void State::completeSchedule(uint64_t id, int64_t now) {
    for (auto& event : schedules) {
        if (event.id != id) continue;
        if (event.repeat == 0) { event.done = !event.done; return; }
        auto local = localTime(event.due);
        using namespace std::chrono;
        auto date = sys_days{year{local.wYear}/month{local.wMonth}/day{local.wDay}};
        do {
            date += days{event.repeat == 1 ? 1 : 7};
            auto next = year_month_day{date}; local.wYear = static_cast<WORD>(int(next.year())); local.wMonth = static_cast<WORD>(unsigned(next.month())); local.wDay = static_cast<WORD>(unsigned(next.day()));
            event.due = timestampFromLocal(local); require(event.due > 0, L"无法计算下一次重复日程。");
        } while (event.due <= now);
        event.notified = false; event.done = false;
        break;
    }
    std::stable_sort(schedules.begin(), schedules.end(), [](const Schedule& a, const Schedule& b) { return a.due < b.due; });
}

[[noreturn]] void fatal(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"浮光待办 · 错误", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

void require(bool condition, const std::wstring& message) {
    if (!condition) fatal(message + L"\nWindows 错误代码：" + std::to_wstring(GetLastError()));
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    require(size > 0, L"文本无法转换为 UTF-8。");
    std::string result(size, '\0');
    require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) == size, L"文本转换失败。");
    return result;
}

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    require(size > 0, L"数据文件包含无效 UTF-8。");
    std::wstring result(size, L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size) == size, L"文本转换失败。");
    return result;
}

std::wstring trim(const std::wstring& value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}

std::wstring normalizeExe(const std::wstring& value) {
    std::wstring result = trim(value);
    if (result.size() >= 2 && result.front() == L'"' && result.back() == L'"') result = result.substr(1, result.size() - 2);
    result = std::filesystem::path(result).filename().wstring();
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return result;
}

State::State() : rules{
    {L"Counter-Strike 2", L"cs2.exe"},
    {L"Counter-Strike: Global Offensive", L"csgo.exe"},
    {L"Counter-Strike 1.6 / Half-Life", L"hl.exe"},
    {L"Counter-Strike: Source", L"hl2.exe"},
    {L"原神 · 国服", L"yuanshen.exe"},
    {L"原神 · 国际服", L"genshinimpact.exe"},
    {L"崩坏：星穹铁道", L"starrail.exe"}
} {}

bool State::addTask(const std::wstring& value) {
    auto text = trim(value);
    if (text.empty() || text.size() > 500) return false;
    tasks.push_back({nextId++, std::move(text), false});
    return true;
}

bool State::editTask(uint64_t id, const std::wstring& value) {
    auto text = trim(value);
    if (text.empty() || text.size() > 500) return false;
    auto it = std::find_if(tasks.begin(), tasks.end(), [id](const Task& task) { return task.id == id; });
    if (it == tasks.end()) return false;
    it->text = std::move(text);
    return true;
}

void State::toggleTask(uint64_t id) {
    for (auto& task : tasks) if (task.id == id) { task.done = !task.done; return; }
}

void State::removeTask(uint64_t id) {
    std::erase_if(tasks, [id](const Task& task) { return task.id == id; });
}

bool State::addRule(const std::wstring& value) {
    const auto exe = normalizeExe(value);
    if (exe.size() <= 4 || exe.size() > 255 || !exe.ends_with(L".exe") || exe.find_first_of(L"<>:\"/\\|?*\r\n\t") != std::wstring::npos) return false;
    if (std::any_of(rules.begin(), rules.end(), [&](const Rule& rule) { return rule.exe == exe; })) return false;
    rules.push_back({exe, exe, true});
    return true;
}

bool State::matches(const std::wstring& value) const {
    if (!automatic) return false;
    const auto exe = normalizeExe(value);
    return std::any_of(rules.begin(), rules.end(), [&](const Rule& rule) { return rule.enabled && rule.exe == exe; });
}

void State::load(const std::filesystem::path& file) {
    if (!std::filesystem::exists(file)) return;
    std::ifstream input(file, std::ios::binary);
    require(input.good(), L"无法读取数据文件：" + file.wstring());
    json root = json::parse(input);
    require(root.at("version").get<int>() == 1, L"数据文件版本不受支持。");
    tasks.clear();
    rules.clear();
    nextId = 1;
    for (const auto& entry : root.at("tasks")) {
        Task task{entry.at("id").get<uint64_t>(), wide(entry.at("text").get<std::string>()), entry.at("done").get<bool>()};
        require(task.id > 0 && task.id < UINT64_MAX && !trim(task.text).empty() && task.text.size() <= 500, L"待办数据无效。");
        require(std::none_of(tasks.begin(), tasks.end(), [&](const Task& existing) { return existing.id == task.id; }), L"待办编号重复。");
        nextId = (std::max)(nextId, task.id + 1);
        tasks.push_back(std::move(task));
    }
    for (const auto& entry : root.at("rules")) {
        Rule rule{wide(entry.at("label").get<std::string>()), normalizeExe(wide(entry.at("exe").get<std::string>())), entry.at("enabled").get<bool>()};
        require(addRule(rule.exe), L"软件规则数据无效。");
        rules.back() = std::move(rule);
    }
    schedules.clear();
    if (root.contains("schedules")) for (const auto& entry : root.at("schedules")) {
        Schedule event{entry.at("id").get<uint64_t>(), wide(entry.at("text").get<std::string>()), entry.at("due").get<int64_t>(), entry.at("repeat").get<int>(), entry.at("done").get<bool>(), entry.at("notified").get<bool>()};
        require(event.id > 0 && event.id < UINT64_MAX && std::none_of(schedules.begin(), schedules.end(), [&](const Schedule& other) { return other.id == event.id; }), L"日程编号无效。");
        require(!trim(event.text).empty() && event.text.size() <= 500 && event.due > 0 && event.due <= 253402214400LL && event.repeat >= 0 && event.repeat <= 2, L"日程数据无效。");
        nextId = (std::max)(nextId, event.id + 1); schedules.push_back(std::move(event));
    }
    std::stable_sort(schedules.begin(), schedules.end(), [](const Schedule& a, const Schedule& b) { return a.due < b.due; });
    automatic = root.at("automatic").get<bool>();
    topmost = root.at("topmost").get<bool>();
    const auto& bounds = root.at("window");
    x = bounds.at("x").get<int>(); y = bounds.at("y").get<int>();
    width = std::clamp(bounds.at("width").get<int>(), 220, 900);
    height = std::clamp(bounds.at("height").get<int>(), 160, 1200);
    if (root.value("layoutVersion", 1) < 2) {
        width = 260;
        height = 300;
    }
}

void State::save(const std::filesystem::path& file) const {
    json root{{"version", 1}, {"layoutVersion", 2}, {"automatic", automatic}, {"topmost", topmost},
        {"window", {{"x", x}, {"y", y}, {"width", width}, {"height", height}}},
        {"tasks", json::array()}, {"rules", json::array()}, {"schedules", json::array()}};
    for (const auto& task : tasks) root["tasks"].push_back({{"id", task.id}, {"text", utf8(task.text)}, {"done", task.done}});
    for (const auto& rule : rules) root["rules"].push_back({{"label", utf8(rule.label)}, {"exe", utf8(rule.exe)}, {"enabled", rule.enabled}});
    for (const auto& event : schedules) root["schedules"].push_back({{"id", event.id}, {"text", utf8(event.text)}, {"due", event.due}, {"repeat", event.repeat}, {"done", event.done}, {"notified", event.notified}});
    std::filesystem::create_directories(file.parent_path());
    auto pending = file;
    pending += L".pending";
    const auto data = root.dump(2);
    HANDLE output = CreateFileW(pending.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(output != INVALID_HANDLE_VALUE, L"无法写入待办数据：" + pending.wstring());
    DWORD written = 0;
    require(WriteFile(output, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size(), L"写入待办数据失败。");
    require(FlushFileBuffers(output) != FALSE, L"保存待办数据失败。");
    require(CloseHandle(output) != FALSE, L"关闭数据文件失败。");
    require(MoveFileExW(pending.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE, L"更新待办数据失败。");
}
