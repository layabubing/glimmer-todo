#pragma once
#include "model.hpp"

struct TaskbarSlot {
    HWND host = nullptr;
    RECT bounds{};
    UINT dpi = 96;
};

TaskbarSlot taskbarSlot();
void initializeTaskbarPanel(HINSTANCE instance, HWND owner, State& state, UINT expandMessage, UINT menuMessage);
void syncTaskbarPanel(bool visible);
void destroyTaskbarPanel();
HWND taskbarPanelWindow();
