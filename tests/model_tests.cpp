#include "../src/model.hpp"
#include "../src/theme.hpp"
#include "../vendor/json.hpp"
#include <iostream>
#include <fstream>

int checks = 0;

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
    ++checks;
}

int main() {
    State state;
    State imported;imported.addTask(L"已有任务");auto initialId=imported.nextId;
    auto batch=imported.importTasks(L"  第一条任务 \r\n\n\t\r\n第二条任务 📝\n第一条任务\n");
    check(batch.added==3 && batch.invalidLine==0 && imported.tasks.size()==4,"batch import ignores blank lines and retains duplicates");
    check(imported.tasks[1].text==L"第一条任务" && imported.tasks[2].text==L"第二条任务 📝" && !imported.tasks[3].done,"batch import preserves Unicode, order and pending status");
    check(imported.tasks[1].id==initialId && imported.tasks[3].id==initialId+2,"batch import allocates consecutive ids");
    auto beforeInvalid=imported.nextId;
    auto invalidBatch=imported.importTasks(L"正常任务\n\n"+std::wstring(501,L'字')+L"\n最后一条");
    check(invalidBatch.added==0 && invalidBatch.invalidLine==3 && imported.tasks.size()==4 && imported.nextId==beforeInvalid,"invalid batch reports source line without partial import");
    check(imported.importTasks(L"\r\n \t\n").added==0 && imported.tasks.size()==4,"empty batch changes nothing");
    check(imported.importTasks(std::wstring(500,L'字')).added==1,"batch accepts task length boundary");
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
    check(state.tasks.back().id==first && state.tasks.back().done, "send completed task to the bottom");
    state.toggleTask(first);
    check(!state.tasks[0].done, "reopen task");
    check(state.editTask(first,L"修改后的待办"), "edit task");
    check(!state.editTask(first,L" "), "empty edit");
    check(!state.editTask(999,L"文字"), "unknown task edit");
    state.toggleTask(second);
    state.x=100;state.y=200;state.width=400;state.height=600;state.topmost=false;
    auto directory=std::filesystem::current_path()/L".work"/L"model-tests";
    auto file=directory/L"state.json";
    imported.fontPercent=150;imported.save(directory/L"imported.json");State restoredImport;restoredImport.load(directory/L"imported.json");
    check(restoredImport.tasks.size()==5 && restoredImport.tasks[2].text==L"第二条任务 📝" && restoredImport.fontPercent==150,"batch tasks and font proportion survive restart");
    state.save(file);
    State loaded;loaded.load(file);
    check(loaded.tasks.size()==2, "persist task count");
    check(loaded.tasks[0].text==L"修改后的待办", "persist unicode text");
    check(loaded.tasks[1].done, "persist completion");
    check(!loaded.rules[0].enabled, "persist disabled rule");
    check(loaded.matches(L"NOTEPAD.EXE"), "persist custom rule");
    check(!loaded.topmost && loaded.x==100 && loaded.y==200 && loaded.width==400 && loaded.height==600, "persist window preferences");
    auto previousLayout=directory/L"previous-layout.json";
    {
        std::ifstream input(file);
        auto data=nlohmann::json::parse(input);
        data.erase("fontPercent");
        for (auto& task : data["tasks"]) { task.erase("priorityScore"); task.erase("dueDate"); task.erase("note"); }
        data["pomodoro"].erase("taskId");
        data["layoutVersion"]=3;data["window"]["width"]=300;data["window"]["height"]=380;
        std::ofstream output(previousLayout);output<<data;
    }
    State migrated;migrated.load(previousLayout);
    check(migrated.width==260 && migrated.height==300,"apply compact dimensions when upgrading layout");
    check(migrated.fontPercent==100,"existing data uses default font proportion");
    check(migrated.tasks.front().details.priority==DefaultTaskPriority && migrated.tasks.front().details.dueDate==0
        && migrated.pomodoro.taskId==0,"existing data has normal priority, no due date and no timer binding");
    check(migrated.findTask(first)->note.empty() && migrated.findTask(second)->note.empty(),"existing tasks start with empty notes");
    check(migrated.x==100 && migrated.y==200 && migrated.tasks[0].text==loaded.tasks[0].text && migrated.rules.size()==loaded.rules.size(),"preserve position and data when upgrading layout");
    migrated.width=320;migrated.height=360;migrated.save(previousLayout);State resized;resized.load(previousLayout);
    check(resized.width==320 && resized.height==360,"preserve user dimensions after layout upgrade");
    check(loaded.addTask(L"重启后新增"), "add after reload");
    auto addedId=loaded.tasks[1].id;
    check(addedId > second, "unique ids after reload");
    loaded.removeTask(first);
    check(loaded.tasks.size()==2 && loaded.tasks.front().id==addedId && loaded.tasks.back().id==second, "delete task");
    loaded.save(file);
    State replaced;replaced.load(file);
    check(replaced.tasks.size()==2 && replaced.tasks.front().id==addedId && replaced.tasks.back().id==second, "atomic replacement");
    check(!std::filesystem::exists(file.wstring()+L".pending"), "no partial file after save");
    check(wide(utf8(L"中文与 emoji 📝"))==L"中文与 emoji 📝", "UTF8 round trip");
    for(int theme=0;theme<3;++theme) {
        state.theme=theme;state.save(directory/L"theme.json");State themed;themed.load(directory/L"theme.json");
        check(themed.theme==theme,"theme survives restart");
        check(themePalette(theme).text!=themePalette(theme).surface,"theme text has distinct color");
    }
    check(transitionProgress(0)==0.0 && transitionProgress(180)==1.0 && transitionProgress(1000)==1.0,"animation endpoints");
    check(transitionProgress(45)<transitionProgress(90)&&transitionProgress(90)<transitionProgress(135),"animation progresses forward");
    for(int mode=0;mode<4;++mode) {
        state.collapseMode=static_cast<CollapseMode>(mode);state.capsuleX=-320;state.capsuleY=64;
        state.save(directory/L"collapse.json");State compact;compact.load(directory/L"collapse.json");
        check(compact.collapseMode==state.collapseMode && compact.capsuleX==-320 && compact.capsuleY==64,"persist collapse mode and position");
    }
    auto now=nowSeconds();
    State noted;
    auto noteId=noted.nextId;
    check(noted.addTask(L"整理资料",{82,20280229}) && noted.addTask(L"另一项任务"),"set up independent task notes");
    auto note=L"第一行 📝\n\n  第二行记录细节";
    check(noted.setTaskNote(noteId,note) && !noted.setTaskNote(noteId,std::wstring(MaxTaskNoteLength+1,L'字'))
        && !noted.setTaskNote(noted.nextId,note),"save multiline notes and reject invalid changes");
    check(noted.editTask(noteId,L"整理项目资料") && noted.editTaskDetails(noteId,{60,20280229}),"edit task title and priority independently of its note");
    noted.toggleTask(noteId);
    auto noteFile=directory/L"task-notes.json";
    noted.save(noteFile);
    State restoredNotes;restoredNotes.load(noteFile);
    const auto* restoredNote=restoredNotes.findTask(noteId);
    check(restoredNote && restoredNote->note==note && restoredNote->done && restoredNote->details.dueDate==20280229
        && restoredNotes.tasks.front().note.empty(),"restore exact multiline note for the correct task after reordering and restart");
    check(restoredNotes.setTaskNote(noteId,L"") && restoredNotes.findTask(noteId)->note.empty(),"clear an existing note");
    State prioritized;
    int leapDay=taskDueDate(2028,2,29);
    check(leapDay==20280229 && taskDueDate(2027,2,29)==0 && taskDueDate(2028,1,257)==0,"validate civil due dates");
    check(prioritized.addTask(L"普通任务") && prioritized.addTask(L"低优先级",{MinTaskPriority,leapDay})
        && prioritized.addTask(L"高优先级",{MaxTaskPriority,leapDay}),"create tasks with priority and due date");
    auto highId=prioritized.tasks.front().id;
    auto normalId=prioritized.tasks[1].id;
    auto lowId=prioritized.tasks.back().id;
    check(prioritized.tasks.front().details.priority==MaxTaskPriority && prioritized.tasks.back().details.priority==MinTaskPriority,"sort by descending priority");
    check(prioritized.editTask(normalId,L"提高优先级",TaskDetails{MaxTaskPriority,leapDay})
        && prioritized.tasks.front().id==normalId && prioritized.tasks[1].id==highId,"sort edited tasks by priority then creation order");
    check(prioritized.importTasks(L"导入普通任务").added==1 && prioritized.tasks.back().id==lowId,"sort batch imports at normal priority");
    check(!prioritized.editTask(normalId,L"无效日期",TaskDetails{MaxTaskPriority,20280230})
        && prioritized.tasks.front().text==L"提高优先级","reject invalid due date without changing task");
    check(!prioritized.editTaskDetails(lowId,{MaxTaskPriority+1,leapDay}) && prioritized.tasks.back().details.priority==MinTaskPriority,"reject out-of-range priority");
    check(prioritized.editTaskDetails(highId,{84,leapDay}) && prioritized.tasks[1].details.priority==84
        && prioritized.tasks[1].text==L"高优先级","edit numeric priority without changing task text");
    check(prioritized.bindPomodoroTask(lowId) && prioritized.tasks.front().id==lowId
        && !prioritized.bindPomodoroTask(0xFFFF),"move even the lowest priority task to the top when bound");
    check(prioritized.bindPomodoroTask(0) && prioritized.tasks.front().id==normalId
        && prioritized.bindPomodoroTask(lowId),"restore numeric order on unbind");
    prioritized.pomodoro.toggle(now);
    prioritized.save(directory/L"prioritized.json");
    State restoredTasks;restoredTasks.load(directory/L"prioritized.json");
    check(restoredTasks.tasks.front().details.dueDate==leapDay && restoredTasks.tasks.front().details.priority==MinTaskPriority
        && restoredTasks.pomodoro.taskId==lowId && restoredTasks.pomodoro.running,"restore numeric priority, due date and running bound timer");
    check(restoredTasks.editTask(lowId,L"更新专注任务") && restoredTasks.tasks.front().details.dueDate==leapDay
        && restoredTasks.compactInfo(now).details.find(L"更新专注任务")!=std::wstring::npos,"rename bound task while keeping its metadata");
    restoredTasks.toggleTask(lowId);
    check(restoredTasks.tasks.back().id==lowId && restoredTasks.pomodoro.taskId==0
        && restoredTasks.pomodoro.running && !restoredTasks.bindPomodoroTask(lowId),"completing a bound task moves it to the bottom and keeps the timer running");
    restoredTasks.toggleTask(normalId);
    check(restoredTasks.tasks[2].id==normalId && restoredTasks.tasks[2].done,"completed high priority follows pending lower priorities");
    restoredTasks.toggleTask(normalId);
    check(restoredTasks.tasks.front().id==normalId && !restoredTasks.tasks.front().done,"restoring a task returns it to the correct priority position");
    check(restoredTasks.bindPomodoroTask(highId) && restoredTasks.tasks.front().id==highId,"switch the running timer to another pending task");
    restoredTasks.removeTask(highId);
    check(restoredTasks.pomodoro.taskId==0 && restoredTasks.pomodoro.running,"deleting the bound task preserves the countdown");
    auto legacyPriorityFile=directory/L"legacy-priority.json";
    {
        std::ifstream input(directory/L"prioritized.json");
        auto data=nlohmann::json::parse(input);
        for(auto& task:data["tasks"]) {
            auto id=task.at("id").get<uint64_t>();
            task.erase("priorityScore");
            task["priority"]=id==lowId?0:id==highId?2:1;
        }
        std::ofstream output(legacyPriorityFile);output<<data;
    }
    State legacyPriorities;legacyPriorities.load(legacyPriorityFile);
    check(legacyPriorities.findTask(lowId)->details.priority==25 && legacyPriorities.findTask(normalId)->details.priority==50
        && legacyPriorities.findTask(highId)->details.priority==75 && legacyPriorities.tasks.front().id==lowId,"migrate all legacy priorities and retain the bound task order");
    State compact;
    check(compact.collapseMode==CollapseMode::Capsule && compact.compactInfo(now).tasks==L"待办 0/0" && compact.compactInfo(now).schedule==L"暂无日程","compact defaults");
    compact.addTask(L"准备资料");compact.addTask(L"完成记录");compact.toggleTask(compact.tasks.back().id);
    compact.putSchedule(0,L"过去的日程",now-60,0);auto past=compact.schedules.front().id;
    compact.putSchedule(0,L"将来的日程",now+60,0);
    check(compact.compactInfo(now).tasks==L"待办 1/2" && compact.compactInfo(now).schedule.find(L"已到期")!=std::wstring::npos,"compact completion and overdue event");
    compact.completeSchedule(past,now);
    check(compact.compactInfo(now).schedule.find(L"将来的日程")!=std::wstring::npos,"compact skips completed schedule");
    check(compact.compactInfo(now).scheduleShort.find(L"将来的日程")!=std::wstring::npos && compact.compactInfo(now).scheduleShort.size()<compact.compactInfo(now).schedule.size(),"short taskbar schedule keeps event text");
    compact.pomodoro.toggle(now);
    check(compact.compactInfo(now+61).timer==L"专注 23:59" && compact.compactInfo(now+61).details.find(L"进行中")!=std::wstring::npos,"compact live countdown");
    compact.pomodoro.toggle(now+61);
    check(compact.compactInfo(now+100).timer==L"专注 23:59" && compact.compactInfo(now+100).details.find(L"已暂停")!=std::wstring::npos,"compact paused countdown");
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
