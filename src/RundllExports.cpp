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
    CMutex(LPCTSTR name) {
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

    if (::ChrCmpI(lpszCmdLine[0], TEXT('S')) && ::ChrCmpI(lpszCmdLine[0], TEXT('H'))) return;

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

    ::SetSuspendState(!::ChrCmpI(lpszCmdLine[0], TEXT('H')) ? TRUE : FALSE,
                      !::ChrCmpI(lpszCmdLine[1], TEXT('F')) ? TRUE : FALSE, FALSE);
}

extern "C" __declspec(dllexport) void CALLBACK DelayedExecuteW(HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow)
{
    bool fStarted = false;
    {
        // このメソッドの処理中はDelayedSuspendW()させない
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
        TCHAR moduleDir[MAX_PATH];
        if (::GetModuleFileName(g_hinstDLL, moduleDir, MAX_PATH) &&
            ::PathRemoveFileSpec(moduleDir) &&
            ::SetCurrentDirectory(moduleDir))
        {
            STARTUPINFO si;
            PROCESS_INFORMATION ps;
            si.dwFlags = 0;
            ::GetStartupInfo(&si);
            if (::CreateProcess(NULL, lpszCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &ps)) {
                fStarted = ::WaitForInputIdle(ps.hProcess, 20000) == 0;
            }
        }
    }

    // 必要であればバルーンチップを表示
    TCHAR iniFileName[MAX_PATH];
    if (GetLongModuleFileName(g_hinstDLL, iniFileName, MAX_PATH) &&
        ::PathRenameExtension(iniFileName, TEXT(".ini")))
    {
        CBalloonTip balloonTip;
        int notifyLevel = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("NotifyLevel"), 0, iniFileName);
        if ((notifyLevel >= 3 || notifyLevel >= 1 && !fStarted) &&
            balloonTip.Initialize(NULL, g_hinstDLL))
        {
            balloonTip.Show(fStarted ? TEXT("TVTestを起動しました。") : TEXT("TVTestの起動に失敗しました。"),
                            TEXT("TTRec (タスクスケジューラ)"), NULL,
                            fStarted ? CBalloonTip::ICON_INFO : CBalloonTip::ICON_WARNING);

            if (::SetTimer(balloonTip.GetHandle(), 1, 10000, NULL)) {
                MSG msg;
                while (::GetMessage(&msg, NULL, 0, 0) > 0) {
                    if (msg.hwnd && msg.hwnd == balloonTip.GetHandle() && msg.message == WM_TIMER) {
                        if (msg.wParam == 1) {
                            ::KillTimer(balloonTip.GetHandle(), 1);
                            balloonTip.Hide();
                            if (!::SetTimer(balloonTip.GetHandle(), 2, 5000, NULL)) {
                                balloonTip.Finalize();
                                ::PostQuitMessage(0);
                            }
                        }
                        else {
                            balloonTip.Finalize();
                            ::PostQuitMessage(0);
                        }
                    }
                    ::DispatchMessage(&msg);
                }
            }
        }
    }
}
