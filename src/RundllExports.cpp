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
    // スリープを防ぐ
    ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    FILETIME ftEstimated, ftNow;
    GetEpgTimeAsFileTime(&ftEstimated);

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
    GetEpgTimeAsFileTime(&ftNow);
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
    TCHAR moduleFileName[MAX_PATH];
    if (GetLongModuleFileName(g_hinstDLL, moduleFileName, MAX_PATH)) {
        // このメソッドの処理中はDelayedSuspendW()させない
        CMutex moduleMutex(MODULE_ID);
        if (!moduleMutex.m_hMutex) goto EXIT;

        if (!lpszCmdLine || !lpszCmdLine[0]) goto EXIT;

        // 補欠のドライバで起動すべきか確認する
        TCHAR optionToReplace[MAX_PATH + 8], subDriverName[MAX_PATH];
        optionToReplace[0] = subDriverName[0] = 0;
        TCHAR iniFileName[MAX_PATH];
        ::lstrcpy(iniFileName, moduleFileName);
        if (::PathRenameExtension(iniFileName, TEXT(".ini"))) {
            FILETIME ft, ftNow;
            GetEpgTimeAsFileTime(&ftNow);
            TCHAR times[128];
            ::GetPrivateProfileString(TEXT("Settings"), TEXT("SubDriverUseTimes"), TEXT(""), times, ARRAY_SIZE(times), iniFileName);
            for (int i = 0; times[i] && StrToFileTime(times + i + 1, &ft);) {
                // 前後計4分だけマージンをとる
                if (-FILETIME_MINUTE < ftNow - ft && ftNow - ft < 3 * FILETIME_MINUTE) {
                    ::lstrcpy(optionToReplace, TEXT(" /D \""));
                    ::GetPrivateProfileString(TEXT("Settings"), TEXT("Driver"), TEXT(""), optionToReplace + 5, MAX_PATH, iniFileName);
                    ::lstrcat(optionToReplace, TEXT("\""));
                    ::GetPrivateProfileString(TEXT("Settings"), TEXT("SubDriver"), TEXT(""), subDriverName, MAX_PATH, iniFileName);
                    break;
                }
                i += 1 + ::StrCSpn(times + i + 1, TEXT("/"));
            }
        }

        // スリープを防ぐ
        if (::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED) == 0) {
            // AwayMode未対応
            ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        }
        for (int i = ::StrToInt(lpszCmdLine); i > 0; i--) {
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

        TCHAR *replacedCmdLine = NULL;
        if (subDriverName[0] && ::lstrlen(optionToReplace) > 6) {
            // 補欠のドライバで起動するよう起動オプションを置換する
            LPCTSTR pFound = ::StrStrI(lpszCmdLine, optionToReplace);
            if (pFound) {
                replacedCmdLine = new TCHAR[::lstrlen(lpszCmdLine) + ::lstrlen(subDriverName) + 1];
                ::lstrcpyn(replacedCmdLine, lpszCmdLine, static_cast<int>(pFound + 6 - lpszCmdLine));
                ::lstrcat(replacedCmdLine, subDriverName);
                ::lstrcat(replacedCmdLine, pFound + ::lstrlen(optionToReplace) - 1);
                lpszCmdLine = replacedCmdLine;
            }
        }

        // カレントをプラグインフォルダに移動
        if (::PathRemoveFileSpec(moduleFileName) && ::SetCurrentDirectory(moduleFileName)) {
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

        delete [] replacedCmdLine;
    }

EXIT:
    ::SetThreadExecutionState(ES_CONTINUOUS);
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
