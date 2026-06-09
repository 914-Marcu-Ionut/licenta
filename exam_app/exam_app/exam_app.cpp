// exam_app.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>   // FIRST
#include <windows.h>
#include <ws2tcpip.h>
#include <wtsapi32.h>

#include <fwpmu.h>
#include <sddl.h>

#include <Aclapi.h>
#include <lm.h>
#include <userenv.h>
#include <shellapi.h>
#include <ntsecapi.h>
#include <Dbt.h>

#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <intrin.h>

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <openssl/sha.h>
#include "json.hpp"
#include "work_analyzer.h"

using json = nlohmann::json;

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "fwpuclnt.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Netapi32.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Iphlpapi.lib")

#include <iphlpapi.h>
#include <tcpmib.h>
#include <tlhelp32.h>
#include <wininet.h>
#pragma comment(lib, "Wininet.lib")

using namespace std;

static std::string BACKEND_HOST = "192.168.0.199";
static int BACKEND_PORT = 8443;

void ws_send_event(const std::string& type, const json& detail = json::object());
json get_active_window_info();

const GUID EXAM_SUBLAYER_GUID =
{ 0x12345678, 0x1234, 0x1234, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22 } };


static const UINT32 DNS_SERVER_1 = ntohl(inet_addr("8.8.8.8"));
static const UINT32 DNS_SERVER_2 = ntohl(inet_addr("1.1.1.1"));

struct ExpectedFilter
{
    FWP_ACTION_TYPE action;
    bool            hasIP;
    UINT32          ip;
    bool            hasDNS;
    UINT32          dnsIP;  // 0 = any, non-zero = specific DNS server IP
    bool            isIPv6Block; // true = IPv6 block-all, different layer
};

static const UINT32 EXPECTED_LOCALHOST = ntohl(inet_addr("127.0.0.1"));

static std::vector<ExpectedFilter> get_expected_filters()
{
    UINT32 backendIP = ntohl(inet_addr(BACKEND_HOST.c_str()));
    return {
        { FWP_ACTION_PERMIT, true,  backendIP,          false, 0,           false }, // ALLOW IP
        { FWP_ACTION_PERMIT, true,  EXPECTED_LOCALHOST, false, 0,           false }, // ALLOW LOCALHOST
        { FWP_ACTION_PERMIT, true,  DNS_SERVER_1,       true,  DNS_SERVER_1, false }, // ALLOW DNS 8.8.8.8
        { FWP_ACTION_PERMIT, true,  DNS_SERVER_2,       true,  DNS_SERVER_2, false }, // ALLOW DNS 1.1.1.1
        { FWP_ACTION_BLOCK,  false, 0,                  false, 0,           false }, // BLOCK ALL IPv4
        { FWP_ACTION_BLOCK,  false, 0,                  false, 0,           true  }, // BLOCK ALL IPv6
    };
}


vector<thread> threads;

// Validate a single filter's conditions match what we expect
bool validate_filter(FWPM_FILTER0* f, const ExpectedFilter& expected)
{
    if (f->action.type != expected.action)
        return false;

    // IPv6 block is on a different layer
    if (expected.isIPv6Block)
    {
        return f->layerKey == FWPM_LAYER_ALE_AUTH_CONNECT_V6
            && f->numFilterConditions == 1; // only user SID condition
    }

    if (f->layerKey != FWPM_LAYER_ALE_AUTH_CONNECT_V4)
        return false;

    bool hasUserCond = false;
    bool hasIPCond = false;
    bool hasProtoCond = false;
    bool hasPortCond = false;

    for (UINT32 i = 0; i < f->numFilterConditions; i++)
    {
        FWPM_FILTER_CONDITION0& c = f->filterCondition[i];

        if (c.fieldKey == FWPM_CONDITION_ALE_USER_ID)
        {
            if (c.conditionValue.type != FWP_SECURITY_DESCRIPTOR_TYPE) return false;
            if (c.conditionValue.sd == NULL) return false;
            hasUserCond = true;
        }

        if (c.fieldKey == FWPM_CONDITION_IP_REMOTE_ADDRESS)
        {
            if (!expected.hasIP) return false;
            if (c.conditionValue.type != FWP_V4_ADDR_MASK) return false;
            if (c.conditionValue.v4AddrMask->addr != expected.ip) return false;
            if (c.conditionValue.v4AddrMask->mask != 0xFFFFFFFF) return false;
            hasIPCond = true;
        }

        if (c.fieldKey == FWPM_CONDITION_IP_PROTOCOL)
        {
            if (!expected.hasDNS) return false;
            if (c.conditionValue.type != FWP_UINT8) return false;
            if (c.conditionValue.uint8 != IPPROTO_UDP) return false;
            hasProtoCond = true;
        }

        if (c.fieldKey == FWPM_CONDITION_IP_REMOTE_PORT)
        {
            if (!expected.hasDNS) return false;
            if (c.conditionValue.type != FWP_UINT16) return false;
            if (c.conditionValue.uint16 != 53) return false;
            hasPortCond = true;
        }
    }

    if (!hasUserCond) return false;
    if (expected.hasIP && !hasIPCond) return false;
    if (expected.hasDNS && (!hasProtoCond || !hasPortCond)) return false;

    return true;
}

int check_rules_intact()
{
    HANDLE engine = NULL;

    // Step 1: open engine
    DWORD res = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, NULL, &engine);
    if (res != ERROR_SUCCESS)
    {
        std::cout << "[WD] FwpmEngineOpen0 failed: " << res << "\n";
        return -1;
    }

    // Step 2: check sublayer exists
    FWPM_SUBLAYER0* pSubLayer = NULL;
    res = FwpmSubLayerGetByKey0(engine, &EXAM_SUBLAYER_GUID, &pSubLayer);
    if (res != ERROR_SUCCESS)
    {
        std::cout << "[WD] Sublayer missing or inaccessible: " << res << "\n";
        FwpmEngineClose0(engine);
        return -2; // rules are gone
    }
    FwpmFreeMemory0((void**)&pSubLayer);

    // Step 3 & 4: enumerate filters on BOTH IPv4 and IPv6 layers
    std::vector<FWPM_FILTER0*> ourFilters;
    std::vector<FWPM_FILTER0**> allocations; // track for cleanup

    auto enumerate_layer = [&](const GUID& layerKey) -> bool
        {
            HANDLE enumHandle = NULL;
            FWPM_FILTER_ENUM_TEMPLATE0 enumTemplate = {};
            enumTemplate.layerKey = layerKey;
            enumTemplate.enumType = FWP_FILTER_ENUM_FULLY_CONTAINED;
            enumTemplate.flags = FWP_FILTER_ENUM_FLAG_INCLUDE_BOOTTIME;
            enumTemplate.actionMask = 0xFFFFFFFF;

            DWORD r = FwpmFilterCreateEnumHandle0(engine, &enumTemplate, &enumHandle);
            if (r != ERROR_SUCCESS)
            {
                std::cout << "[WD] FwpmFilterCreateEnumHandle0 failed: " << r << "\n";
                return false;
            }

            FWPM_FILTER0** filters = NULL;
            UINT32 count = 0;
            r = FwpmFilterEnum0(engine, enumHandle, 100, &filters, &count);
            if (r != ERROR_SUCCESS)
            {
                std::cout << "[WD] FwpmFilterEnum0 failed: " << r << "\n";
                FwpmFilterDestroyEnumHandle0(engine, enumHandle);
                return false;
            }

            for (UINT32 i = 0; i < count; i++)
                if (filters[i]->subLayerKey == EXAM_SUBLAYER_GUID)
                    ourFilters.push_back(filters[i]);

            allocations.push_back(filters);
            FwpmFilterDestroyEnumHandle0(engine, enumHandle);
            return true;
        };

    auto free_all = [&]()
        {
            for (auto alloc : allocations)
                FwpmFreeMemory0((void**)&alloc);
            FwpmEngineClose0(engine);
        };

    if (!enumerate_layer(FWPM_LAYER_ALE_AUTH_CONNECT_V4) ||
        !enumerate_layer(FWPM_LAYER_ALE_AUTH_CONNECT_V6))
    {
        free_all();
        return -1; // engine/API failure, not necessarily tampering
    }

    // Step 5: must have exactly the expected number of filters
    auto expectedFilters = get_expected_filters();
    int expectedCount = (int)expectedFilters.size();
    if ((int)ourFilters.size() != expectedCount)
    {
        std::cout << "[WD] Expected " << expectedCount
            << " filters, found " << ourFilters.size() << "\n";
        free_all();
        return -2; // wrong number of filters — tampered
    }

    // Step 6: match each expected filter against what we found —
    // order is not guaranteed by WFP so we match by content
    std::vector<bool> matched(ourFilters.size(), false);
    for (const auto& expected : expectedFilters)
    {
        bool found = false;
        for (size_t i = 0; i < ourFilters.size(); i++)
        {
            if (!matched[i] && validate_filter(ourFilters[i], expected))
            {
                matched[i] = true;
                found = true;
                break;
            }
        }

        if (!found)
        {
            std::cout << "[WD] Missing or invalid filter — action=" << expected.action
                << " hasIP=" << expected.hasIP
                << " ip=" << expected.ip
                << " hasDNS=" << expected.hasDNS
                << " isIPv6=" << expected.isIPv6Block << "\n";
            free_all();
            return -2; // filter present but contents are wrong — tampered
        }
    }

    free_all();
    return 0;
}

bool protect_process() {
    HANDLE hProcess = GetCurrentProcess();

    EXPLICIT_ACCESS ea = {};
    ea.grfAccessPermissions = PROCESS_TERMINATE;
    ea.grfAccessMode = DENY_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    ea.Trustee.ptstrName = (LPWSTR)L"Users";

    PACL pNewDACL = NULL;

    DWORD res = SetEntriesInAcl(1, &ea, NULL, &pNewDACL);
    if (res != ERROR_SUCCESS)
        return false;

    res = SetSecurityInfo(
        hProcess,
        SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION,
        NULL,
        NULL,
        pNewDACL,
        NULL
    );

    if (pNewDACL)
        LocalFree(pNewDACL);

    return res == ERROR_SUCCESS;
}

json get_active_window_info()
{
    json info;
    info["window"] = "";
    info["process"] = "";
    info["pid"] = 0;

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return info;

    int titleLen = GetWindowTextLengthA(hwnd);
    std::string title;
    if (titleLen > 0)
    {
        title.resize(static_cast<size_t>(titleLen) + 1);
        int copied = GetWindowTextA(hwnd, &title[0], titleLen + 1);
        title.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    std::string processName;
    if (pid != 0)
    {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess)
        {
            char processPath[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameA(hProcess, 0, processPath, &size))
                processName = processPath;
            CloseHandle(hProcess);
        }
    }

    info["window"] = title;
    info["process"] = processName;
    info["pid"] = pid;
    return info;
}


HWND g_hiddenWindow = NULL;

// Your actual clipboard handling logic goes here
void on_clipboard_changed()
{
    if (!OpenClipboard(NULL)) return;

    json win = get_active_window_info();

    if (IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        std::string text;
        if (h) {
            LPCWSTR wstr = (LPCWSTR)GlobalLock(h);
            if (wstr) {
                int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
                if (len > 0) {
                    text.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &text[0], len, NULL, NULL);
                }
                GlobalUnlock(h);
            }
        }
        if (text.size() > 512) text = text.substr(0, 512) + "...";
        json detail;
        detail["text"] = text;
        detail["active_window"] = win;
        ws_send_event("clipboard_text", detail);
    }

    if (IsClipboardFormatAvailable(CF_HDROP))
    {
        json detail;
        detail["active_window"] = win;

        HANDLE h = GetClipboardData(CF_HDROP);
        if (h) {
            HDROP hDrop = (HDROP)GlobalLock(h);
            if (hDrop) {
                UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
                json files = json::array();
                for (UINT i = 0; i < count && i < 20; i++) {
                    wchar_t path[MAX_PATH];
                    if (DragQueryFileW(hDrop, i, path, MAX_PATH)) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
                        if (len > 0) {
                            std::string utf8(len - 1, 0);
                            WideCharToMultiByte(CP_UTF8, 0, path, -1, &utf8[0], len, NULL, NULL);
                            files.push_back(utf8);
                        }
                    }
                }
                detail["files"] = files;
                GlobalUnlock(h);
            }
        }

        ws_send_event("clipboard_file", detail);
    }

    CloseClipboard();
}

LRESULT CALLBACK hidden_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CLIPBOARDUPDATE:
        // Fired every time clipboard changes in THIS window station
        on_clipboard_changed();
        return 0;

    case WM_DESTROY:
        RemoveClipboardFormatListener(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


void monitor_clipboard_thread() {
    // Register a hidden window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc = hidden_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"ExamClipboardMonitor";
    RegisterClassW(&wc);

    // Create hidden window — no WS_VISIBLE, no parent
    g_hiddenWindow = CreateWindowExW(
        0, L"ExamClipboardMonitor", L"",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,   // message-only window, never shown
        NULL, GetModuleHandleW(NULL), NULL
    );

    if (!g_hiddenWindow)
    {
        std::cout << "[CB] Failed to create hidden window\n";
        return;
    }

    // Register for clipboard notifications
    if (!AddClipboardFormatListener(g_hiddenWindow))
    {
        std::cout << "[CB] AddClipboardFormatListener failed\n";
        DestroyWindow(g_hiddenWindow);
        return;
    }

    std::cout << "[CB] Clipboard monitor started (event-driven)\n";

    // Message loop — WM_CLIPBOARDUPDATE fires here on every clipboard change
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout << "[CB] Clipboard monitor stopped\n";
}

void rules_monitor_thread() {
    int lastStatus = 999;
    while (true) {
        int wfpStatus = check_rules_intact();

        if (wfpStatus != lastStatus) {
            lastStatus = wfpStatus;
            json detail;
            detail["code"] = wfpStatus;

            if (wfpStatus == 0) {
                detail["message"] = "rules_ok";
                std::cout << "[FW] All rules intact\n";
                ws_send_event("firewall_ok", detail);
            }
            else if (wfpStatus == -1) {
                detail["message"] = "sublayer_missing";
                std::cout << "[FW] Sublayer missing!\n";
                ws_send_event("firewall_error", detail);
            }
            else if (wfpStatus == -2) {
                detail["message"] = "rules_tampered";
                std::cout << "[FW] Rules tampered!\n";
                ws_send_event("firewall_tampered", detail);
            }
            else {
                detail["message"] = "unknown_error";
                std::cout << "[FW] Unknown error: " << wfpStatus << "\n";
                ws_send_event("firewall_error", detail);
            }
        }

        Sleep(1000);
    }
}


HWND g_logoffWindow = NULL;

LRESULT CALLBACK logoff_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_WTSSESSION_CHANGE:
    {
        std::string event_type;
        switch (wParam)
        {
        case WTS_SESSION_LOCK:
            event_type = "session_lock";
            break;
        case WTS_SESSION_UNLOCK:
            event_type = "session_unlock";
            break;
        case WTS_SESSION_LOGOFF:
            event_type = "session_logoff";
            break;
        case WTS_CONSOLE_DISCONNECT:
            event_type = "session_disconnect";
            break;
        case WTS_CONSOLE_CONNECT:
            event_type = "session_connect";
            break;
        case WTS_REMOTE_DISCONNECT:
            event_type = "session_remote_disconnect";
            break;
        case WTS_REMOTE_CONNECT:
            event_type = "session_remote_connect";
            break;
        default:
            event_type = "session_other";
            break;
        }
        std::cout << "[SESSION] " << event_type << "\n";
        ws_send_event(event_type);
        return 0;
    }

    case WM_QUERYENDSESSION:
        std::cout << "[SESSION] Logoff/shutdown requested\n";
        ws_send_event("session_shutdown_requested");
        return TRUE;

    case WM_ENDSESSION:
        std::cout << "[SESSION] Session ending\n";
        ws_send_event("session_ending");
        return 0;

    case WM_DESTROY:
        WTSUnRegisterSessionNotification(hwnd);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}


void logoff_monitor_thread()
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = logoff_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"ExamSessionMonitor";

    RegisterClassW(&wc);

    g_logoffWindow = CreateWindowExW(
        0,
        L"ExamSessionMonitor",
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        NULL,
        GetModuleHandleW(NULL),
        NULL
    );

    if (!g_logoffWindow)
    {
        std::cout << "[SESSION] Failed to create hidden window\n";
        return;
    }

    if (!WTSRegisterSessionNotification(
        g_logoffWindow,
        NOTIFY_FOR_THIS_SESSION))
    {
        std::cout << "[SESSION] WTSRegisterSessionNotification failed\n";
        DestroyWindow(g_logoffWindow);
        return;
    }

    std::cout << "[SESSION] Logoff monitor started\n";

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    std::cout << "[SESSION] Logoff monitor stopped\n";
}

typedef websocketpp::client<websocketpp::config::asio_tls_client> wss_client;
typedef websocketpp::lib::shared_ptr<asio::ssl::context> context_ptr;

static wss_client* g_ws = nullptr;
static websocketpp::connection_hdl g_ws_hdl;
static std::mutex g_ws_mutex;
static std::atomic<bool> g_ws_connected{ false };
static std::atomic<int> g_ws_msg_id{ 1 };

static int g_ws_seq = 0;
static std::string g_ws_prev_hash;

static std::string g_registered_name;
static std::string g_run_id;
static std::string g_student_id;
static std::atomic<bool> g_work_uploaded{ false };
static json g_exam_settings;
static std::mutex g_settings_mutex;
static std::atomic<bool> g_logged_in{ false };

static std::wstring g_user_profile;
static std::string g_user_profile_utf8;

void init_user_profile_path() {
    wchar_t buf[MAX_PATH];
    DWORD len = ExpandEnvironmentStringsW(L"%USERPROFILE%", buf, MAX_PATH);
    if (len > 0 && len <= MAX_PATH) {
        g_user_profile = buf;
    } else {
        g_user_profile = L"C:\\Users\\exam_user";
    }
    char narrow[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, g_user_profile.c_str(), -1, narrow, MAX_PATH, NULL, NULL);
    g_user_profile_utf8 = narrow;
    std::wcout << L"[INIT] User profile: " << g_user_profile << std::endl;
}

static std::string sha256_hex(const std::string& input)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(hex + i * 2, "%02x", hash[i]);
    hex[SHA256_DIGEST_LENGTH * 2] = 0;
    return std::string(hex);
}

bool load_exam_config(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cout << "[WS] Cannot open " << path << "\n";
        return false;
    }
    json cfg;
    try {
        f >> cfg;
    }
    catch (...) {
        std::cout << "[WS] Failed to parse " << path << "\n";
        return false;
    }
    if (cfg.contains("registered_name")) g_registered_name = cfg["registered_name"].get<std::string>();
    if (cfg.contains("run_id"))          g_run_id = cfg["run_id"].get<std::string>();
    if (cfg.contains("backend_host"))    BACKEND_HOST = cfg["backend_host"].get<std::string>();
    if (cfg.contains("backend_port"))    BACKEND_PORT = cfg["backend_port"].get<int>();
    return !g_registered_name.empty() && !g_run_id.empty();
}

context_ptr on_tls_init(websocketpp::connection_hdl)
{
    context_ptr ctx = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12);
    ctx->set_verify_mode(asio::ssl::verify_none);
    return ctx;
}

void ws_send(const json& msg)
{
    std::lock_guard<std::mutex> lk(g_ws_mutex);
    if (!g_ws_connected || !g_ws) {
        std::cout << "[WS] send skipped (not connected): " << msg.value("method", "?") << "\n";
        return;
    }
    try {
        json chained = msg;
        chained["seq"] = g_ws_seq++;
        chained["prev_hash"] = g_ws_prev_hash;

        std::string payload = chained.dump();
        g_ws_prev_hash = sha256_hex(payload);

        g_ws->send(g_ws_hdl, payload, websocketpp::frame::opcode::text);
    }
    catch (const std::exception& e) {
        std::cout << "[WS] send error: " << e.what() << "\n";
        g_ws_connected = false;
    }
}

void ws_send_login()
{
    json req;
    req["id"] = g_ws_msg_id++;
    req["method"] = "login";
    req["params"] = { {"name", g_registered_name}, {"run_id", g_run_id} };
    std::cout << "[WS] Sending login: " << req.dump() << "\n";
    ws_send(req);
}

void ws_send_event(const std::string& type, const json& detail)
{
    if (!g_ws_connected) return;
    json req;
    req["id"] = g_ws_msg_id++;
    req["method"] = "event";
    json params;
    params["type"] = type;
    params["detail"] = detail;
    req["params"] = params;
    std::cout << "[WS] Event: " << type << "\n";
    ws_send(req);
}

struct VMDetectionResult {
    bool is_vm;
    std::string reason;
};

VMDetectionResult detect_virtual_machine()
{
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 1);
    if (cpuInfo[2] & (1 << 31)) {
        int hvInfo[4] = {};
        __cpuid(hvInfo, 0x40000000);
        char vendor[13] = {};
        memcpy(vendor, &hvInfo[1], 4);
        memcpy(vendor + 4, &hvInfo[2], 4);
        memcpy(vendor + 8, &hvInfo[3], 4);
        vendor[12] = 0;

        std::string hvVendor(vendor);

        if (hvVendor == "Microsoft Hv") {
            // Check if we're in the root partition (bare metal with VBS/HVCI)
            // CPUID leaf 0x40000003 EBX bit 0 = running in root partition
            int partInfo[4] = {};
            __cpuid(partInfo, 0x40000003);
            bool isRootPartition = (partInfo[1] & 1) != 0;
            if (isRootPartition) {
                // Bare metal with Hyper-V/VBS enabled — not a VM
                return { false, "" };
            }
            return { true, "cpuid_hypervisor: Microsoft Hv (guest partition)" };
        }

        if (hvVendor == "VMwareVMware" || hvVendor == "VBoxVBoxVBox" ||
            hvVendor == "KVMKVMKVM\0\0\0" || hvVendor == "XenVMMXenVMM" ||
            hvVendor == "prl hyperv  ") {
            return { true, "cpuid_hypervisor: " + hvVendor };
        }
        return { true, "cpuid_hypervisor_bit_set" };
    }

    auto check_registry = [](HKEY root, const wchar_t* path, const wchar_t* value, const char* vm_strings[], int count) -> std::string {
        wchar_t data[256] = {};
        DWORD size = sizeof(data);
        if (RegGetValueW(root, path, value, RRF_RT_REG_SZ, NULL, data, &size) == ERROR_SUCCESS) {
            char buf[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, data, -1, buf, sizeof(buf), NULL, NULL);
            std::string lower(buf);
            for (auto& c : lower) c = (char)tolower(c);
            for (int i = 0; i < count; i++) {
                if (lower.find(vm_strings[i]) != std::string::npos)
                    return std::string(vm_strings[i]);
            }
        }
        return "";
    };

    const char* vm_identifiers[] = { "vmware", "virtualbox", "vbox", "qemu", "kvm", "xen", "parallels", "hyper-v", "virtual machine" };
    int id_count = sizeof(vm_identifiers) / sizeof(vm_identifiers[0]);

    std::string hit;

    hit = check_registry(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Disk\\Enum", L"0", vm_identifiers, id_count);
    if (!hit.empty()) return { true, "registry_disk: " + hit };

    hit = check_registry(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer", vm_identifiers, id_count);
    if (!hit.empty()) return { true, "registry_bios_manufacturer: " + hit };

    hit = check_registry(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName", vm_identifiers, id_count);
    if (!hit.empty()) return { true, "registry_bios_product: " + hit };

    const wchar_t* vm_services[] = { L"VMTools", L"VBoxService", L"VBoxGuest", L"vmicheartbeat", L"xenevtchn" };
    for (const auto& svc : vm_services) {
        SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        if (scm) {
            SC_HANDLE hSvc = OpenServiceW(scm, svc, SERVICE_QUERY_STATUS);
            if (hSvc) {
                CloseServiceHandle(hSvc);
                CloseServiceHandle(scm);
                char buf[64] = {};
                WideCharToMultiByte(CP_UTF8, 0, svc, -1, buf, sizeof(buf), NULL, NULL);
                return { true, "vm_service_found: " + std::string(buf) };
            }
            CloseServiceHandle(scm);
        }
    }

    return { false, "" };
}

void on_ws_open(websocketpp::connection_hdl hdl)
{
    std::cout << "[WS] Connected to backend (WSS)\n";
    {
        std::lock_guard<std::mutex> lk(g_ws_mutex);
        g_ws_hdl = hdl;
        g_ws_connected = true;
    }
    ws_send_login();

    VMDetectionResult vm = detect_virtual_machine();
    json vmDetail;
    vmDetail["is_vm"] = vm.is_vm;
    vmDetail["reason"] = vm.reason;
    ws_send_event("vm_detection", vmDetail);

    snapshot_user_files(g_user_profile.c_str(), [](const std::string& type, const json& detail) {
        ws_send_event(type, detail);
    });
}

std::atomic<bool> g_exam_ended{ false };
std::atomic<int> g_remaining_seconds{ 0 };
std::atomic<bool> g_student_finished{ false };

bool download_file_https(const std::string& server, int port, const std::string& url_path, const std::wstring& local_path)
{
    HINTERNET hInternet = InternetOpenA("ExamApp/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hConnect = InternetConnectA(hInternet, server.c_str(), (INTERNET_PORT)port,
        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { InternetCloseHandle(hInternet); return false; }

    DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID |
        INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_NO_CACHE_WRITE;

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "GET", url_path.c_str(),
        NULL, NULL, NULL, flags, 0);
    if (!hRequest) { InternetCloseHandle(hConnect); InternetCloseHandle(hInternet); return false; }

    DWORD secFlags = 0;
    DWORD secSize = sizeof(secFlags);
    InternetQueryOptionA(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, &secSize);
    secFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    InternetSetOptionA(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    if (!HttpSendRequestA(hRequest, NULL, 0, NULL, 0)) {
        InternetCloseHandle(hRequest); InternetCloseHandle(hConnect); InternetCloseHandle(hInternet);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, NULL);
    if (statusCode != 200) {
        InternetCloseHandle(hRequest); InternetCloseHandle(hConnect); InternetCloseHandle(hInternet);
        return false;
    }

    std::wstring dir = local_path.substr(0, local_path.find_last_of(L"\\/"));
    CreateDirectoryW(dir.c_str(), NULL);

    HANDLE hFile = CreateFileW(local_path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        InternetCloseHandle(hRequest); InternetCloseHandle(hConnect); InternetCloseHandle(hInternet);
        return false;
    }

    char buffer[8192];
    DWORD bytesRead;
    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        DWORD written;
        WriteFile(hFile, buffer, bytesRead, &written, NULL);
    }

    CloseHandle(hFile);
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return true;
}

void download_exam_files(const json& file_urls)
{
    const std::wstring subject_dir = g_user_profile + L"\\Desktop\\subject";
    CreateDirectoryW(subject_dir.c_str(), NULL);

    std::cout << "[FILES] Downloading " << file_urls.size() << " exam files to subject/ folder\n";

    for (const auto& url : file_urls) {
        std::string url_path = url.get<std::string>();

        std::string filename = url_path;
        size_t last_slash = filename.find_last_of('/');
        if (last_slash != std::string::npos)
            filename = filename.substr(last_slash + 1);

        int len = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, NULL, 0);
        std::wstring wfilename(len - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, &wfilename[0], len);

        std::wstring local_path = subject_dir + L"\\" + wfilename;

        if (download_file_https(BACKEND_HOST, BACKEND_PORT, url_path, local_path)) {
            std::cout << "[FILES] Downloaded: " << filename << "\n";
        }
        else {
            std::cout << "[FILES] FAILED to download: " << filename << "\n";
        }
    }

    std::cout << "[FILES] Download complete\n";
}

// ===================== WORK UPLOAD (ZIP + HTTPS POST) =====================

static bool zip_directory(const std::wstring& dir_path, const std::wstring& zip_path)
{
    DeleteFileW(zip_path.c_str());

    std::string cmd = "powershell -NoProfile -Command \"Compress-Archive -Path '";
    char narrow_dir[MAX_PATH], narrow_zip[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, dir_path.c_str(), -1, narrow_dir, MAX_PATH, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, zip_path.c_str(), -1, narrow_zip, MAX_PATH, NULL, NULL);
    cmd += narrow_dir;
    cmd += "\\*' -DestinationPath '";
    cmd += narrow_zip;
    cmd += "' -Force\"";

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        std::cout << "[UPLOAD] Failed to start zip process\n";
        return false;
    }

    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        std::cout << "[UPLOAD] Zip process failed with code " << exitCode << "\n";
        return false;
    }
    return GetFileAttributesW(zip_path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool upload_work_https(const std::string& server, int port,
    const std::string& student_id, const std::string& run_id,
    const std::wstring& file_path)
{
    HANDLE hFile = CreateFileW(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, NULL);
    std::vector<char> fileData(fileSize);
    DWORD bytesRead;
    ReadFile(hFile, fileData.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    std::string boundary = "----ExamWorkBoundary9876543210";
    std::string body;
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"student_id\"\r\n\r\n";
    body += student_id + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"run_id\"\r\n\r\n";
    body += run_id + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"work.zip\"\r\n";
    body += "Content-Type: application/zip\r\n\r\n";
    body += std::string(fileData.begin(), fileData.end());
    body += "\r\n--" + boundary + "--\r\n";

    HINTERNET hInternet = InternetOpenA("ExamApp/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hConnect = InternetConnectA(hInternet, server.c_str(), (INTERNET_PORT)port,
        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { InternetCloseHandle(hInternet); return false; }

    DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID |
        INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/student/upload-work",
        NULL, NULL, NULL, flags, 0);
    if (!hRequest) { InternetCloseHandle(hConnect); InternetCloseHandle(hInternet); return false; }

    DWORD secFlags = 0;
    DWORD secFlagsSize = sizeof(secFlags);
    InternetQueryOptionA(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, &secFlagsSize);
    secFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    InternetSetOptionA(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    std::string headers = "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";

    BOOL sent = HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.size(),
        (LPVOID)body.data(), (DWORD)body.size());

    bool success = false;
    if (sent) {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
            &statusCode, &statusSize, NULL);
        success = (statusCode == 200);
        std::cout << "[UPLOAD] Server responded: " << statusCode << "\n";
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return success;
}

void upload_student_work()
{
    if (g_work_uploaded.exchange(true)) return;

    std::cout << "[UPLOAD] Preparing to upload student work...\n";

    const std::wstring work_dir = g_user_profile + L"\\Desktop\\your_work";
    const std::wstring zip_path = g_user_profile + L"\\Desktop\\your_work.zip";

    if (GetFileAttributesW(work_dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::cout << "[UPLOAD] your_work folder not found, skipping upload\n";
        MessageBoxW(NULL,
            L"The 'your_work' folder was not found on Desktop.\n\n"
            L"No work was uploaded. You may now log out.",
            L"Exam Finished", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return;
    }

    std::cout << "[UPLOAD] Zipping your_work folder...\n";
    if (!zip_directory(work_dir, zip_path)) {
        std::cout << "[UPLOAD] Failed to zip your_work folder\n";
        g_work_uploaded = false;
        MessageBoxW(NULL,
            L"Failed to compress your work folder.\n\n"
            L"Please contact your teacher immediately.",
            L"Upload Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return;
    }

    std::cout << "[UPLOAD] Uploading work.zip to backend...\n";
    if (upload_work_https(BACKEND_HOST, BACKEND_PORT, g_student_id, g_run_id, zip_path)) {
        std::cout << "[UPLOAD] Work uploaded successfully!\n";
        DeleteFileW(zip_path.c_str());
        MessageBoxW(NULL,
            L"Your exam is finished and your work has been uploaded successfully.\n\n"
            L"You can now log out from this account.",
            L"Exam Finished", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    }
    else {
        std::cout << "[UPLOAD] Failed to upload work\n";
        g_work_uploaded = false;
        MessageBoxW(NULL,
            L"Failed to upload your work to the server.\n\n"
            L"Please do NOT log out. Contact your teacher immediately.",
            L"Upload Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
}

// ===================== END WORK UPLOAD =====================

void on_ws_message(websocketpp::connection_hdl, wss_client::message_ptr msg)
{
    std::string payload = msg->get_payload();
    std::cout << "[WS] Received: " << payload << "\n";

    try {
        json resp = json::parse(payload);
        int status = resp.value("status", -1);
        std::string message = resp.value("message", "");

        if (message == "exam_ended") {
            std::cout << "[WS] === EXAM ENDED === reason: " << resp["data"].value("reason", "unknown") << "\n";
            g_exam_ended = true;
            g_remaining_seconds = 0;
            std::thread(upload_student_work).detach();
            return;
        }

        if (status == 0 && message == "success" && resp.contains("data")) {
            json data = resp["data"];

            if (data.contains("student_id") && data.contains("settings")) {
                std::lock_guard<std::mutex> lk(g_settings_mutex);
                g_student_id = data["student_id"].get<std::string>();
                g_exam_settings = data["settings"];
                g_logged_in = true;

                int remaining = data.value("remaining_seconds", 0);
                g_remaining_seconds = remaining;
                std::cout << "[WS] Login successful. Settings: " << g_exam_settings.dump() << "\n";
                if (remaining > 0) {
                    std::cout << "[WS] Exam time remaining: " << remaining / 60 << "m " << remaining % 60 << "s\n";
                }

                if (data.contains("exam_files") && data["exam_files"].is_array() && !data["exam_files"].empty()) {
                    std::thread(download_exam_files, data["exam_files"]).detach();
                }
            }
        }
        else if (status != 0) {
            std::cout << "[WS] Server error: " << message << "\n";
        }
    }
    catch (...) {
        std::cout << "[WS] Failed to parse server message\n";
    }
}

void on_ws_close(websocketpp::connection_hdl)
{
    std::cout << "[WS] Disconnected from backend\n";
    g_ws_connected = false;
}

void on_ws_fail(websocketpp::connection_hdl)
{
    std::cout << "[WS] Connection failed\n";
    g_ws_connected = false;
}

void websocket_thread()
{
    if (!load_exam_config("C:\\ProgramData\\ExamApp\\exam_config.json")) {
        std::cout << "[WS] exam_config.json missing or invalid, skipping WS\n";
        return;
    }
    std::cout << "[WS] Config loaded: name=" << g_registered_name << " run=" << g_run_id << "\n";

    std::string uri = "wss://" + BACKEND_HOST + ":" + std::to_string(BACKEND_PORT) + "/ws";

    while (true) {
        try {
            {
                std::lock_guard<std::mutex> lk(g_ws_mutex);
                g_ws_connected = false;
                g_ws_seq = 0;
                g_ws_prev_hash = "";
                delete g_ws;
                g_ws = new wss_client();
            }

            g_ws->clear_access_channels(websocketpp::log::alevel::all);
            g_ws->clear_error_channels(websocketpp::log::elevel::all);

            g_ws->init_asio();
            g_ws->set_tls_init_handler(&on_tls_init);
            g_ws->set_open_handler(&on_ws_open);
            g_ws->set_message_handler(&on_ws_message);
            g_ws->set_close_handler(&on_ws_close);
            g_ws->set_fail_handler(&on_ws_fail);

            websocketpp::lib::error_code ec;
            wss_client::connection_ptr con = g_ws->get_connection(uri, ec);
            if (ec) {
                std::cout << "[WS] Connection init error: " << ec.message() << "\n";
            }
            else {
                g_ws->connect(con);
                std::cout << "[WS] Connecting to " << uri << " ...\n";
                g_ws->run();
            }
        }
        catch (const std::exception& e) {
            std::cout << "[WS] Exception: " << e.what() << "\n";
        }

        g_ws_connected = false;
        std::cout << "[WS] Reconnecting in 5 seconds...\n";
        Sleep(5000);
    }
}

HWND g_usbWindow = NULL;

LRESULT CALLBACK usb_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DEVICECHANGE) {
        DEV_BROADCAST_HDR* hdr = (DEV_BROADCAST_HDR*)lParam;

        if (wParam == DBT_DEVICEARRIVAL && hdr && hdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
            DEV_BROADCAST_VOLUME* vol = (DEV_BROADCAST_VOLUME*)hdr;
            char drive = 0;
            for (int i = 0; i < 26; i++) {
                if (vol->dbcv_unitmask & (1 << i)) { drive = 'A' + i; break; }
            }
            std::cout << "[USB] Device connected: drive " << drive << ":\n";
            json detail;
            detail["drive"] = std::string(1, drive) + ":";
            ws_send_event("usb_connected", detail);
        }
        else if (wParam == DBT_DEVICEREMOVECOMPLETE && hdr && hdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
            DEV_BROADCAST_VOLUME* vol = (DEV_BROADCAST_VOLUME*)hdr;
            char drive = 0;
            for (int i = 0; i < 26; i++) {
                if (vol->dbcv_unitmask & (1 << i)) { drive = 'A' + i; break; }
            }
            std::cout << "[USB] Device disconnected: drive " << drive << ":\n";
            json detail;
            detail["drive"] = std::string(1, drive) + ":";
            ws_send_event("usb_disconnected", detail);
        }

        return TRUE;
    }

    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void usb_monitor_thread()
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = usb_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"ExamUSBMonitor";
    RegisterClassW(&wc);

    // WM_DEVICECHANGE requires a top-level window, not HWND_MESSAGE
    g_usbWindow = CreateWindowExW(
        0, L"ExamUSBMonitor", L"",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        NULL, NULL, GetModuleHandleW(NULL), NULL
    );

    if (!g_usbWindow) {
        std::cout << "[USB] Failed to create hidden window\n";
        return;
    }

    DEV_BROADCAST_DEVICEINTERFACE filter = {};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

    HDEVNOTIFY hNotify = RegisterDeviceNotificationW(
        g_usbWindow, &filter,
        DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES
    );

    if (!hNotify) {
        std::cout << "[USB] RegisterDeviceNotification failed: " << GetLastError() << "\n";
        DestroyWindow(g_usbWindow);
        return;
    }

    std::cout << "[USB] USB device monitor started\n";

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterDeviceNotification(hNotify);
    std::cout << "[USB] USB device monitor stopped\n";
}

static std::string get_process_name(DWORD pid)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return "";

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                CloseHandle(hSnap);
                char buf[260] = {};
                WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, buf, sizeof(buf), NULL, NULL);
                return std::string(buf);
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return "";
}

void network_monitor_thread()
{
    std::cout << "[NET] Network connection monitor started\n";

    while (true) {
        Sleep(10000);
        if (!g_ws_connected) continue;

        DWORD size = 0;
        GetExtendedTcpTable(NULL, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (size == 0) continue;

        std::vector<BYTE> buffer(size);
        DWORD result = GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (result != NO_ERROR) continue;

        MIB_TCPTABLE_OWNER_PID* table = (MIB_TCPTABLE_OWNER_PID*)buffer.data();

        json connections = json::array();

        for (DWORD i = 0; i < table->dwNumEntries; i++) {
            MIB_TCPROW_OWNER_PID& row = table->table[i];

            if (row.dwState != MIB_TCP_STATE_ESTAB) continue;

            IN_ADDR remoteAddr;
            remoteAddr.S_un.S_addr = row.dwRemoteAddr;

            json conn;
            conn["process"] = get_process_name(row.dwOwningPid);
            conn["pid"] = (int)row.dwOwningPid;
            conn["remote_ip"] = std::string(inet_ntoa(remoteAddr));
            conn["remote_port"] = ntohs((u_short)row.dwRemotePort);
            connections.push_back(conn);
        }

        json detail;
        detail["connections"] = connections;
        detail["count"] = (int)connections.size();
        ws_send_event("tcp_snapshot", detail);
    }
}

void heartbeat_thread()
{
    while (true) {
        Sleep(5000);
        if (!g_ws_connected) continue;

        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        json detail;
        detail["timestamp"] = epoch;
        ws_send_event("heartbeat", detail);
    }
}

// ===================== STUDENT TIMER OVERLAY =====================

#define IDC_FINISH_BTN 5001
#define WM_TIMER_TICK  (WM_USER + 100)

static HWND g_overlay_hwnd = NULL;
static HFONT g_timer_font = NULL;
static HFONT g_btn_font = NULL;

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_timer_font = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
        g_btn_font = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        HWND btn = CreateWindowExW(0, L"BUTTON", L"Finish Exam",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 50, 180, 30, hwnd, (HMENU)IDC_FINISH_BTN, NULL, NULL);
        SendMessageW(btn, WM_SETFONT, (WPARAM)g_btn_font, TRUE);

        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    }

    case WM_TIMER:
    {
        int sec = g_remaining_seconds.load();
        if (sec > 0) {
            g_remaining_seconds = sec - 1;
        }
        InvalidateRect(hwnd, NULL, TRUE);

        if (g_exam_ended || g_student_finished) {
            KillTimer(hwnd, 1);
            EnableWindow(GetDlgItem(hwnd, IDC_FINISH_BTN), FALSE);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(24, 24, 27));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, g_timer_font);

        int sec = g_remaining_seconds.load();
        int h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
        wchar_t timeStr[32];
        if (h > 0)
            swprintf_s(timeStr, L"%d:%02d:%02d", h, m, s);
        else
            swprintf_s(timeStr, L"%02d:%02d", m, s);

        if (g_exam_ended) {
            SetTextColor(hdc, RGB(248, 113, 113));
            wcscpy_s(timeStr, L"ENDED");
        }
        else if (g_student_finished) {
            SetTextColor(hdc, RGB(96, 165, 250));
            wcscpy_s(timeStr, L"FINISHED");
        }
        else if (sec < 300) {
            SetTextColor(hdc, RGB(251, 191, 36));
        }
        else {
            SetTextColor(hdc, RGB(167, 243, 208));
        }

        RECT textRc = { 0, 8, rc.right, 48 };
        DrawTextW(hdc, timeStr, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_FINISH_BTN) {
            int result = MessageBoxW(hwnd,
                L"Are you sure you want to finish the exam?\n\nYou cannot undo this action.",
                L"Finish Exam", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);
            if (result == IDYES) {
                g_student_finished = true;
                json detail;
                detail["reason"] = "student_finished";
                ws_send_event("student_finished", detail);
                EnableWindow(GetDlgItem(hwnd, IDC_FINISH_BTN), FALSE);
                std::thread(upload_student_work).detach();
            }
        }
        return 0;

    case WM_CLOSE:
        return 0;

    case WM_DESTROY:
        if (g_timer_font) { DeleteObject(g_timer_font); g_timer_font = NULL; }
        if (g_btn_font) { DeleteObject(g_btn_font); g_btn_font = NULL; }
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void timer_overlay_thread()
{
    while (!g_logged_in) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"ExamTimerOverlay";
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int winW = 200, winH = 90;
    int x = screenW - winW - 20, y = 20;

    g_overlay_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ExamTimerOverlay", L"Exam Timer",
        WS_POPUP | WS_VISIBLE,
        x, y, winW, winH,
        NULL, NULL, GetModuleHandleW(NULL), NULL);

    ShowWindow(g_overlay_hwnd, SW_SHOW);
    UpdateWindow(g_overlay_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ===================== END TIMER OVERLAY =====================

int main()
{
    bool protected_process = protect_process();
    cout << "protected: " << protected_process << endl;
    std::cout << "Exam started\n";

    init_user_profile_path();
    CreateDirectoryW((g_user_profile + L"\\Desktop\\your_work").c_str(), NULL);
    CreateDirectoryW((g_user_profile + L"\\Desktop\\subject").c_str(), NULL);

    threads.push_back(thread(monitor_clipboard_thread));
    threads.push_back(thread(rules_monitor_thread));
    threads.push_back(thread(logoff_monitor_thread));
    threads.push_back(thread(websocket_thread));
    threads.push_back(thread(heartbeat_thread));
    threads.push_back(thread(usb_monitor_thread));
    threads.push_back(thread(network_monitor_thread));
    threads.push_back(thread(timer_overlay_thread));
    /*
    threads.push_back(thread(work_analyzer_thread, g_user_profile,
        [](const std::string& type, const json& detail) {
            ws_send_event(type, detail);
        }
    ));
    */

    for (size_t i = 0; i < threads.size(); i++) {
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
