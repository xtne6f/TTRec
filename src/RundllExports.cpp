#include <Windows.h>
#include <Shlwapi.h>
#include <powrprof.h>
#include "Util.h"

extern HINSTANCE g_hinstDLL;

// CloseHandle()を確実に呼ぶため
class CMutex
{
public:
    HANDLE m_hMutex;
    bool m_alreadyExists;

    CMutex(LPCTSTR name) : m_hMutex(NULL), m_alreadyExists(false) {
        m_hMutex = CreateFullAccessMutex(FALSE, name);
        m_alreadyExists = m_hMutex && ::GetLastError() == ERROR_ALREADY_EXISTS;
    }
    ~CMutex() {
        if (m_hMutex) ::CloseHandle(m_hMutex);
    }
};

extern "C" __declspec(dllexport) void CALLBACK DelayedSuspendW(HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow)
{
    // このメソッドは2重起動禁止
    CMutex suspendMutex(SUSPEND_ID);
    if (!suspendMutex.m_hMutex || suspendMutex.m_alreadyExists) return;

    // プラグイン全体で1つでも有効なものがあればスリープしない
    // 20秒だけ待ってみる
    HANDLE hMutex = NULL;
    for (int i = 0; i < 20; i++) {
        hMutex = ::OpenMutex(MUTEX_ALL_ACCESS, FALSE, MODULE_ID);
        if (!hMutex) break;
        ::CloseHandle(hMutex);
        ::Sleep(1000);
    }
    if (hMutex) return;

    if (!lpszCmdLine || !lpszCmdLine[0]) return;
    ::Sleep(::StrToInt(lpszCmdLine) * 1000);

    // 最終確認
    hMutex = ::OpenMutex(MUTEX_ALL_ACCESS, FALSE, MODULE_ID);
    if (hMutex) {
        ::CloseHandle(hMutex);
        return;
    }

    if ((lpszCmdLine = ::StrChr(lpszCmdLine, TEXT(' '))) == NULL) return;
    lpszCmdLine++;

    if (lpszCmdLine[0] != L'S' && lpszCmdLine[0] != L'H') return;

    // SeShutdownPrivilege有効化
    HANDLE hToken;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LUID luid;
        if (::LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &luid)) {
            TOKEN_PRIVILEGES tp;
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            ::AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
        }
        ::CloseHandle(hToken);
    }

    ::SetSuspendState(lpszCmdLine[0] == L'H' ? TRUE : FALSE,
                      lpszCmdLine[1] == L'F' ? TRUE : FALSE, FALSE);
}

extern "C" __declspec(dllexport) void CALLBACK DelayedExecuteW(HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow)
{
    CMutex moduleMutex(MODULE_ID);
    if (!moduleMutex.m_hMutex) return;

    if (!lpszCmdLine || !lpszCmdLine[0]) return;
    ::Sleep(::StrToInt(lpszCmdLine) * 1000);

    // 同名のプラグインが有効化されていれば起動しない
    TCHAR name[MAX_PATH];
    if (!GetIdentifierFromModule(g_hinstDLL, name, MAX_PATH)) return;
    HANDLE hMutex = ::OpenMutex(MUTEX_ALL_ACCESS, FALSE, name);
    if (hMutex) {
        ::CloseHandle(hMutex);
        return;
    }

    if ((lpszCmdLine = ::StrChr(lpszCmdLine, TEXT(' '))) == NULL) return;
    lpszCmdLine++;

    // カレントをプラグインフォルダに移動
    WCHAR moduleDir[MAX_PATH];
    if (!::GetModuleFileNameW(g_hinstDLL, moduleDir, MAX_PATH) ||
        !::PathRemoveFileSpecW(moduleDir) || 
        !::SetCurrentDirectoryW(moduleDir)) return;

    STARTUPINFO si;
    PROCESS_INFORMATION ps;
    si.dwFlags = 0;
    ::GetStartupInfoW(&si);
    if (!::CreateProcessW(NULL, lpszCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &ps)) {
        ::MessageBoxW(NULL, lpszCmdLine, L"TTRec: 起動に失敗しました", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return;
    }
    ::WaitForInputIdle(ps.hProcess, 20000);
}
