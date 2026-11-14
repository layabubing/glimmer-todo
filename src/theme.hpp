#pragma once
#include <windows.h>
#include <algorithm>

struct Palette {
    COLORREF background, surface, text, muted, accent, onAccent, selected, border, track;
};

inline Palette themePalette(int theme) {
    if (theme == 1) return {RGB(22,26,36),RGB(31,37,51),RGB(234,238,247),RGB(161,173,194),RGB(177,166,255),RGB(22,26,36),RGB(52,47,77),RGB(55,64,84),RGB(34,40,55)};
    if (theme == 2) return {RGB(237,244,239),RGB(249,252,249),RGB(36,59,49),RGB(95,120,104),RGB(53,112,82),RGB(255,255,255),RGB(218,235,224),RGB(205,224,210),RGB(221,234,226)};
    return {RGB(246,247,251),RGB(255,255,255),RGB(34,41,56),RGB(105,114,133),RGB(93,91,224),RGB(255,255,255),RGB(237,236,255),RGB(225,229,238),RGB(233,235,244)};
}

inline double transitionProgress(ULONGLONG elapsed) {
    double t = std::clamp(static_cast<double>(elapsed) / 180.0, 0.0, 1.0);
    return 1.0 - (1.0-t)*(1.0-t)*(1.0-t);
}
