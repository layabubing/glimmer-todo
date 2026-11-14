#pragma once
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

struct Task {
    uint64_t id;
    std::wstring text;
    bool done;
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

struct Pomodoro {
    int focusMinutes = 25, shortMinutes = 5, longMinutes = 15;
    int phase = 0, cycle = 0, todayCount = 0, date = 0;
    int remaining = 25 * 60;
    int64_t deadline = 0;
    bool running = false, noticePending = false;

    int duration() const;
    int secondsLeft(int64_t now) const;
    bool tick(int64_t now);
    void toggle(int64_t now);
    void reset();
    void skip();
    bool configure(int focus, int shortBreak, int longBreak);
};

struct State {
    std::vector<Task> tasks;
    std::vector<Rule> rules;
    std::vector<Schedule> schedules;
    Pomodoro pomodoro;
    bool automatic = true;
    bool topmost = true;
    int theme = 0;
    int x = -1, y = -1, width = 260, height = 300;
    uint64_t nextId = 1;

    State();
    bool addTask(const std::wstring& text);
    bool editTask(uint64_t id, const std::wstring& text);
    void toggleTask(uint64_t id);
    void removeTask(uint64_t id);
    bool addRule(const std::wstring& executable);
    bool matches(const std::wstring& executable) const;
    bool putSchedule(uint64_t id, const std::wstring& text, int64_t due, int repeat);
    void completeSchedule(uint64_t id, int64_t now);
    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

std::wstring trim(const std::wstring& value);
std::wstring normalizeExe(const std::wstring& value);
std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
[[noreturn]] void fatal(const std::wstring& message);
void require(bool condition, const std::wstring& message);
