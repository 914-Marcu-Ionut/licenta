#pragma once

#include <string>
#include <functional>
#include "json.hpp"

using json = nlohmann::json;

using EventCallback = std::function<void(const std::string& type, const json& detail)>;

void work_analyzer_thread(const std::wstring& watch_path, EventCallback on_event);
void snapshot_user_files(const std::wstring& root_path, EventCallback on_event);
