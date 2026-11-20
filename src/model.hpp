#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

constexpr int MinTaskPriority = 0, DefaultTaskPriority = 50, MaxTaskPriority = 100;
constexpr size_t MaxTaskNoteLength = 8192;

enum class TaskRepeat { None, Daily, Weekly };

struct TaskDetails {
    int priority = DefaultTaskPriority;
    int dueDate = 0;
    TaskRepeat repeat = TaskRepeat::None;
};

struct Task {
    uint64_t id;
    std::wstring text;
    bool done;
    TaskDetails details{};
    std::wstring note;
    int completedOn = 0;
};

struct TaskImportResult {
    size_t added = 0;
    size_t invalidLine = 0;
};

struct Rule {
    std::wstring label;
    std::wstring exe;
    bool enabled = true;
};

struct Schedule {
    uint64_t id;
    std::wstring text;
    int64_t due;
    int repeat = 0;
    bool done = false;
    bool notified = false;
};

int64_t nowSeconds();
SYSTEMTIME localTime(int64_t timestamp);
int64_t timestampFromLocal(const SYSTEMTIME& local);
std::wstring formatDate(int64_t timestamp);
int localDate(int64_t timestamp);
int taskDueDate(int year, int month, int day);

struct Pomodoro {
    int focusMinutes = 25, shortMinutes = 5, longMinutes = 15;
    int phase = 0, cycle = 0, todayCount = 0, date = 0;
    int remaining = 25 * 60;
    int64_t deadline = 0;
    uint64_t taskId = 0;
    bool running = false, noticePending = false;

    int duration() const;
    int secondsLeft(int64_t now) const;
    bool tick(int64_t now);
    void toggle(int64_t now);
    void reset();
    void skip();
    bool configure(int focus, int shortBreak, int longBreak);
};

enum class CollapseMode { Capsule, TopBar, Hidden, Taskbar };
constexpr const wchar_t* CollapseNames[] = {L"胶囊", L"屏幕顶部长条", L"完全隐藏", L"任务栏"};

struct CompactInfo {
    std::wstring tasks, schedule, scheduleShort, timer, details;
};

struct TaskWindowPreferences {
    bool visible = true;
    int x = -1, y = -1, width = 260, height = 300;
};

struct State {
    std::vector<Task> tasks;
    std::vector<Rule> rules;
    std::vector<Schedule> schedules;
    Pomodoro pomodoro;
    bool automatic = true;
    bool topmost = true;
    bool multipleTaskWindows = false;
    std::array<TaskWindowPreferences, 2> repeatWindows;
    int theme = 0;
    int fontPercent = 100;
    CollapseMode collapseMode = CollapseMode::Capsule;
    int capsuleX = -1, capsuleY = -1;
    int x = -1, y = -1, width = 260, height = 300;
    uint64_t nextId = 1;

    State();
    bool addTask(const std::wstring& text, TaskDetails details = {});
    TaskImportResult importTasks(const std::wstring& text);
    const Task* findTask(uint64_t id) const;
    bool editTask(uint64_t id, const std::wstring& text, std::optional<TaskDetails> details = std::nullopt);
    bool editTaskDetails(uint64_t id, TaskDetails details);
    bool setTaskNote(uint64_t id, std::wstring note);
    void toggleTask(uint64_t id, int today = localDate(nowSeconds()));
    bool refreshTaskRepeats(int today);
    void removeTask(uint64_t id);
    bool bindPomodoroTask(uint64_t id);
    bool addRule(const std::wstring& executable);
    bool matches(const std::wstring& executable) const;
    bool putSchedule(uint64_t id, const std::wstring& text, int64_t due, int repeat);
    void completeSchedule(uint64_t id, int64_t now);
    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
    CompactInfo compactInfo(int64_t now) const;
private:
    void sortTasks();
};

std::wstring trim(const std::wstring& value);
std::wstring normalizeExe(const std::wstring& value);
std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
[[noreturn]] void fatal(const std::wstring& message);
void require(bool condition, const std::wstring& message);
