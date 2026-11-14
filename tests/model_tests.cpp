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
    auto now=nowSeconds();
    auto local=localTime(now); local.wSecond=0; local.wMilliseconds=0;
    check(timestampFromLocal(local)>0 && localTime(timestampFromLocal(local)).wHour==local.wHour,"local schedule time round trip");
    State calendar;
    check(calendar.putSchedule(0,L"日程一",now+60,0),"add schedule");
    auto eventId=calendar.schedules.front().id;
    check(calendar.putSchedule(0,L"较早日程",now-60,1),"add repeating schedule");
    check(calendar.schedules.front().text==L"较早日程","schedule ordering");
    auto repeatId=calendar.schedules.front().id;
    calendar.completeSchedule(repeatId,now);
    check(calendar.schedules.back().due>now && !calendar.schedules.back().done,"repeat advances to future");
    check(localTime(calendar.schedules.back().due).wHour==localTime(now-60).wHour,"repeat preserves local hour");
    calendar.completeSchedule(eventId,now);
    check(calendar.schedules.front().done,"complete one-time schedule");
    check(calendar.putSchedule(eventId,L"已编辑",now+120,2),"edit schedule");
    check(!calendar.schedules.front().done && !calendar.schedules.front().notified,"edited schedule resets reminder");
    check(!calendar.putSchedule(0,L" ",now,0),"reject empty schedule");
    check(!calendar.putSchedule(0,L"无效日期",-1,0),"reject invalid date");
    Pomodoro timer;
    check(timer.duration()==1500 && timer.secondsLeft(now)==1500,"default focus duration");
    timer.toggle(now);
    check(timer.running && timer.secondsLeft(now+10)==1490,"focus countdown");
    timer.toggle(now+10);
    check(!timer.running && timer.remaining==1490,"pause focus");
    timer.toggle(now+20);
    check(timer.deadline==now+1510,"resume keeps remainder");
    check(timer.tick(timer.deadline),"finish focus");
    check(timer.phase==1 && timer.todayCount==1 && timer.cycle==1 && !timer.running && timer.noticePending,"short break transition");
    check(!timer.tick(now+1511) && timer.todayCount==1,"completion counted once");
    timer.toggle(now+1511);timer.tick(timer.deadline);
    check(timer.phase==0 && timer.todayCount==1,"break does not count focus");
    for(int i=0;i<3;++i){timer.toggle(now);timer.tick(timer.deadline);if(i<2){timer.toggle(now);timer.tick(timer.deadline);}}
    check(timer.phase==2 && timer.cycle==0 && timer.todayCount==4,"fourth focus selects long break");
    timer.reset();timer.skip();
    check(timer.todayCount==4 && timer.phase==1,"skip preserves completed count");
    check(timer.configure(30,6,20) && timer.remaining==360,"custom durations");
    check(!timer.configure(0,6,20) && !timer.configure(30,61,20),"duration bounds");
    timer.reset();timer.toggle(now);calendar.pomodoro=timer;
    calendar.save(directory/L"calendar.json");State resumed;resumed.load(directory/L"calendar.json");
    check(resumed.schedules.size()==2 && resumed.schedules.front().text==L"已编辑","persist schedules");
    check(resumed.pomodoro.running && resumed.pomodoro.deadline==timer.deadline && resumed.pomodoro.focusMinutes==30,"persist timer deadline");
    resumed.pomodoro.tick(timer.deadline+3600);
    check(!resumed.pomodoro.running && resumed.pomodoro.todayCount==5,"resume overdue timer completes one session");
    std::cout << checks << " checks passed\n";
    return 0;
}
