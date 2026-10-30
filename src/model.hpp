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

struct State {
    std::vector<Task> tasks;
    std::vector<Rule> rules;
    bool automatic = true;
    bool topmost = true;
    int x = -1, y = -1, width = 260, height = 300;
    uint64_t nextId = 1;

    State();
    bool addTask(const std::wstring& text);
    bool editTask(uint64_t id, const std::wstring& text);
    void toggleTask(uint64_t id);
    void removeTask(uint64_t id);
    bool addRule(const std::wstring& executable);
    bool matches(const std::wstring& executable) const;
    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

std::wstring trim(const std::wstring& value);
std::wstring normalizeExe(const std::wstring& value);
std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
[[noreturn]] void fatal(const std::wstring& message);
void require(bool condition, const std::wstring& message);
