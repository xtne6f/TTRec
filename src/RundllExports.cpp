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
    FILETIME ftEstimated, ftNow;
    GetLocalTimeAsFileTime(&ftEstimated);

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
        ftEstimated += FILETIME_SECOND;
    }
    if (hMutex) return;

    if (!lpszCmdLine || !lpszCmdLine[0]) return;

    for (int i = ::StrToInt(lpszCmdLine); i > 0; i--) {
        ::SetThreadExecutionState(ES_SYSTEM_REQUIRED);
        ::Sleep(1000);
        ftEstimated += FILETIME_SECOND;
    }

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

    // 実行にかかった時間が極端におかしいときは中止する
    GetLocalTimeAsFileTime(&ftNow);
    if (ftNow - ftEstimated < -FILETIME_MINUTE || FILETIME_MINUTE < ftNow - ftEstimated) {
        return;
    }

    ::SetSuspendState(!::ChrCmpI(lpszCmdLine[0], TEXT('H')) ? TRUE : FALSE,
                      !::ChrCmpI(lpszCmdLine[1], TEXT('F')) ? TRUE : FALSE, FALSE);
}

static void ShowBalloonTip(LPCTSTR text, int notifyLevel);

extern "C" __declspec(dllexport) void CALLBACK DelayedExecuteW(HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow)
{
    LPCTSTR notifyText = TEXT("TVTestの起動に失敗しました。");
    int notifyLevel = 1;
    {
        // このメソッドの処理中はDelayedSuspendW()させない
        CMutex moduleMutex(MODULE_ID);
        if (!moduleMutex.m_hMutex) goto EXIT;

        if (!lpszCmdLine || !lpszCmdLine[0]) goto EXIT;

        for (int i = ::StrToInt(lpszCmdLine); i > 0; i--) {
            ::SetThreadExecutionState(ES_SYSTEM_REQUIRED);
            ::Sleep(1000);
        }

        // 同名のプラグインが有効化されていれば起動しない
        TCHAR name[MAX_PATH];
        if (!GetIdentifierFromModule(g_hinstDLL, name, MAX_PATH)) goto EXIT;
        HANDLE hMutex = ::OpenMutex(MUTEX_ALL_ACCESS, FALSE, name);
        if (hMutex) {
            ::CloseHandle(hMutex);
            notifyText = TEXT("TVTestは既に起動しています。");
            notifyLevel = 3;
            goto EXIT;
        }

        if ((lpszCmdLine = ::StrChr(lpszCmdLine, TEXT(' '))) == NULL) goto EXIT;
        lpszCmdLine++;

        // カレントをプラグインフォルダに移動
        TCHAR moduleDir[MAX_PATH];
        if (::GetModuleFileName(g_hinstDLL, moduleDir, MAX_PATH) &&
            ::PathRemoveFileSpec(moduleDir) &&
            ::SetCurrentDirectory(moduleDir))
        {
            STARTUPINFO si = { sizeof(si) };
            PROCESS_INFORMATION ps;
            if (::CreateProcess(NULL, lpszCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &ps)) {
                if (::WaitForInputIdle(ps.hProcess, 60000) == 0) {
                    notifyText = TEXT("TVTestを起動しました。");
                    notifyLevel = 3;
                }
                ::CloseHandle(ps.hThread);
                ::CloseHandle(ps.hProcess);
            }
        }
    }

EXIT:
    ShowBalloonTip(notifyText, notifyLevel);
}

// 必要であればバルーンチップを表示する
static void ShowBalloonTip(LPCTSTR text, int notifyLevel)
{
    TCHAR iniFileName[MAX_PATH];
    if (GetLongModuleFileName(g_hinstDLL, iniFileName, MAX_PATH) &&
        ::PathRenameExtension(iniFileName, TEXT(".ini")))
    {
        CBalloonTip balloonTip;
        int level = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("NotifyLevel"), 0, iniFileName);
        if (notifyLevel <= level && balloonTip.Initialize(NULL, g_hinstDLL)) {
            balloonTip.Show(text, TEXT("TTRec (タスクスケジューラ)"), NULL,
                            notifyLevel == 1 ? CBalloonTip::ICON_WARNING : CBalloonTip::ICON_INFO);
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
