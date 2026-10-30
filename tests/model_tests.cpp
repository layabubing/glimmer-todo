#include "../src/model.hpp"
#include <iostream>
#include <fstream>

int checks = 0;

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
    ++checks;
}

int main() {
    State state;
    check(state.matches(L"CS2.EXE"), "CS2 detection");
    check(state.matches(L"C:\\Games\\csgo.exe"), "CSGO detection");
    check(state.matches(L"hl.exe"), "CS 1.6 detection");
    check(state.matches(L"hl2.exe"), "CS Source detection");
    check(state.matches(L"YuanShen.exe"), "Genshin CN detection");
    check(state.matches(L"GenshinImpact.exe"), "Genshin global detection");
    check(state.matches(L"StarRail.exe"), "Star Rail detection");
    check(!state.matches(L"cs2.exe.other.exe"), "exact filename matching");
    check(!state.matches(L"explorer.exe"), "unlisted process");
    state.automatic = false;
    check(!state.matches(L"cs2.exe"), "automatic toggle");
    state.automatic = true;
    state.rules[0].enabled = false;
    check(!state.matches(L"cs2.exe"), "disabled rule");
    check(state.addRule(L"\"C:\\Program Files\\Example\\NOTEPAD.EXE\""), "quoted executable path");
    check(state.matches(L"Notepad.exe"), "custom process");
    check(!state.addRule(L"notepad.exe"), "duplicate custom rule");
    check(!state.addRule(L"program.txt"), "invalid extension");
    check(!state.addRule(L"bad?.exe"), "invalid filename");
    check(!state.addTask(L" \t\r\n"), "blank task");
    check(state.addTask(L"  编写待办 📝  "), "unicode task");
    check(state.tasks[0].text == L"编写待办 📝", "trim task");
    check(!state.addTask(std::wstring(501,L'a')), "task length limit");
    check(state.addTask(L"第二件事情"), "second task");
    auto first = state.tasks[0].id;
    auto second = state.tasks[1].id;
    state.toggleTask(first);
    check(state.tasks[0].done, "complete task");
    state.toggleTask(first);
    check(!state.tasks[0].done, "reopen task");
    check(state.editTask(first,L"修改后的待办"), "edit task");
    check(!state.editTask(first,L" "), "empty edit");
    check(!state.editTask(999,L"文字"), "unknown task edit");
    state.toggleTask(second);
    state.x=100;state.y=200;state.width=400;state.height=600;state.topmost=false;
    auto directory=std::filesystem::current_path()/L".work"/L"model-tests";
    auto file=directory/L"state.json";
    state.save(file);
    State loaded;loaded.load(file);
    check(loaded.tasks.size()==2, "persist task count");
    check(loaded.tasks[0].text==L"修改后的待办", "persist unicode text");
    check(loaded.tasks[1].done, "persist completion");
    check(!loaded.rules[0].enabled, "persist disabled rule");
    check(loaded.matches(L"NOTEPAD.EXE"), "persist custom rule");
    check(!loaded.topmost && loaded.x==100 && loaded.y==200 && loaded.width==400 && loaded.height==600, "persist window preferences");
    check(loaded.addTask(L"重启后新增"), "add after reload");
    check(loaded.tasks.back().id > second, "unique ids after reload");
    loaded.removeTask(first);
    check(loaded.tasks.size()==2 && loaded.tasks[0].id==second, "delete task");
    loaded.save(file);
    State replaced;replaced.load(file);
    check(replaced.tasks.size()==2 && replaced.tasks[0].id==second, "atomic replacement");
    check(!std::filesystem::exists(file.wstring()+L".pending"), "no partial file after save");
    check(wide(utf8(L"中文与 emoji 📝"))==L"中文与 emoji 📝", "UTF8 round trip");
    std::cout << checks << " checks passed\n";
    return 0;
}
