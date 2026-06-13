// client.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>   // FIRST
#include <windows.h>
#include <ws2tcpip.h>

#include <fwpmu.h>
#include <sddl.h>

#include <lm.h>
#include <userenv.h>
#include <shellapi.h>
#include <ntsecapi.h>

#include <tlhelp32.h>

#include <iostream>
#include <fstream>
#include <thread>
#include <vector>

#include "json.hpp"

#pragma comment(lib, "Netapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "fwpuclnt.lib")
#pragma comment(lib, "Ws2_32.lib")

using namespace std;
using json = nlohmann::json;

const wstring UiCommunicationPipe = L"ExamAppUiPipe";


struct InstalledProgram {
    std::string name;
    std::string version;
    std::string display_icon;
};


std::vector<InstalledProgram> get_local_programs();
int start_exam(const std::string& registered_name, const std::string& run_id, const std::string& backend_host, std::string& error_message);

const GUID EXAM_SUBLAYER_GUID =
{ 0x12345678, 0x1234, 0x1234, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22 } };

const GUID MY_PROVIDER_GUID =
{ 0x87654321, 0x4321, 0x4321, { 0xbb, 0xaa, 0xdd, 0xcc, 0xee, 0xff, 0x22, 0x11 } };


static const UINT32 DNS_SERVER_1 = ntohl(inet_addr("8.8.8.8"));
static const UINT32 DNS_SERVER_2 = ntohl(inet_addr("1.1.1.1"));

bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    HANDLE token = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation;
    DWORD size;

    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
        isAdmin = elevation.TokenIsElevated;
    }

    CloseHandle(token);
    return isAdmin;
}

bool initialize_user_profile(const wchar_t* username, const wchar_t* password)
{
    HANDLE hToken = NULL;

    CreateDirectoryW(L"C:\\ProgramData\\ExamApp", NULL);

    if (!LogonUserW(
        username,
        L".",
        password,
        LOGON32_LOGON_INTERACTIVE,
        LOGON32_PROVIDER_DEFAULT,
        &hToken
    )) {
        std::wcout << L"LogonUser failed: " << GetLastError() << std::endl;
        return false;
    }

    std::wcout << L"LogonUser OK\n";

    PROFILEINFOW profileInfo = { 0 };
    profileInfo.dwSize = sizeof(PROFILEINFOW);
    profileInfo.lpUserName = (LPWSTR)username;

    if (!LoadUserProfileW(hToken, &profileInfo)) {
        std::wcout << L"LoadUserProfile failed: " << GetLastError() << std::endl;
        CloseHandle(hToken);
        return false;
    }

    std::wcout << L"LoadUserProfile OK\n";

    wchar_t profilePath[MAX_PATH];
    DWORD profilePathSize = MAX_PATH;

    if (GetUserProfileDirectoryW(hToken, profilePath, &profilePathSize)) {
        std::wcout << L"Profile path: " << profilePath << std::endl;
    }
    else {
        std::wcout << L"GetUserProfileDirectoryW failed: "
            << GetLastError() << std::endl;
        wcscpy_s(profilePath, L"C:\\Users\\Default");
    }

    LPVOID env = NULL;

    if (!CreateEnvironmentBlock(&env, hToken, FALSE)) {
        std::wcout << L"CreateEnvironmentBlock failed: " << GetLastError() << std::endl;
        UnloadUserProfile(hToken, profileInfo.hProfile);
        CloseHandle(hToken);
        return false;
    }

    std::wcout << L"CreateEnvironmentBlock OK\n";

    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;

    PROCESS_INFORMATION pi = { 0 };

    wchar_t cmdLine[] =
        L"cmd.exe /c "
        L"whoami > C:\\ProgramData\\ExamApp\\whoami.txt";

    if (!CreateProcessWithLogonW(
        username,
        L".",
        password,
        LOGON_WITH_PROFILE,
        NULL,
        cmdLine,
        CREATE_UNICODE_ENVIRONMENT,
        env,
        profilePath,
        &si,
        &pi
    )) {
        std::wcout << L"CreateProcessWithLogonW explorer failed: "
            << GetLastError() << std::endl;

        DestroyEnvironmentBlock(env);
        UnloadUserProfile(hToken, profileInfo.hProfile);
        CloseHandle(hToken);
        return false;
    }

    DWORD sessionId = 0;
    if (ProcessIdToSessionId(pi.dwProcessId, &sessionId)) {
        std::wcout << L"Explorer PID: " << pi.dwProcessId << std::endl;
        std::wcout << L"Explorer Session ID: " << sessionId << std::endl;
    }
    else {
        std::wcout << L"ProcessIdToSessionId failed: "
            << GetLastError() << std::endl;
    }

    std::wcout << L"Explorer started and left running.\n";

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // IMPORTANT:
    // For this experiment, do NOT unload the profile immediately.
    // Explorer is still using it.
    //
    // DestroyEnvironmentBlock(env);
    // UnloadUserProfile(hToken, profileInfo.hProfile);
    // CloseHandle(hToken);

    return true;
}

bool add_scheduled_task_for_user(const wchar_t* username, const wchar_t* password) {
    wchar_t cmdLine[1024];

    const wchar_t* appPath = L"C:\\ProgramData\\ExamApp\\exam_app.exe";

    swprintf_s(cmdLine,
        L"cmd.exe /c "
        L"schtasks /delete /tn \"ExamApp\" /f & "
        L"schtasks /create /f "
        L"/sc ONLOGON "
        L"/tn \"ExamApp\" "
        L"/tr \"\\\"%s\\\"\" "
        L"/rl LIMITED",
        appPath
    );              

    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessWithLogonW(
        username,
        L".",
        password,
        LOGON_WITH_PROFILE,
        NULL,
        cmdLine,
        CREATE_UNICODE_ENVIRONMENT,
        NULL,
        NULL,
        &si,
        &pi
    )) {
        std::wcout << L"Failed to create scheduled task: " << GetLastError() << std::endl;
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::wcout << L"Scheduled task configured\n";
    return true;
}

bool grant_batch_logon(const wchar_t* username) {
    LSA_OBJECT_ATTRIBUTES lsaAttr = { 0 };
    LSA_HANDLE lsaHandle;

    if (LsaOpenPolicy(NULL, &lsaAttr, POLICY_ALL_ACCESS, &lsaHandle) != 0) {
        std::wcout << L"LsaOpenPolicy failed\n";
        return false;
    }

    // Convert username to SID
    DWORD sidSize = 0, domainSize = 0;
    SID_NAME_USE sidType;

    LookupAccountName(NULL, username, NULL, &sidSize, NULL, &domainSize, &sidType);

    PSID sid = (PSID)malloc(sidSize);
    wchar_t* domain = new wchar_t[domainSize];

    if (!LookupAccountName(NULL, username, sid, &sidSize, domain, &domainSize, &sidType)) {
        std::wcout << L"LookupAccountName failed\n";
        return false;
    }

    // Define privilege
    LSA_UNICODE_STRING right;
    right.Buffer = (PWSTR)L"SeBatchLogonRight";
    right.Length = wcslen(right.Buffer) * sizeof(wchar_t);
    right.MaximumLength = right.Length + sizeof(wchar_t);

    NTSTATUS status = LsaAddAccountRights(
        lsaHandle,
        sid,
        &right,
        1
    );

    if (status != 0) {
        std::wcout << L"LsaAddAccountRights failed\n";
        return false;
    }

    std::wcout << L"Batch logon right granted\n";

    LsaClose(lsaHandle);
    free(sid);
    delete[] domain;

    return true;
}

bool deploy_exam_app() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring sourceDir = std::wstring(exePath);
    sourceDir = sourceDir.substr(0, sourceDir.find_last_of(L"\\/"));

    const wchar_t* destDir = L"C:\\ProgramData\\ExamApp";

    if (!CreateDirectoryW(destDir, NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            std::wcout << L"Failed to create directory\n";
            return false;
        }
    }

    struct FileToCopy { const wchar_t* name; bool required; };
    FileToCopy files[] = {
        { L"exam_app.exe",          true },
        { L"libcrypto-3-x64.dll",   true },
        { L"libssl-3-x64.dll",      true },
    };

    for (const auto& f : files) {
        std::wstring src = sourceDir + L"\\" + f.name;
        std::wstring dst = std::wstring(destDir) + L"\\" + f.name;
        if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
            std::wcout << L"Failed to copy " << f.name << L": " << GetLastError() << std::endl;
            if (f.required) return false;
        } else {
            std::wcout << L"Deployed: " << f.name << std::endl;
        }
    }

    std::wcout << L"Exam app deployed\n";
    return true;
}

bool add_startup_for_user(const wchar_t* username, const wchar_t* password) {
    wchar_t cmdLine[800];

    const wchar_t* appPath = L"C:\\ProgramData\\ExamApp\\exam_app.exe";

    // 🔥 First delete old key, then add new one
    swprintf_s(cmdLine,
        L"cmd.exe /c "
        L"reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v ExamApp /f >nul 2>&1 & "
        L"reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v ExamApp /t REG_SZ /d \"\\\"%s\\\"\" /f",
        appPath
    );

    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessWithLogonW(
        username,
        L".",
        password,
        LOGON_WITH_PROFILE,
        NULL,
        cmdLine,
        CREATE_UNICODE_ENVIRONMENT,
        NULL,
        NULL,
        &si,
        &pi
    )) {
        std::wcout << L"Failed to set startup: " << GetLastError() << std::endl;
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::wcout << L"Startup refreshed\n";
    return true;
}


bool run_command(const std::wstring& cmd) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    wchar_t* cmdline = _wcsdup(cmd.c_str());

    BOOL ok = CreateProcessW(
        NULL,
        cmdline,
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (!ok) {
        free(cmdline);
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    free(cmdline);

    return exitCode == 0;
}

bool setup_unlock_hook(const wchar_t* username) {
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computerName, &size);

    std::wstring fullUser = std::wstring(computerName) + L"\\" + username;

    std::wstring exam_folder_path = L"C:\\ProgramData\\ExamApp";
    std::wstring exam_app_path = exam_folder_path + L"\\exam_app.exe";

    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        L"<Task version=\"1.4\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
        L"  <Triggers>\n"
        L"    <LogonTrigger>\n"
        L"      <Enabled>true</Enabled>\n"
        L"      <UserId>" + fullUser + L"</UserId>\n"
        L"    </LogonTrigger>\n"
        L"    <SessionStateChangeTrigger>\n"
        L"      <Enabled>true</Enabled>\n"
        L"      <StateChange>SessionUnlock</StateChange>\n"
        L"      <UserId>" + fullUser + L"</UserId>\n"
        L"    </SessionStateChangeTrigger>\n"
        L"  </Triggers>\n"
        L"  <Principals>\n"
        L"    <Principal id=\"Author\">\n"
        L"      <UserId>" + fullUser + L"</UserId>\n"
        L"      <LogonType>InteractiveToken</LogonType>\n"
        L"      <RunLevel>HighestAvailable</RunLevel>\n"
        L"    </Principal>\n"
        L"  </Principals>\n"
        L"  <Settings>\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
        L"    <AllowHardTerminate>true</AllowHardTerminate>\n"
        L"    <StartWhenAvailable>true</StartWhenAvailable>\n"
        L"  </Settings>\n"
        L"  <Actions Context=\"Author\">\n"
        L"    <Exec>\n"
        L"      <Command>" + exam_app_path + L"</Command>\n"
        L"      <WorkingDirectory>" + exam_folder_path + L"</WorkingDirectory>\n"
        L"    </Exec>\n"
        L"  </Actions>\n"
        L"</Task>";

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);

    std::wstring filePath = std::wstring(tempPath) + L"exam_task.xml";

    std::wofstream file(filePath);
    if (!file.is_open())
        return false;

    file << xml;
    file.close();

    run_command(L"schtasks /delete /tn \"ExamAppUnlock\" /f >nul 2>&1");

    std::wstring cmd =
        L"schtasks /create /tn \"ExamAppUnlock\" /xml \"" +
        filePath +
        L"\" /f";

    return run_command(cmd);
}


enum class PipeReadStatus {
    Ok,
    InvalidJson,
    Disconnected
};

bool WritePipeUtf8Line(HANDLE hPipe, const std::string& line)
{
    if (line.find('\n') != std::string::npos || line.find('\r') != std::string::npos) {
        return false;
    }

    std::string payload = line + "\n";
    DWORD totalWritten = 0;
    while (totalWritten < payload.size()) {
        DWORD bytesWritten = 0;
        BOOL ok = WriteFile(
            hPipe,
            payload.data() + totalWritten,
            static_cast<DWORD>(payload.size() - totalWritten),
            &bytesWritten,
            NULL
        );

        if (!ok || bytesWritten == 0) {
            return false;
        }

        totalWritten += bytesWritten;
    }

    return true;
}

bool WritePipeJson(HANDLE hPipe, const json& packet)
{
    return WritePipeUtf8Line(hPipe, packet.dump());
}

bool ReadPipeUtf8Line(HANDLE hPipe, std::string& result)
{
    result.clear();
    char ch;
    DWORD bytesRead;

    while (true)
    {
        BOOL success = ReadFile(
            hPipe,
            &ch,
            1,
            &bytesRead,
            NULL
        );

        if (!success || bytesRead == 0) {
            return false;
        }

        if (ch == '\r')
            continue;

        if (ch == '\n')
            break;

        result += ch;
    }

    // Remove UTF-8 BOM if present at line start.
    if (result.size() >= 3 &&
        (unsigned char)result[0] == 0xEF &&
        (unsigned char)result[1] == 0xBB &&
        (unsigned char)result[2] == 0xBF) {
        result.erase(0, 3);
    }

    return true;
}

PipeReadStatus ReadPipeJson(HANDLE hPipe, json& packet, std::string& rawLine)
{
    rawLine.clear();
    if (!ReadPipeUtf8Line(hPipe, rawLine)) {
        return PipeReadStatus::Disconnected;
    }

    packet = json::parse(rawLine, nullptr, false);
    if (packet.is_discarded() || !packet.is_object()) {
        return PipeReadStatus::InvalidJson;
    }

    return PipeReadStatus::Ok;
}

void pipe_client(HANDLE& hPipe)
{
    std::wcout << L"Pipe connected" << std::endl;

    while (true)
    {
        json packet;
        std::string rawLine;
        PipeReadStatus readStatus = ReadPipeJson(hPipe, packet, rawLine);

        if (readStatus == PipeReadStatus::Disconnected) {
            break;
        }

        if (readStatus == PipeReadStatus::InvalidJson) {
            json response = {
                {"type", "response"},
                {"code", 1},
                {"error", "invalid_json"}
            };
            if (!WritePipeJson(hPipe, response)) {
                std::cout << "Failed to send invalid_json response" << std::endl;
                break;
            }
            continue;
        }

        std::cout << "Received: " << packet.dump() << std::endl;

        std::string type = packet.value("type", "");
        if (type == "connect") {
            json response = {
                {"type", "response"},
                {"code", 0}
            };
            if (!WritePipeJson(hPipe, response)) {
                std::cout << "Failed to send connect response" << std::endl;
                break;
            }
            std::cout << "Sent: " << response.dump() << std::endl;
            continue;
        }
        else if (type == "start_exam") {
            auto localPrograms = get_local_programs();

            json programsJson = json::array();
            for (const auto& program : localPrograms) {
                programsJson.push_back({
                    {"name", program.name},
                    {"version", program.version},
                    {"display_icon", program.display_icon}
                    });
            }

            json response = {
                {"type", "start_exam_result"},
                {"code", 0},
                {"warning", "These applications will not be available in your exam, install them globally if you need them in the exam"},
                {"programs", programsJson}
            };
            if (!WritePipeJson(hPipe, response)) {
                std::cout << "Failed to send start_exam_result response" << std::endl;
                break;
            }
            std::cout << "Sent: " << response.dump() << std::endl;
            continue;
        }
        else if (type == "continue_to_exam") {
            std::string registeredName = "";
            if (packet.contains("registered_name") && packet["registered_name"].is_string()) {
                registeredName = packet["registered_name"].get<std::string>();
            }
            std::string runId = "";
            if (packet.contains("run_id") && packet["run_id"].is_string()) {
                runId = packet["run_id"].get<std::string>();
            }
            std::string backendHost = "";
            if (packet.contains("backend_host") && packet["backend_host"].is_string()) {
                backendHost = packet["backend_host"].get<std::string>();
            }

            if (registeredName.empty() || runId.empty() || backendHost.empty()) {
                json response = {
                    {"type", "init_exam_response"},
                    {"code", 20},
                    {"error", "registered_name, run_id and backend_host are required"}
                };
                WritePipeJson(hPipe, response);
                std::cout << "Sent error: missing required fields" << std::endl;
                continue;
            }

            std::string errorMessage;
            int code = start_exam(registeredName, runId, backendHost, errorMessage);

            json response = {
                {"type", "init_exam_response"},
                {"code", code},
                {"error", errorMessage}
            };
            if (!WritePipeJson(hPipe, response)) {
                std::cout << "Failed to send init_exam_response" << std::endl;
                break;
            }
            std::cout << "Sent: " << response.dump() << std::endl;
            continue;
        }

        json response = {
            {"type", "response"},
            {"code", 2},
            {"error", "unknown_message_type"}
        };
        if (!WritePipeJson(hPipe, response)) {
            std::cout << "Failed to send unknown_message_type response" << std::endl;
            break;
        }
        std::cout << "Sent: " << response.dump() << std::endl;

    }

    std::cout << "Pipe disconnected" << std::endl;
}

static std::atomic<bool> g_pipe_was_connected{ false };

void pipe_server_thread() {
    while (true) {
        const std::wstring pipe_path = L"\\\\.\\pipe\\" + UiCommunicationPipe;

        PSECURITY_DESCRIPTOR pSD = NULL;

        ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;WD)", // WD = Everyone, GA = Generic All
            SDDL_REVISION_1,
            &pSD,
            NULL
        );

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = pSD;
        sa.bInheritHandle = FALSE;

        HANDLE hPipe = CreateNamedPipeW(
            pipe_path.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            1024,
            1024,
            0,
            &sa   // ✅ FIX: use security attributes
        );


        if (hPipe == INVALID_HANDLE_VALUE) {
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, NULL) ?
            TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected) {
            g_pipe_was_connected = true;
            pipe_client(hPipe);
        }

        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);

        if (g_pipe_was_connected) {
            std::cout << "[PIPE] Client disconnected, exiting.\n";
            ExitProcess(0);
        }
    }
}

std::string wstring_to_utf8(const std::wstring& wstr)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
    return result;
}

std::wstring utf8_to_wstring(const std::string& str)
{
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

std::wstring get_user_sid(const std::wstring& username)
{
    std::wstring cmd =
        L"powershell.exe -Command \""
        L"(Get-WmiObject Win32_UserAccount -Filter \\\"Name='"
        + username +
        L"' AND LocalAccount=True\\\").SID | Out-File -Encoding ASCII $env:TEMP\\sid.txt\"";

    std::wcout << cmd << std::endl;

    if (!run_command(cmd)) {
        std::cout << "stricat" << std::endl;
        return L"";
    }

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring filePathW = std::wstring(tempPath) + L"sid.txt";

    std::wcout << filePathW << std::endl;

    // ✅ convert path to UTF-8
    std::string filePath = wstring_to_utf8(filePathW);

    FILE* file = nullptr;
    if (fopen_s(&file, filePath.c_str(), "r") != 0 || !file)
    {
        std::cout << "error opening file" << std::endl;
        return L"";
    }

    char buffer[256];
    std::string sidStr;

    while (fgets(buffer, sizeof(buffer), file))
    {
        std::string line = buffer;
        std::cout << "line " << line << std::endl;

        if (line.find("S-1-") != std::string::npos)
        {
            sidStr = line;
            break;
        }
    }

    fclose(file);

    std::cout << "extracted sid " << sidStr << std::endl;

    // trim
    if (!sidStr.empty())
    {
        sidStr.erase(sidStr.find_last_not_of(" \n\r\t") + 1);
    }

    return utf8_to_wstring(sidStr);
}
bool enable_firewall()
{
    return run_command(L"netsh advfirewall set allprofiles state on");
}


void print_error(const char* msg, DWORD err)
{
    std::cout << msg << " failed. Error = " << err << std::endl;
}


bool check_rules_intact()
{
    HANDLE engine = NULL;
    FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, NULL, &engine);

    // Check sublayer exists
    FWPM_SUBLAYER0* pSubLayer = NULL;
    DWORD res = FwpmSubLayerGetByKey0(engine, &EXAM_SUBLAYER_GUID, &pSubLayer);
    if (res != ERROR_SUCCESS)
    {
        std::cout << "[WD] Sublayer missing!\n";
        FwpmEngineClose0(engine);
        return false;
    }
    FwpmFreeMemory0((void**)&pSubLayer);

    // Count our filters
    HANDLE enumHandle = NULL;
    FWPM_FILTER_ENUM_TEMPLATE0 enumTemplate = {};
    enumTemplate.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    enumTemplate.enumType = FWP_FILTER_ENUM_FULLY_CONTAINED;
    enumTemplate.flags = FWP_FILTER_ENUM_FLAG_INCLUDE_BOOTTIME;
    enumTemplate.actionMask = 0xFFFFFFFF;

    UINT32 ourFilterCount = 0;
    res = FwpmFilterCreateEnumHandle0(engine, &enumTemplate, &enumHandle);
    if (res == ERROR_SUCCESS)
    {
        FWPM_FILTER0** filters = NULL;
        UINT32 count = 0;
        res = FwpmFilterEnum0(engine, enumHandle, 100, &filters, &count);
        if (res == ERROR_SUCCESS)
        {
            for (UINT32 i = 0; i < count; i++)
            {
                if (filters[i]->subLayerKey == EXAM_SUBLAYER_GUID)
                    ourFilterCount++;
            }
            FwpmFreeMemory0((void**)&filters);
        }
        FwpmFilterDestroyEnumHandle0(engine, enumHandle);
    }

    FwpmEngineClose0(engine);

    // We expect exactly 4 filters: ALLOW IP, ALLOW LOCALHOST, ALLOW DNS, BLOCK ALL
    if (ourFilterCount != 4)
    {
        std::cout << "[WD] Expected 4 filters, found " << ourFilterCount << "\n";
        return false;
    }

    return true;
}

bool setup_exam_user_wfp(const std::wstring& sidString, const std::string& backend_host)
{
    HANDLE engine = NULL;
    PSID sid = NULL;
    DWORD res;

    std::wcout << L"[+] Starting WFP setup for SID: " << sidString << std::endl;

    // Open engine
    res = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, NULL, &engine);
    if (res != ERROR_SUCCESS)
    {
        print_error("FwpmEngineOpen0", res);
        return false;
    }

    // Convert SID string to binary SID
    if (!ConvertStringSidToSidW(sidString.c_str(), &sid))
    {
        print_error("ConvertStringSidToSidW", GetLastError());
        FwpmEngineClose0(engine);
        return false;
    }

    std::cout << "[+] SID converted\n";

    // Build a security descriptor with a DACL granting FWP_ACTRL_MATCH_FILTER
    // to our target SID. This is what FWPM_CONDITION_ALE_USER_ID requires —
    // NOT a raw SID. Confirmed by:
    // https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/FWP/permitting-and-blocking-applications-and-users.md
    DWORD sidLen = GetLengthSid(sid);
    DWORD daclSize = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + sidLen;

    PACL pDacl = (PACL)LocalAlloc(LPTR, daclSize);
    if (!pDacl)
    {
        print_error("LocalAlloc(DACL)", GetLastError());
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    if (!InitializeAcl(pDacl, daclSize, ACL_REVISION))
    {
        print_error("InitializeAcl", GetLastError());
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    if (!AddAccessAllowedAce(pDacl, ACL_REVISION, FWP_ACTRL_MATCH_FILTER, sid))
    {
        print_error("AddAccessAllowedAce", GetLastError());
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    PSECURITY_DESCRIPTOR pSD = (PSECURITY_DESCRIPTOR)LocalAlloc(
        LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!pSD)
    {
        print_error("LocalAlloc(SD)", GetLastError());
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    if (!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION))
    {
        print_error("InitializeSecurityDescriptor", GetLastError());
        LocalFree(pSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    if (!SetSecurityDescriptorDacl(pSD, TRUE, pDacl, FALSE))
    {
        print_error("SetSecurityDescriptorDacl", GetLastError());
        LocalFree(pSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    // WFP requires the SD to be in self-relative (contiguous) form.
    DWORD srSDLen = 0;
    MakeSelfRelativeSD(pSD, NULL, &srSDLen);
    PSECURITY_DESCRIPTOR pSRSD = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, srSDLen);
    if (!pSRSD)
    {
        print_error("LocalAlloc(SRSD)", GetLastError());
        LocalFree(pSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    if (!MakeSelfRelativeSD(pSD, pSRSD, &srSDLen))
    {
        print_error("MakeSelfRelativeSD", GetLastError());
        LocalFree(pSRSD);
        LocalFree(pSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    LocalFree(pSD);
    pSD = NULL;

    FWP_BYTE_BLOB sdBlob = {};
    sdBlob.size = srSDLen;
    sdBlob.data = (UINT8*)pSRSD;

    std::cout << "[+] Security descriptor built\n";

    // -----------------------------------------------------------------------
    // Build visibility SD for sublayer and filters:
    //   SYSTEM    = full control
    //   Admins    = full control
    //   Everyone  = OPEN | BEGIN_READ_TXN | ENUM | READ (0xE4) only
    // -----------------------------------------------------------------------
    std::wstring sddl = L"D:(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x000000E4;;;WD)";
    std::wcout << L"[+] SDDL: " << sddl << std::endl;

    PSECURITY_DESCRIPTOR pFilterSD = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl.c_str(), SDDL_REVISION_1, &pFilterSD, NULL))
    {
        print_error("ConvertStringSecurityDescriptorToSecurityDescriptorW", GetLastError());
        LocalFree(pSRSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    std::cout << "[+] Visibility SD built\n";

    // Crack DACL out once — reused for container, sublayer, and individual filters
    BOOL daclPresent = FALSE, daclDefault = FALSE;
    PACL pVisibilityDacl = NULL;
    GetSecurityDescriptorDacl(pFilterSD, &daclPresent, &pVisibilityDacl, &daclDefault);

    // -----------------------------------------------------------------------
    // Grant Everyone read on the FILTER CONTAINER — controls who can call
    // FwpmFilterCreateEnumHandle0. Must be outside any transaction.
    // -----------------------------------------------------------------------
    res = FwpmFilterSetSecurityInfoByKey0(
        engine,
        NULL,                   // NULL = filter container
        DACL_SECURITY_INFORMATION,
        NULL, NULL, pVisibilityDacl, NULL
    );
    std::cout << "[*] Filter container SD result: " << res << "\n";
    if (res != ERROR_SUCCESS)
    {
        print_error("FwpmFilterSetSecurityInfoByKey0(container)", res);
        LocalFree(pFilterSD);
        LocalFree(pSRSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    std::cout << "[+] Filter container access granted\n";

    // -----------------------------------------------------------------------
    // Transaction 1: cleanup old filters and sublayer
    // -----------------------------------------------------------------------
    res = FwpmTransactionBegin0(engine, 0);
    if (res != ERROR_SUCCESS)
    {
        print_error("FwpmTransactionBegin0(cleanup)", res);
        LocalFree(pFilterSD);
        LocalFree(pSRSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    {
        // Helper to delete all our filters on a given layer
        auto delete_filters_on_layer = [&](const GUID& layerKey)
            {
                HANDLE enumHandle = NULL;
                FWPM_FILTER_ENUM_TEMPLATE0 enumTemplate = {};
                enumTemplate.layerKey = layerKey;
                enumTemplate.enumType = FWP_FILTER_ENUM_FULLY_CONTAINED;
                enumTemplate.flags = FWP_FILTER_ENUM_FLAG_INCLUDE_BOOTTIME;
                enumTemplate.actionMask = 0xFFFFFFFF;

                DWORD r = FwpmFilterCreateEnumHandle0(engine, &enumTemplate, &enumHandle);
                if (r != ERROR_SUCCESS) return;

                FWPM_FILTER0** filters = NULL;
                UINT32 count = 0;
                r = FwpmFilterEnum0(engine, enumHandle, 100, &filters, &count);
                if (r == ERROR_SUCCESS)
                {
                    for (UINT32 i = 0; i < count; i++)
                    {
                        if (filters[i]->subLayerKey == EXAM_SUBLAYER_GUID)
                        {
                            DWORD dr = FwpmFilterDeleteById0(engine, filters[i]->filterId);
                            std::cout << "[*] Deleted filter id=" << filters[i]->filterId
                                << " result=" << dr << "\n";
                        }
                    }
                    FwpmFreeMemory0((void**)&filters);
                }
                FwpmFilterDestroyEnumHandle0(engine, enumHandle);
            };

        // Delete filters on BOTH layers before touching the sublayer
        delete_filters_on_layer(FWPM_LAYER_ALE_AUTH_CONNECT_V4);
        delete_filters_on_layer(FWPM_LAYER_ALE_AUTH_CONNECT_V6); // ← new

        res = FwpmSubLayerDeleteByKey0(engine, &EXAM_SUBLAYER_GUID);
        std::cout << "[*] Delete sublayer result: " << res << "\n";
    }

    res = FwpmTransactionCommit0(engine);
    if (res != ERROR_SUCCESS)
    {
        print_error("FwpmTransactionCommit0(cleanup)", res);
        FwpmTransactionAbort0(engine);
        LocalFree(pFilterSD);
        LocalFree(pSRSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    std::cout << "[+] Cleanup committed\n";

    // -----------------------------------------------------------------------
    // Transaction 2: add new sublayer and filters
    // -----------------------------------------------------------------------
    res = FwpmTransactionBegin0(engine, 0);
    if (res != ERROR_SUCCESS)
    {
        print_error("FwpmTransactionBegin0(add)", res);
        LocalFree(pFilterSD);
        LocalFree(pSRSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    // Pass pFilterSD to sublayer so Everyone can read it
    FWPM_SUBLAYER0 subLayer = {};
    subLayer.subLayerKey = EXAM_SUBLAYER_GUID;
    subLayer.displayData.name = (wchar_t*)L"Exam User Sublayer";
    subLayer.weight = 0x100;

    res = FwpmSubLayerAdd0(engine, &subLayer, pFilterSD);
    if (res != ERROR_SUCCESS)
    {
        print_error("FwpmSubLayerAdd0", res);
        FwpmTransactionAbort0(engine);
        LocalFree(pFilterSD);
        LocalFree(pSRSD);
        LocalFree(pDacl);
        LocalFree(sid);
        FwpmEngineClose0(engine);
        return false;
    }

    std::cout << "[+] Sublayer added\n";

    auto ip_to_uint = [](const wchar_t* ipStr) -> UINT32
        {
            char buf[32] = {};
            wcstombs(buf, ipStr, sizeof(buf));
            unsigned long addr = inet_addr(buf);
            if (addr == INADDR_NONE) { std::cout << "Invalid IP\n"; return 0; }
            return ntohl(addr);
        };

    std::wstring wBackendHost(backend_host.begin(), backend_host.end());
    UINT32 allowIP = ip_to_uint(wBackendHost.c_str());
    UINT32 localhost = ip_to_uint(L"127.0.0.1");

    std::cout << "[+] IPs converted\n";

    auto add_filter = [&](const char* name,
        FWP_ACTION_TYPE action,
        GUID layerKey,          // ← now explicit, caller passes IPv4 or IPv6 layer
        UINT32 ip = 0,
        bool useIP = false,
        bool dns = false) -> bool
        {
            FWPM_FILTER0 filter = {};
            filter.subLayerKey = EXAM_SUBLAYER_GUID;
            filter.layerKey = layerKey;             // ← IPv4 or IPv6
            filter.action.type = action;
            filter.displayData.name = (wchar_t*)L"Exam Filter";
            filter.weight.type = FWP_UINT8;
            filter.weight.uint8 = (action == FWP_ACTION_PERMIT) ? 10 : 1;

            std::vector<FWPM_FILTER_CONDITION0> conditions;

            // USER condition
            FWPM_FILTER_CONDITION0 userCond = {};
            userCond.fieldKey = FWPM_CONDITION_ALE_USER_ID;
            userCond.matchType = FWP_MATCH_EQUAL;
            userCond.conditionValue.type = FWP_SECURITY_DESCRIPTOR_TYPE;
            userCond.conditionValue.sd = &sdBlob;
            conditions.push_back(userCond);

            // IP condition
            FWP_V4_ADDR_AND_MASK addrMask = {};
            if (useIP)
            {
                addrMask.addr = ip;
                addrMask.mask = 0xFFFFFFFF;
                FWPM_FILTER_CONDITION0 ipCond = {};
                ipCond.fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
                ipCond.matchType = FWP_MATCH_EQUAL;
                ipCond.conditionValue.type = FWP_V4_ADDR_MASK;
                ipCond.conditionValue.v4AddrMask = &addrMask;
                conditions.push_back(ipCond);
            }

            // DNS condition (UDP port 53) — now always paired with a specific IP
            if (dns)
            {
                FWPM_FILTER_CONDITION0 proto = {};
                proto.fieldKey = FWPM_CONDITION_IP_PROTOCOL;
                proto.matchType = FWP_MATCH_EQUAL;
                proto.conditionValue.type = FWP_UINT8;
                proto.conditionValue.uint8 = IPPROTO_UDP;
                conditions.push_back(proto);

                FWPM_FILTER_CONDITION0 port = {};
                port.fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
                port.matchType = FWP_MATCH_EQUAL;
                port.conditionValue.type = FWP_UINT16;
                port.conditionValue.uint16 = 53;
                conditions.push_back(port);
            }

            filter.numFilterConditions = (UINT32)conditions.size();
            filter.filterCondition = conditions.data();

            UINT64 id = 0;
            DWORD r = FwpmFilterAdd0(engine, &filter, pFilterSD, &id);
            std::cout << "[*] " << name << " -> result = " << r << "\n";
            return r == ERROR_SUCCESS;
        };

    auto abort_and_cleanup = [&]()
        {
            FwpmTransactionAbort0(engine);
            LocalFree(pFilterSD);
            LocalFree(pSRSD);
            LocalFree(pDacl);
            LocalFree(sid);
            FwpmEngineClose0(engine);
        };

    // IPv4 rules
    if (!add_filter("ALLOW IP", FWP_ACTION_PERMIT, FWPM_LAYER_ALE_AUTH_CONNECT_V4, allowIP, true, false)) { std::cout << "[!] Failed at ALLOW IP\n";        abort_and_cleanup(); return false; }
    if (!add_filter("ALLOW LOCALHOST", FWP_ACTION_PERMIT, FWPM_LAYER_ALE_AUTH_CONNECT_V4, localhost, true, false)) { std::cout << "[!] Failed at ALLOW LOCALHOST\n"; abort_and_cleanup(); return false; }
    if (!add_filter("ALLOW DNS 8.8.8.8", FWP_ACTION_PERMIT, FWPM_LAYER_ALE_AUTH_CONNECT_V4, DNS_SERVER_1, true, true)) { std::cout << "[!] Failed at ALLOW DNS 8.8.8.8\n"; abort_and_cleanup(); return false; }
    if (!add_filter("ALLOW DNS 1.1.1.1", FWP_ACTION_PERMIT, FWPM_LAYER_ALE_AUTH_CONNECT_V4, DNS_SERVER_2, true, true)) { std::cout << "[!] Failed at ALLOW DNS 1.1.1.1\n"; abort_and_cleanup(); return false; }
    if (!add_filter("BLOCK ALL IPv4", FWP_ACTION_BLOCK, FWPM_LAYER_ALE_AUTH_CONNECT_V4, 0, false, false)) { std::cout << "[!] Failed at BLOCK ALL IPv4\n";  abort_and_cleanup(); return false; }
    if (!add_filter("BLOCK ALL IPv6", FWP_ACTION_BLOCK, FWPM_LAYER_ALE_AUTH_CONNECT_V6, 0, false, false)) { std::cout << "[!] Failed at BLOCK ALL IPv6\n";  abort_and_cleanup(); return false; }

    res = FwpmTransactionCommit0(engine);
    if (res != ERROR_SUCCESS)
    {
        print_error("FwpmTransactionCommit0(add)", res);
        abort_and_cleanup();
        return false;
    }

    std::cout << "[+] SUCCESS: Rules applied\n";

    // -----------------------------------------------------------------------
    // Grant Everyone read on the SUBLAYER object itself — must be outside
    // any transaction and after the sublayer has been committed.
    // -----------------------------------------------------------------------
    res = FwpmSubLayerSetSecurityInfoByKey0(
        engine,
        &EXAM_SUBLAYER_GUID,
        DACL_SECURITY_INFORMATION,
        NULL, NULL, pVisibilityDacl, NULL
    );
    std::cout << "[*] Sublayer SD result: " << res << "\n";
    if (res != ERROR_SUCCESS)
        print_error("FwpmSubLayerSetSecurityInfoByKey0", res);

    LocalFree(pFilterSD);
    LocalFree(pSRSD);
    LocalFree(pDacl);
    LocalFree(sid);
    FwpmEngineClose0(engine);
    return true;
}


std::vector<InstalledProgram> read_uninstall_key(HKEY root, const std::wstring& path)
{
    std::vector<InstalledProgram> programs;

    HKEY hKey;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return programs;

    DWORD index = 0;
    wchar_t subkeyName[256];
    DWORD subkeySize = 256;

    while (RegEnumKeyExW(hKey, index, subkeyName, &subkeySize,
        NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
        HKEY hSubKey;
        if (RegOpenKeyExW(hKey, subkeyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS)
        {
            wchar_t displayName[256];
            DWORD size = sizeof(displayName);

            if (RegGetValueW(hSubKey, NULL, L"DisplayName",
                RRF_RT_REG_SZ, NULL, displayName, &size) == ERROR_SUCCESS)
            {
                InstalledProgram prog;
                prog.name = wstring_to_utf8(displayName);

                wchar_t version[128];
                DWORD vsize = sizeof(version);

                if (RegGetValueW(hSubKey, NULL, L"DisplayVersion",
                    RRF_RT_REG_SZ, NULL, version, &vsize) == ERROR_SUCCESS)
                {
                    prog.version = wstring_to_utf8(version);
                }

                wchar_t displayIcon[1024];
                DWORD iconSize = sizeof(displayIcon);
                if (RegGetValueW(
                    hSubKey,
                    NULL,
                    L"DisplayIcon",
                    RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                    NULL,
                    displayIcon,
                    &iconSize
                ) == ERROR_SUCCESS)
                {
                    prog.display_icon = wstring_to_utf8(displayIcon);
                }

                programs.push_back(prog);
            }

            RegCloseKey(hSubKey);
        }

        index++;
        subkeySize = 256;
    }

    RegCloseKey(hKey);
    return programs;
}

std::vector<InstalledProgram> get_all_programs()
{
    std::vector<InstalledProgram> all;

    auto global1 = read_uninstall_key(
        HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    );

    auto global2 = read_uninstall_key(
        HKEY_LOCAL_MACHINE,
        L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    );

    auto user = read_uninstall_key(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    );

    all.insert(all.end(), user.begin(), user.end());

    return all;
}

std::vector<InstalledProgram> get_local_programs() {
    auto all_programs = get_all_programs();
    for (int i = 0;i < all_programs.size();i++) {
        cout << all_programs[i].name << " ";
        cout << all_programs[i].version << endl;
    }
    return all_programs;
}

void kill_user_processes(const wchar_t* username) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = { sizeof(pe) };
    int killed = 0;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (!hProc) continue;

            HANDLE hToken = NULL;
            if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
                DWORD size = 0;
                GetTokenInformation(hToken, TokenUser, NULL, 0, &size);
                if (size > 0) {
                    std::vector<BYTE> buf(size);
                    if (GetTokenInformation(hToken, TokenUser, buf.data(), size, &size)) {
                        TOKEN_USER* tokenUser = (TOKEN_USER*)buf.data();
                        wchar_t name[256], domain[256];
                        DWORD nameSize = 256, domainSize = 256;
                        SID_NAME_USE sidType;
                        if (LookupAccountSidW(NULL, tokenUser->User.Sid, name, &nameSize, domain, &domainSize, &sidType)) {
                            if (_wcsicmp(name, username) == 0) {
                                TerminateProcess(hProc, 0);
                                killed++;
                            }
                        }
                    }
                }
                CloseHandle(hToken);
            }
            CloseHandle(hProc);
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    if (killed > 0) {
        std::wcout << L"[CLEANUP] Killed " << killed << L" processes running as " << username << std::endl;
        Sleep(1000);
    }
}

void cleanup_exam_user_profiles(const wchar_t* username) {
    // Use DeleteProfileW for proper cleanup — it handles registry + folder atomically
    HKEY hProfileList;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList",
        0, KEY_READ, &hProfileList) == ERROR_SUCCESS)
    {
        DWORD index = 0;
        wchar_t subkeyName[256];
        DWORD subkeySize;
        std::vector<std::wstring> sidsToDelete;

        while (true) {
            subkeySize = 256;
            if (RegEnumKeyExW(hProfileList, index, subkeyName, &subkeySize,
                NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;

            HKEY hSubKey;
            if (RegOpenKeyExW(hProfileList, subkeyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                wchar_t profilePath[MAX_PATH];
                DWORD pathSize = sizeof(profilePath);
                if (RegGetValueW(hSubKey, NULL, L"ProfileImagePath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                    NULL, profilePath, &pathSize) == ERROR_SUCCESS)
                {
                    std::wstring path = profilePath;
                    if (path.find(username) != std::wstring::npos) {
                        sidsToDelete.push_back(std::wstring(subkeyName));
                        std::wcout << L"[CLEANUP] Found profile to delete: " << path << L" (SID: " << subkeyName << L")" << std::endl;
                    }
                }
                RegCloseKey(hSubKey);
            }
            index++;
        }
        RegCloseKey(hProfileList);

        for (const auto& sid : sidsToDelete) {
            if (DeleteProfileW(sid.c_str(), NULL, NULL)) {
                std::wcout << L"[CLEANUP] DeleteProfileW succeeded for SID: " << sid << std::endl;
            } else {
                DWORD err = GetLastError();
                std::wcout << L"[CLEANUP] DeleteProfileW failed for SID: " << sid << L" error=" << err << std::endl;
                // Fallback: manually remove folder and registry if DeleteProfileW fails
                std::wstring key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\" + sid;
                HKEY hKey;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                    wchar_t profilePath[MAX_PATH];
                    DWORD pathSize = sizeof(profilePath);
                    if (RegGetValueW(hKey, NULL, L"ProfileImagePath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                        NULL, profilePath, &pathSize) == ERROR_SUCCESS) {
                        std::wstring cmd = L"cmd.exe /c rmdir /s /q \"" + std::wstring(profilePath) + L"\"";
                        run_command(cmd);
                    }
                    RegCloseKey(hKey);
                }
                HKEY hPL;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList",
                    0, KEY_WRITE, &hPL) == ERROR_SUCCESS) {
                    RegDeleteTreeW(hPL, sid.c_str());
                    RegCloseKey(hPL);
                }
                std::wcout << L"[CLEANUP] Fallback cleanup done for SID: " << sid << std::endl;
            }
        }
    }

    // Also remove any leftover folders that might not have registry entries
    std::wstring usersDir = L"C:\\Users";
    std::wstring basePrefix = std::wstring(username) + L".";
    WIN32_FIND_DATAW findData;
    std::wstring searchPath = usersDir + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            std::wstring name = findData.cFileName;
            if (name == L"." || name == L"..")
                continue;
            if (name == username || name.find(basePrefix) == 0) {
                std::wstring fullPath = usersDir + L"\\" + name;
                std::wstring cmd = L"cmd.exe /c rmdir /s /q \"" + fullPath + L"\"";
                run_command(cmd);
                std::wcout << L"[CLEANUP] Removed leftover folder: " << name << std::endl;
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }

    Sleep(500);
    std::wcout << L"[CLEANUP] Profile cleanup complete\n";
}

int start_exam(const std::string& registered_name, const std::string& run_id, const std::string& backend_host, std::string& error_message) {
    error_message.clear();

    USER_INFO_1 ui;
    DWORD dwError = 0;

    if (!IsRunningAsAdmin()) {
        cout << "Not an admin" << endl;
        error_message = "Application must run as administrator.";
        return 10;
    }

    wchar_t username[] = L"exam_user";
    wchar_t password[] = L"Password123";

    ui.usri1_name = username;
    ui.usri1_password = password;
    ui.usri1_priv = USER_PRIV_USER;
    ui.usri1_home_dir = NULL;
    ui.usri1_comment = NULL;
    ui.usri1_flags = UF_SCRIPT;
    ui.usri1_script_path = NULL;

    NET_API_STATUS status = NetUserAdd(NULL, 1, (LPBYTE)&ui, &dwError);

    if (status == NERR_UserExists) {
        cout << "Account already exists, deleting and recreating...\n";

        // Kill all processes running as exam_user before deleting
        kill_user_processes(username);

        NET_API_STATUS delStatus = NetUserDel(NULL, username);
        if (delStatus != NERR_Success) {
            wcout << L"NetUserDel failed: " << delStatus << endl;
            error_message = "Failed to delete existing exam user. Code: " + std::to_string(delStatus);
            return 11;
        }
        wcout << L"Old exam_user deleted\n";

        // Clean up stale profile folders and registry entries
        cleanup_exam_user_profiles(username);

        status = NetUserAdd(NULL, 1, (LPBYTE)&ui, &dwError);
        if (status != NERR_Success) {
            wcout << L"NetUserAdd failed after delete: " << status << endl;
            error_message = "Failed to recreate exam user (NetUserAdd). Code: " + std::to_string(status);
            return 11;
        }
        wcout << L"User recreated successfully\n";
    }
    else if (status == NERR_Success) {
        wcout << L"User created successfully\n";
    }
    else {
        wcout << L"NetUserAdd failed: " << status << endl;
        error_message = "Failed to create exam user (NetUserAdd). Code: " + std::to_string(status);
        return 11;
    }

    LOCALGROUP_MEMBERS_INFO_3 groupInfo;
    groupInfo.lgrmi3_domainandname = username;

    NET_API_STATUS groupStatus = NetLocalGroupAddMembers(
        NULL,
        L"Users",   // group name
        3,
        (LPBYTE)&groupInfo,
        1
    );

    if (groupStatus == NERR_Success) {
        wcout << L"Added to Users group\n";
    }
    else if (groupStatus == ERROR_MEMBER_IN_ALIAS) {
        wcout << L"User already in Users group\n";
    }
    else {
        wcout << L"Failed to add to Users group: " << groupStatus << endl;
        error_message = "Failed to add exam user to Users group. Code: " + std::to_string(groupStatus);
        return 12;
    }

    bool init_ok = initialize_user_profile(username, password);
    cout << "init = " << init_ok << endl;
    if (!init_ok) {
        error_message = "Failed to initialize exam user profile.";
        return 13;
    }


    {
        json exam_config = {
            {"registered_name", registered_name},
            {"run_id", run_id},
            {"backend_host", backend_host}
        };
        std::string configPath = "C:\\ProgramData\\ExamApp\\exam_config.json";
        std::ofstream configFile(configPath);
        if (configFile.is_open()) {
            configFile << exam_config.dump(4);
            configFile.close();
            cout << "exam_config.json written to " << configPath << endl;
        }
        else {
            cout << "Failed to write exam_config.json" << endl;
            error_message = "Failed to write exam configuration file.";
            return 13;
        }
    }

    // your_work folder is created by exam_app after user logs in (Desktop doesn't exist until first interactive logon)

    //Setup UAC deny

    DWORD originalValue = 0;
    DWORD size = sizeof(DWORD);

    RegGetValue(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        L"ConsentPromptBehaviorUser",
        RRF_RT_REG_DWORD,
        NULL,
        &originalValue,
        &size
    );

    cout << "Original value: " << originalValue << endl;


    DWORD newValue = 0; // deny

    RegSetKeyValue(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        L"ConsentPromptBehaviorUser",
        REG_DWORD,
        &newValue,
        sizeof(newValue)
    );

    //Setup startup program

    bool deployed = deploy_exam_app();
    cout << "deploy = " << deployed << endl;
    if (!deployed) {
        error_message = "Failed to deploy exam application.";
        return 14;
    }

    bool unlock_status = setup_unlock_hook(username);
    cout << "startup = " << unlock_status << endl;
    if (!unlock_status) {
        error_message = "Failed to configure exam startup task.";
        return 15;
    }

    bool exam_wfp = setup_exam_user_wfp(get_user_sid(wstring(username)), backend_host);
    cout << "exam_wfp = " << exam_wfp << endl;
    if (!exam_wfp) {
        error_message = "Failed to apply network restrictions for exam user.";
        return 16;
    }


    //LockWorkStation();
    return 0;
}

vector<thread> threads;

static DWORD g_parent_pid = 0;

void parent_monitor_thread() {
    if (g_parent_pid == 0) return;

    HANDLE hParent = OpenProcess(SYNCHRONIZE, FALSE, g_parent_pid);
    if (!hParent) {
        std::cout << "[MONITOR] Cannot open parent PID " << g_parent_pid << ", exiting.\n";
        ExitProcess(0);
        return;
    }

    std::cout << "[MONITOR] Watching parent PID " << g_parent_pid << "\n";
    WaitForSingleObject(hParent, INFINITE);
    CloseHandle(hParent);
    std::cout << "[MONITOR] Parent process exited, shutting down.\n";
    ExitProcess(0);
}

int main(int argc, char* argv[]) {

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--parent-pid" && i + 1 < argc) {
            g_parent_pid = (DWORD)atoi(argv[i + 1]);
            i++;
        }
    }

    threads.push_back(thread(pipe_server_thread));
    threads.push_back(thread(parent_monitor_thread));

    //LockWorkStation();

    for (int i = 0;i < threads.size();i++) {
        threads[i].join();
    }
    
    return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
