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

int Pomodoro::duration() const { return (phase == 0 ? focusMinutes : phase == 1 ? shortMinutes : longMinutes) * 60; }
int Pomodoro::secondsLeft(int64_t now) const { return running ? static_cast<int>(std::clamp<int64_t>(deadline - now, 0, 180 * 60)) : remaining; }

bool Pomodoro::tick(int64_t now) {
    auto local = localTime(now); int today = local.wYear * 10000 + local.wMonth * 100 + local.wDay;
    bool changed = false;
    if (date != today) { date = today; todayCount = 0; changed = true; }
    if (!running || now < deadline) return changed;
    if (phase == 0) {
        ++todayCount; cycle = (cycle + 1) % 4; phase = cycle == 0 ? 2 : 1;
    } else phase = 0;
    remaining = duration(); running = false; deadline = 0; noticePending = true;
    return true;
}

void Pomodoro::toggle(int64_t now) {
    tick(now);
    if (running) { remaining = secondsLeft(now); running = false; deadline = 0; }
    else { deadline = now + remaining; running = true; noticePending = false; }
}
void Pomodoro::reset() { phase = 0; cycle = 0; running = false; deadline = 0; remaining = duration(); noticePending = false; }
void Pomodoro::skip() { phase = phase == 0 ? 1 : 0; running = false; deadline = 0; remaining = duration(); noticePending = false; }
bool Pomodoro::configure(int focus, int shortBreak, int longBreak) {
    if (running || focus < 1 || focus > 180 || shortBreak < 1 || shortBreak > 60 || longBreak < 1 || longBreak > 120) return false;
    focusMinutes = focus; shortMinutes = shortBreak; longMinutes = longBreak; remaining = duration(); return true;
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

CompactInfo State::compactInfo(int64_t now) const {
    CompactInfo info;
    auto completed = std::count_if(tasks.begin(), tasks.end(), [](const Task& task) { return task.done; });
    info.tasks = L"待办 " + std::to_wstring(completed) + L"/" + std::to_wstring(tasks.size());
    const Schedule* next = nullptr;
    for (const auto& event : schedules) if (!event.done && (!next || event.due < next->due)) next = &event;
    info.schedule = next ? (next->due <= now ? L"已到期 · " : L"日程 · ") + formatDate(next->due) + L" " + next->text : L"暂无日程";
    info.scheduleShort=L"暂无日程";
    if(next) {
        auto date=localTime(next->due),today=localTime(now);wchar_t time[32]{};
        bool sameDay=date.wYear==today.wYear && date.wMonth==today.wMonth && date.wDay==today.wDay;
        if(sameDay)swprintf_s(time,L"%02u:%02u",date.wHour,date.wMinute);
        else swprintf_s(time,L"%02u/%02u %02u:%02u",date.wMonth,date.wDay,date.wHour,date.wMinute);
        info.scheduleShort=(next->due<=now?L"到期 ":L"")+std::wstring(time)+L" "+next->text;
    }
    int seconds = pomodoro.secondsLeft(now);
    wchar_t clock[24]{};
    swprintf_s(clock, L"%02d:%02d", seconds / 60, seconds % 60);
    info.timer = std::wstring(pomodoro.phase == 0 ? L"专注 " : pomodoro.phase == 1 ? L"短休息 " : L"长休息 ") + clock;
    info.details = info.tasks + L" · " + info.timer + (pomodoro.running ? L" · 进行中" : L" · 已暂停") + L"\n" + info.schedule;
    return info;
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
    if (root.contains("pomodoro")) {
        const auto& p = root.at("pomodoro");
        pomodoro = Pomodoro{};
        require(pomodoro.configure(p.at("focusMinutes").get<int>(),p.at("shortMinutes").get<int>(),p.at("longMinutes").get<int>()),L"番茄钟时长无效。");
        pomodoro.phase=p.at("phase").get<int>(); pomodoro.cycle=p.at("cycle").get<int>(); pomodoro.todayCount=p.at("todayCount").get<int>(); pomodoro.date=p.at("date").get<int>();
        pomodoro.remaining=p.at("remaining").get<int>(); pomodoro.deadline=p.at("deadline").get<int64_t>(); pomodoro.running=p.at("running").get<bool>(); pomodoro.noticePending=p.at("noticePending").get<bool>();
        require(pomodoro.phase>=0 && pomodoro.phase<=2 && pomodoro.cycle>=0 && pomodoro.cycle<4 && pomodoro.todayCount>=0 && pomodoro.remaining>=0 && pomodoro.remaining<=10800 && (!pomodoro.running || pomodoro.deadline>0),L"番茄钟状态无效。");
    }
    automatic = root.at("automatic").get<bool>();
    topmost = root.at("topmost").get<bool>();
    theme = root.value("theme", 0);
    require(theme >= 0 && theme <= 2, L"主题设置无效。");
    int collapse = root.value("collapseMode", 0);
    require(collapse >= 0 && collapse <= 3, L"收起模式无效。");
    collapseMode = static_cast<CollapseMode>(collapse);
    capsuleX = root.value("capsuleX", -1); capsuleY = root.value("capsuleY", -1);
    const auto& bounds = root.at("window");
    x = bounds.at("x").get<int>(); y = bounds.at("y").get<int>();
    width = std::clamp(bounds.at("width").get<int>(), 240, 900);
    height = std::clamp(bounds.at("height").get<int>(), 200, 1200);
    if (root.value("layoutVersion", 1) < 2) {
        width = 260;
        height = 300;
    }
}

void State::save(const std::filesystem::path& file) const {
    json root{{"version", 1}, {"layoutVersion", 2}, {"automatic", automatic}, {"topmost", topmost}, {"theme", theme},
        {"collapseMode", static_cast<int>(collapseMode)}, {"capsuleX", capsuleX}, {"capsuleY", capsuleY},
        {"window", {{"x", x}, {"y", y}, {"width", width}, {"height", height}}},
        {"tasks", json::array()}, {"rules", json::array()}, {"schedules", json::array()}};
    for (const auto& task : tasks) root["tasks"].push_back({{"id", task.id}, {"text", utf8(task.text)}, {"done", task.done}});
    for (const auto& rule : rules) root["rules"].push_back({{"label", utf8(rule.label)}, {"exe", utf8(rule.exe)}, {"enabled", rule.enabled}});
    for (const auto& event : schedules) root["schedules"].push_back({{"id", event.id}, {"text", utf8(event.text)}, {"due", event.due}, {"repeat", event.repeat}, {"done", event.done}, {"notified", event.notified}});
    const auto& p = pomodoro;
    root["pomodoro"]={{"focusMinutes",p.focusMinutes},{"shortMinutes",p.shortMinutes},{"longMinutes",p.longMinutes},{"phase",p.phase},{"cycle",p.cycle},{"todayCount",p.todayCount},{"date",p.date},{"remaining",p.remaining},{"deadline",p.deadline},{"running",p.running},{"noticePending",p.noticePending}};
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
