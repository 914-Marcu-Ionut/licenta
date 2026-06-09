#include "work_analyzer.h"

#include <windows.h>
#include <iostream>
#include <vector>

static std::string wstr_to_utf8(const std::wstring& wstr)
{
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, NULL, NULL);
    return result;
}

static void scan_directory_recursive(const std::wstring& dir, std::vector<json>& files)
{
    std::wstring search = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring fullPath = dir + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_directory_recursive(fullPath, files);
        }
        else {
            LARGE_INTEGER fileSize;
            fileSize.LowPart = fd.nFileSizeLow;
            fileSize.HighPart = fd.nFileSizeHigh;

            json entry;
            entry["path"] = wstr_to_utf8(fullPath);
            entry["size_bytes"] = fileSize.QuadPart;
            files.push_back(entry);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

void snapshot_user_files(const std::wstring& root_path, EventCallback on_event)
{
    std::cout << "[WA] Taking initial file snapshot of " << wstr_to_utf8(root_path) << "\n";

    std::vector<json> files;
    scan_directory_recursive(root_path, files);

    std::cout << "[WA] Snapshot complete: " << files.size() << " files found\n";

    json detail;
    detail["root"] = wstr_to_utf8(root_path);
    detail["file_count"] = (int)files.size();
    detail["files"] = files;

    on_event("initial_snapshot", detail);
}

void work_analyzer_thread(const std::wstring& watch_path, EventCallback on_event)
{
    HANDLE hDir = CreateFileW(
        watch_path.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE) {
        std::cout << "[WA] Failed to open directory for monitoring: " << GetLastError() << "\n";
        return;
    }

    std::cout << "[WA] Work analyzer started\n";

    std::vector<BYTE> buffer(4096);

    while (true) {
        DWORD bytesReturned = 0;
        BOOL result = ReadDirectoryChangesW(
            hDir,
            buffer.data(),
            (DWORD)buffer.size(),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_CREATION,
            &bytesReturned,
            NULL,
            NULL
        );

        if (!result) {
            std::cout << "[WA] ReadDirectoryChangesW failed: " << GetLastError() << "\n";
            break;
        }

        FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)buffer.data();

        while (true) {
            std::wstring wFileName(fni->FileName, fni->FileNameLength / sizeof(WCHAR));

            int len = WideCharToMultiByte(CP_UTF8, 0, wFileName.c_str(), -1, NULL, 0, NULL, NULL);
            std::string fileName(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, wFileName.c_str(), -1, &fileName[0], len, NULL, NULL);

            std::string action;
            switch (fni->Action) {
            case FILE_ACTION_ADDED:            action = "file_created"; break;
            case FILE_ACTION_REMOVED:          action = "file_deleted"; break;
            case FILE_ACTION_MODIFIED:         action = "file_modified"; break;
            case FILE_ACTION_RENAMED_OLD_NAME: action = "file_renamed_from"; break;
            case FILE_ACTION_RENAMED_NEW_NAME: action = "file_renamed_to"; break;
            default:                           action = "file_unknown"; break;
            }

            json detail;
            detail["path"] = fileName;
            detail["action"] = action;

            on_event(action, detail);

            if (fni->NextEntryOffset == 0) break;
            fni = (FILE_NOTIFY_INFORMATION*)((BYTE*)fni + fni->NextEntryOffset);
        }
    }

    CloseHandle(hDir);
    std::cout << "[WA] Work analyzer stopped\n";
}
