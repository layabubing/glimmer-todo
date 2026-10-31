#pragma once
#include <windows.h>
#include <initializer_list>

void initializeVirtualDesktops();
void reconnectVirtualDesktops();
void shutdownVirtualDesktops();
bool isOnCurrentDesktop(HWND window);
bool followCurrentDesktop(std::initializer_list<HWND> windows);
