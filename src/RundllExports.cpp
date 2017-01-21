#include <Windows.h>
#include <Shlwapi.h>
#include <powrprof.h>

extern HINSTANCE g_hinstDLL;

extern "C" __declspec(dllexport) void CALLBACK DelayedSuspendW(HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow)
{
    if (!lpszCmdLine || !lpszCmdLine[0]) return;
    ::Sleep(::StrToInt(lpszCmdLine) * 1000);
    
    if (!(lpszCmdLine = ::StrChr(lpszCmdLine, TEXT(' ')))) return;
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
    if (!lpszCmdLine || !lpszCmdLine[0]) return;
    ::Sleep(::StrToInt(lpszCmdLine) * 1000);
    
    if (!(lpszCmdLine = ::StrChr(lpszCmdLine, TEXT(' ')))) return;
    lpszCmdLine++;

    // カレントをプラグインフォルダに移動
    WCHAR moduleDir[MAX_PATH];
    if (!::GetModuleFileNameW(g_hinstDLL, moduleDir, MAX_PATH) ||
        !::PathRemoveFileSpecW(moduleDir) || 
        !::SetCurrentDirectoryW(moduleDir)) return;

    STARTUPINFO si;
    PROCESS_INFORMATION ps;
    ::GetStartupInfoW(&si);
    if (!::CreateProcessW(NULL, lpszCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &ps)) {
        ::MessageBoxW(NULL, lpszCmdLine, L"TTRec: 起動に失敗しました", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return;
    }
}
