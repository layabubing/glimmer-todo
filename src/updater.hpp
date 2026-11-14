#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

enum class UpdatePhase { Idle, Checking, Available, Current, Downloading, Ready, Error };
struct UpdateStatus {
    UpdatePhase phase = UpdatePhase::Idle;
    std::wstring message;
    std::string version, url, digest;
    uint64_t size = 0;
};
UpdateStatus updateStatus();
void checkUpdate();
void downloadUpdate();
void stopUpdater();
void installUpdate();
bool runUpdateHelper();
bool newerVersion(const std::string& candidate, const std::string& current);
