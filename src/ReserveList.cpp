#include <Windows.h>
#include <Shlwapi.h>
#include <MSTask.h>
#include <taskschd.h>
#include <Lmcons.h>
#include "resource.h"
#include "Util.h"
#include "RecordingOption.h"
#include "ReserveList.h"


CReserveList::CReserveList()
    : m_head(NULL)
    , m_hThread(NULL)
{
    m_saveFileName[0] = 0;
    m_saveTaskName[0] = 0;
    m_pluginPath[0] = 0;
}


CReserveList::~CReserveList()
{
    Clear();
    if (m_hThread) {
        if (::WaitForSingleObject(m_hThread, 30000) != WAIT_OBJECT_0) {
            ::TerminateThread(m_hThread, 0);
        }
        ::CloseHandle(m_hThread);
    }
}


void CReserveList::Clear()
{
    while (m_head) {
        RESERVE *pRes = m_head;
        m_head = pRes->next;
        delete pRes;
    }
}


// strには少なくとも1024要素の確保が必要
void CReserveList::ToString(const RESERVE &res, LPTSTR str)
{
    TCHAR szStartTime[64];
    TCHAR szDuration[64];

    FileTimeToStr(&res.startTime, szStartTime);
    TimeSpanToStr(res.duration, szDuration);
    int len = ::wsprintf(str, TEXT("0x%04X\t0x%04X\t0x%04X\t0x%04X\t%s\t%s%s%s\t%s\t"),
                         res.networkID, res.transportStreamID, res.serviceID, res.eventID,
                         szStartTime, szDuration, res.updateByPf ? TEXT("!") : TEXT(""),
                         res.isEnabled ? TEXT("") : TEXT("#"), res.eventName);
    res.recOption.ToString(str + len);
}


bool CReserveList::Insert(const RESERVE &in)
{
    RESERVE *pRes;
    RESERVE *prev = NULL;

    // 同じ予約がないか調べる
    for (pRes = m_head; pRes; pRes = pRes->next) {
        if (pRes->eventID == in.eventID &&
            pRes->networkID == in.networkID &&
            pRes->transportStreamID == in.transportStreamID &&
            pRes->serviceID == in.serviceID) break;
        prev = pRes;
    }

    if (pRes) {
        // 一度リストから切り離す
        if (!prev) m_head = pRes->next;
        else prev->next = pRes->next;
    }
    else {
        pRes = new RESERVE;
    }
    *pRes = in;

    // 入力チェック
    ReplaceTokenDelimiters(pRes->eventName);
    ReplaceTokenDelimiters(pRes->recOption.saveDir);
    ReplaceTokenDelimiters(pRes->recOption.saveName);

    if (!m_head) {
        pRes->next = NULL;
        m_head = pRes;
    }
    else if (m_head->GetTrimmedStartTime() - pRes->GetTrimmedStartTime() > 0) {
        // 予約をリストの先頭に挿入
        pRes->next = m_head;
        m_head = pRes;
    }
    else {
        // 予約をリストに挿入
        RESERVE *tail;
        for (tail = m_head; tail->next; tail = tail->next) {
            if (tail->next->GetTrimmedStartTime() - pRes->GetTrimmedStartTime() > 0) break;
        }
        pRes->next = tail->next;
        tail->next = pRes;
    }

    return true;
}


bool CReserveList::Insert(LPCTSTR str)
{
    RESERVE res;
    int i = 0;

    ::StrToIntEx(str, STIF_SUPPORT_HEX, &i);
    res.networkID = static_cast<WORD>(i);
    if (!NextToken(&str)) return false;

    ::StrToIntEx(str, STIF_SUPPORT_HEX, &i);
    res.transportStreamID = static_cast<WORD>(i);
    if (!NextToken(&str)) return false;

    ::StrToIntEx(str, STIF_SUPPORT_HEX, &i);
    res.serviceID = static_cast<WORD>(i);
    if (!NextToken(&str)) return false;

    ::StrToIntEx(str, STIF_SUPPORT_HEX, &i);
    res.eventID = static_cast<WORD>(i);
    if (!NextToken(&str)) return false;

    if (!StrToFileTime(str, &res.startTime)) return false;
    if (!NextToken(&str)) return false;

    LPCTSTR endptr;
    if (!StrToTimeSpan(str, &res.duration, &endptr)) return false;
    res.updateByPf = *endptr == TEXT('!') ? 1 : 0;
    res.isEnabled = endptr[*endptr == TEXT('!') ? 1 : 0] != TEXT('#');
    if (!NextToken(&str)) return false;

    GetToken(str, res.eventName, ARRAY_SIZE(res.eventName));
    if (!NextToken(&str)) return false;

    if (!res.recOption.FromString(str)) return false;

    return Insert(res);
}


bool CReserveList::Insert(HINSTANCE hInstance, HWND hWndParent, const RESERVE &in,
                          const RECORDING_OPTION &defaultRecOption, LPCTSTR serviceName, LPCTSTR captionSuffix)
{
    DIALOG_PARAMS prms = { in, &defaultRecOption, serviceName, captionSuffix };
    INT_PTR rv = ::DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_RESERVATION), hWndParent,
                                  DlgProc, reinterpret_cast<LPARAM>(&prms));
    if (rv == IDC_DISABLE) {
        const RESERVE *pRes = Get(prms.res.networkID, prms.res.transportStreamID, prms.res.serviceID, prms.res.eventID);
        if (!pRes) return false;
        RESERVE res = *pRes;
        res.isEnabled = !res.isEnabled;
        return Insert(res);
    }
    return rv == IDOK ? Insert(prms.res) :
           rv == IDC_DELETE ? Delete(prms.res.networkID, prms.res.transportStreamID,
                                     prms.res.serviceID, prms.res.eventID) : false;
}


INT_PTR CALLBACK CReserveList::DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_INITDIALOG:
        {
            DIALOG_PARAMS *pPrms = reinterpret_cast<DIALOG_PARAMS*>(lParam);
            RESERVE *pRes = &pPrms->res;
            ::SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pRes));

            // キャプションをいじる
            TCHAR cap[128];
            if (pPrms->captionSuffix && pPrms->captionSuffix[0] && ::GetWindowText(hDlg, cap, 32)) {
                ::lstrcpyn(cap + ::lstrlen(cap), pPrms->captionSuffix, 32);
                ::SetWindowText(hDlg, cap);
            }

            if (pPrms->serviceName && pPrms->serviceName[0]) {
                ::SetDlgItemText(hDlg, IDC_STATIC_SERVICE_NAME, pPrms->serviceName);
            }

            SYSTEMTIME sysTime;
            FILETIME time = pRes->GetTrimmedStartTime();
            ::FileTimeToSystemTime(&time, &sysTime);
            TCHAR text[512];
            ::wsprintf(text, TEXT("%d年%d月%d日(%s) %d時%d分%d秒 から %d分%d秒間%s"),
                       sysTime.wYear, sysTime.wMonth, sysTime.wDay, GetDayOfWeekText(sysTime.wDayOfWeek),
                       sysTime.wHour, sysTime.wMinute, sysTime.wSecond,
                       pRes->GetTrimmedDuration() / 60, pRes->GetTrimmedDuration() % 60,
                       pRes->updateByPf == 2 ? TEXT(" 【p/f延長中】") : pRes->updateByPf ? TEXT(" 【p/f更新あり】") : TEXT(""));
            ::SetDlgItemText(hDlg, IDC_STATIC_RES_TIME, text);

            ::SendDlgItemMessage(hDlg, IDC_EDIT_EVENT_NAME, EM_LIMITTEXT, ARRAY_SIZE(pRes->eventName) - 1, 0);
            if (pRes->eventName[0] == PREFIX_EPGORIGIN) {
                ::SetDlgItemText(hDlg, IDC_EDIT_EVENT_NAME, pRes->eventName + 1);
                ::EnableWindow(::GetDlgItem(hDlg, IDC_STATIC_EVENT_NAME), FALSE);
            }
            else {
                ::SetDlgItemText(hDlg, IDC_EDIT_EVENT_NAME, pRes->eventName);
            }

            if (!pRes->isEnabled) ::SetDlgItemText(hDlg, IDC_DISABLE, TEXT("有効にする"));

            return pRes->recOption.DlgProc(hDlg, uMsg, wParam, true, pPrms->pDefaultRecOption);
        }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_EDIT_EVENT_NAME:
            if (HIWORD(wParam) == EN_CHANGE) {
                if (!::IsWindowEnabled(::GetDlgItem(hDlg, IDC_STATIC_EVENT_NAME))) {
                    ::EnableWindow(::GetDlgItem(hDlg, IDC_STATIC_EVENT_NAME), TRUE);
                }
            }
            break;
        case IDOK:
            {
                RESERVE *pRes = reinterpret_cast<RESERVE*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
                if (!::IsWindowEnabled(::GetDlgItem(hDlg, IDC_STATIC_EVENT_NAME))) {
                    pRes->eventName[0] = PREFIX_EPGORIGIN;
                    if (!::GetDlgItemText(hDlg, IDC_EDIT_EVENT_NAME, pRes->eventName + 1, ARRAY_SIZE(pRes->eventName) - 1)) {
                        pRes->eventName[0] = 0;
                    }
                }
                else {
                    if (!::GetDlgItemText(hDlg, IDC_EDIT_EVENT_NAME, pRes->eventName, ARRAY_SIZE(pRes->eventName))) {
                        pRes->eventName[0] = 0;
                    }
                }
                pRes->recOption.DlgProc(hDlg, uMsg, wParam, true);
            }
            // fall through!
        case IDC_DISABLE:
        case IDC_DELETE:
        case IDCANCEL:
            ::EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        default:
            {
                RESERVE *pRes = reinterpret_cast<RESERVE*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
                return pRes->recOption.DlgProc(hDlg, uMsg, wParam, true);
            }
        }
        break;
    }
    return FALSE;
}


bool CReserveList::Delete(DWORD networkID, DWORD transportStreamID, DWORD serviceID, DWORD eventID)
{
    RESERVE *tail;
    RESERVE *prev = NULL;

    for (tail = m_head; tail; tail = tail->next) {
        if (tail->eventID == eventID &&
            tail->networkID == networkID &&
            tail->transportStreamID == transportStreamID &&
            tail->serviceID == serviceID) break;
        prev = tail;
    }

    if (!tail) return false;

    if (!prev) m_head = tail->next;
    else prev->next = tail->next;
    delete tail;
    return true;
}


const RESERVE *CReserveList::Get(DWORD networkID, DWORD transportStreamID, DWORD serviceID, DWORD eventID) const
{
    RESERVE *tail = m_head;
    for (; tail; tail = tail->next) {
        if (tail->eventID == eventID &&
            tail->networkID == networkID &&
            tail->transportStreamID == transportStreamID &&
            tail->serviceID == serviceID) break;
    }
    return tail;
}


const RESERVE *CReserveList::Get(int index) const
{
    if (index < 0) return NULL;

    RESERVE *tail = m_head;
    for (int i = 0; i < index && tail; i++, tail = tail->next);
    return tail;
}


bool CReserveList::Load()
{
    if (!::PathFileExists(m_saveFileName)) {
        Clear();
        return true;
    }
    LPTSTR text = NULL;
    for (int i = 0; i < 5; ++i) {
        if ((text = NewReadTextFileToEnd(m_saveFileName, FILE_SHARE_READ)) != NULL) break;
        ::Sleep(200);
    }
    if (!text) return false;

    Clear();

    // Insertの効率のため(大して変わらんけど)逆順に読む
    LPCTSTR line = text + ::lstrlen(text);
    do {
        line = ::StrRChr(text, line, TEXT('\n'));
        if (!Insert(!line ? text : line + 1)) {
            DEBUG_OUT(TEXT("CReserveList::Load(): Insert Error\n"));
        }
    } while (line);

    delete [] text;
    return true;
}


bool CReserveList::Save() const
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 5; ++i) {
        hFile = ::CreateFile(m_saveFileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) break;
        ::Sleep(200);
    }
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD writtenBytes;
    WCHAR bom = L'\xFEFF';
    ::WriteFile(hFile, &bom, sizeof(bom), &writtenBytes, NULL);

    for (RESERVE *tail = m_head; tail; tail = tail->next) {
        TCHAR buf[1024 + 2];
        ToString(*tail, buf);
        ::lstrcat(buf, TEXT("\r\n"));
        ::WriteFile(hFile, buf, ::lstrlen(buf) * sizeof(TCHAR), &writtenBytes, NULL);
    }

    ::CloseHandle(hFile);
    return true;
}


#define GET_PRIORITY(x) ((x) % PRIORITY_MOD == PRIORITY_DEFAULT ? (x) + defaultRecOption.priority % PRIORITY_MOD : (x))
#define GET_START_MARGIN(x) ((x) < 0 ? defaultRecOption.startMargin : (x))


// 直近の予約を取得
RESERVE *CReserveList::GetNearest(const RECORDING_OPTION &defaultRecOption, RESERVE **pPrev, bool fEnabledOnly) const
{
    *pPrev = NULL;

    RESERVE *pMinRes = NULL;
    FILETIME minStart;
    minStart.dwLowDateTime = 0xFFFFFFFF;
    minStart.dwHighDateTime = 0x7FFFFFFF;

    // 開始マージンを含めてもっとも直近の予約を探す
    for (RESERVE *tail = m_head; tail; tail = tail->next) {
        FILETIME start = tail->GetTrimmedStartTime();
        start += -GET_START_MARGIN(tail->recOption.startMargin) * FILETIME_SECOND;

        // 同時刻の予約は優先度の高いものを選択
        if ((!fEnabledOnly || tail->isEnabled) && (minStart - start > 0 || minStart - start == 0 &&
            GET_PRIORITY(pMinRes->recOption.priority) < GET_PRIORITY(tail->recOption.priority)))
        {
            *pPrev = pMinRes;
            pMinRes = tail;
            minStart = start;
        }
        // リストは既ソートなのでMARGIN_MAX秒以上遅い予約を探す必要はない
        if (start - minStart > MARGIN_MAX * FILETIME_SECOND) break;
    }

    return pMinRes;
}


const RESERVE *CReserveList::GetNearest(const RECORDING_OPTION &defaultRecOption, bool fEnabledOnly) const
{
    RESERVE *prev;
    return GetNearest(defaultRecOption, &prev, fEnabledOnly);
}


// 直近の有効な予約を取得
// ・recOptionはデフォルト適用済みになる
// ・startTrimおよびendTrimは0に補正される(トリム済みになる)
// ・startTimeおよびdurationは予約の重複によって調整される場合がある
// ・eventNameのPREFIX_EPGORIGINは削除される
bool CReserveList::GetNearest(RESERVE *pRes, const RECORDING_OPTION &defaultRecOption, int readyOffset) const
{
    const RESERVE *pNearest = GetNearest(defaultRecOption, true);
    if (!pNearest || !pRes) return false;
    RESERVE &res = *pRes;
    res = *pNearest;
    ::lstrcpy(res.eventName, pNearest->eventName + (pNearest->eventName[0]==PREFIX_EPGORIGIN ? 1 : 0));
    res.recOption.ApplyDefault(defaultRecOption);

    // トリム済みにする
    res.startTime = res.GetTrimmedStartTime();
    res.duration = res.GetTrimmedDuration();
    res.recOption.startTrim = 0;
    res.recOption.endTrim = 0;

    // 終了マージンは録画時間を超えて負であってはならない
    if (res.recOption.endMargin < -res.duration) res.recOption.endMargin = -res.duration;

    FILETIME resEnd = res.startTime;
    resEnd += (res.duration + res.recOption.endMargin) * FILETIME_SECOND;

    // 優先度の高い別の予約があれば録画終了時刻を早める
    FILETIME resRealEnd = resEnd;
    for (const RESERVE *tail = m_head; tail; tail = tail->next) {
        FILETIME start = tail->GetTrimmedStartTime();
        // 録画はすぐに切り替わらないので、readyOffset秒の余裕をもたせる
        start += -(GET_START_MARGIN(tail->recOption.startMargin) + readyOffset) * FILETIME_SECOND;

        if (tail->isEnabled && res.recOption.priority < GET_PRIORITY(tail->recOption.priority) &&
            resRealEnd - start > 0) resRealEnd = start;

        if (start - resRealEnd > MARGIN_MAX * FILETIME_SECOND) break;
    }

    int diff = static_cast<int>((resEnd - resRealEnd) / FILETIME_SECOND);
    // 終了マージンを削る
    if (diff <= res.recOption.endMargin) {
        res.recOption.endMargin -= diff;
    }
    else {
        // 終了マージンが負のときdiffは増加する
        diff -= res.recOption.endMargin;
        res.recOption.endMargin = 0;
        // 録画時間を削る
        if (diff <= res.duration) {
            res.duration -= diff;
        }
        else {
            diff -= res.duration;
            res.duration = 0;
            // 開始マージンを削る(同時に録画開始時刻も変更する必要がある)
            if (diff > res.recOption.startMargin) diff = res.recOption.startMargin;
            res.recOption.startMargin -= diff;
            res.startTime += -diff * FILETIME_SECOND;
        }
    }

    return true;
}


// 直近の予約を削除
bool CReserveList::DeleteNearest(const RECORDING_OPTION &defaultRecOption, bool fEnabledOnly)
{
    RESERVE *prev;
    RESERVE *pRes = GetNearest(defaultRecOption, &prev, fEnabledOnly);
    if (!pRes) return false;

    if (!prev) m_head = pRes->next;
    else prev->next = pRes->next;
    delete pRes;
    return true;
}


void CReserveList::SetPluginFileName(LPCTSTR fileName)
{
    TCHAR saveFileName[MAX_PATH + 32];
    ::lstrcpy(saveFileName, fileName);
    ::PathRemoveExtension(saveFileName);
    ::lstrcat(saveFileName, TEXT("_Reserves"));

    ::lstrcpyn(m_saveTaskName, ::PathFindFileName(saveFileName), ARRAY_SIZE(m_saveTaskName));
    ::lstrcat(saveFileName, TEXT(".txt"));
    ::lstrcpyn(m_saveFileName, saveFileName, ARRAY_SIZE(m_saveFileName));

    ::lstrcpyn(m_pluginPath, fileName, ARRAY_SIZE(m_pluginPath));
}


bool CReserveList::RunSaveTask(bool fNoWakeViewOnly, int resumeMargin, int execWait, LPCTSTR appName, LPCTSTR driverName,
                               LPCTSTR appCmdOption, HWND hwndPost, UINT uMsgPost)
{
    if (!m_pluginPath[0] || !m_saveTaskName[0] || !appName[0] || !driverName[0]) return false;

    if (m_hThread) {
        // タスク登録は極めて長い時間がかかることがあるのでタイムアウトを設ける(録画失敗するよりはマシ)
        if (::WaitForSingleObject(m_hThread, 30000) != WAIT_OBJECT_0) {
            ::TerminateThread(m_hThread, 0);
        }
        ::CloseHandle(m_hThread);
        m_hThread = NULL;
    }

    // タスクスケジューラ登録に必要な情報をセット
    m_saveTask.resumeMargin = resumeMargin;
    m_saveTask.execWait = execWait;
    ::lstrcpyn(m_saveTask.appPath, appName, ARRAY_SIZE(m_saveTask.appPath));
    ::lstrcpyn(m_saveTask.driverName, driverName, ARRAY_SIZE(m_saveTask.driverName));
    ::lstrcpyn(m_saveTask.appCmdOption, appCmdOption, ARRAY_SIZE(m_saveTask.appCmdOption));
    m_saveTask.hwndPost = hwndPost;
    m_saveTask.uMsgPost = uMsgPost;
    ::lstrcpy(m_saveTask.saveTaskName, m_saveTaskName);
    ::lstrcpy(m_saveTask.saveTaskNameNoWake, m_saveTaskName);
    ::lstrcat(m_saveTask.saveTaskNameNoWake, TEXT("N"));
    ::lstrcpy(m_saveTask.pluginPath, m_pluginPath);
    RESERVE *tail = m_head;
    m_saveTask.resumeTimeNum = 0;
    int noWakeNum = 0;
    for (; tail && m_saveTask.resumeTimeNum < TASK_TRIGGER_MAX; tail = tail->next) {
        if (tail->isEnabled) {
            FILETIME resumeTime = tail->GetTrimmedStartTime();
            resumeTime += -resumeMargin * FILETIME_MINUTE;
            SYSTEMTIME st;
            if (::FileTimeToSystemTime(&resumeTime, &st)) {
                // 重複チェック
                bool fNoWake = fNoWakeViewOnly && tail->recOption.IsViewOnly();
                bool fCreate = true;
                for (int i = 0; i < m_saveTask.resumeTimeNum; i++) {
                    if (m_saveTask.resumeTime[i].wYear == st.wYear &&
                        m_saveTask.resumeTime[i].wMonth == st.wMonth &&
                        m_saveTask.resumeTime[i].wDay == st.wDay &&
                        m_saveTask.resumeTime[i].wHour == st.wHour &&
                        m_saveTask.resumeTime[i].wMinute == st.wMinute)
                    {
                        if (!fNoWake && m_saveTask.resumeIsNoWake[i]) {
                            m_saveTask.resumeIsNoWake[i] = false;
                            noWakeNum--;
                        }
                        fCreate = false;
                        break;
                    }
                }
                if (fCreate) {
                    if (fNoWake) {
                        if (noWakeNum < TASK_TRIGGER_NOWAKE_MAX) {
                            m_saveTask.resumeIsNoWake[m_saveTask.resumeTimeNum] = true;
                            m_saveTask.resumeTime[m_saveTask.resumeTimeNum++] = st;
                            noWakeNum++;
                        }
                    }
                    else {
                        m_saveTask.resumeIsNoWake[m_saveTask.resumeTimeNum] = false;
                        m_saveTask.resumeTime[m_saveTask.resumeTimeNum++] = st;
                    }
                }
            }
        }
    }

    m_hThread = ::CreateThread(NULL, 0, SaveTaskThread, &m_saveTask, 0, NULL);
    return m_hThread != NULL;
}

#define EXIT_ON_FAIL(hr)          {if (FAILED(hr)) goto EXIT;}
#define EXIT_ON_FAIL_TO_GET(hr,p) {if (FAILED(hr)) {p = NULL; goto EXIT;}}

DWORD WINAPI CReserveList::SaveTaskThread(LPVOID pParam)
{
    const CONTEXT_SAVE_TASK *pSaveTask = static_cast<CONTEXT_SAVE_TASK*>(pParam);
    bool fInitialized = false;
    HRESULT hr;
    ITaskScheduler *pScheduler = NULL;
    ITask *pTask = NULL;
    IPersistFile *pPersistFile = NULL;
    ITask *pTaskNoWake = NULL;
    IPersistFile *pPersistFileNoWake = NULL;
    // TaskScheduler 2.0
    ITaskService *pService = NULL;
    ITaskFolder *pTaskFolder = NULL;
    ITaskDefinition *pTaskDefinition = NULL;
    IRegistrationInfo *pRegistrationInfo = NULL;
    ITaskSettings *pTaskSettings = NULL;
    IActionCollection *pActionCollection = NULL;
    IAction *pAction = NULL;
    IExecAction *pExecAction = NULL;
    ITriggerCollection *pTriggerCollection = NULL;
    IRegisteredTask *pRegisteredTask = NULL;
    IRegisteredTask *pRegisteredTaskNoWake = NULL;

    // 実行パスを生成
    TCHAR rundllPath[MAX_PATH];
    // LNK2001:__chkstk対策のため
    LPTSTR pNewArgs = new TCHAR[MAX_PATH * 3 + CMD_OPTION_MAX + 64];
    hr = E_FAIL;
    if (GetRundll32Path(rundllPath)) {
        DWORD len = ::GetShortPathName(pSaveTask->pluginPath, pNewArgs, MAX_PATH);
        if (len && len < MAX_PATH) {
            len += ::wsprintf(&pNewArgs[len], TEXT(",DelayedExecute %d \""), pSaveTask->execWait);
            if (::PathRelativePathTo(&pNewArgs[len], pSaveTask->pluginPath, FILE_ATTRIBUTE_NORMAL, pSaveTask->appPath, FILE_ATTRIBUTE_NORMAL)) {
                len = ::lstrlen(pNewArgs);
                len += ::wsprintf(&pNewArgs[len], TEXT("\" /D \"%s\""), pSaveTask->driverName);
                if (pSaveTask->appCmdOption[0]) {
                    ::wsprintf(&pNewArgs[len], TEXT(" %s"), pSaveTask->appCmdOption);
                }
                hr = S_OK;
            }
        }
    }
    EXIT_ON_FAIL(hr);

    EXIT_ON_FAIL(hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    fInitialized = true;

    // TaskScheduler 2.0
    hr = ::CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, reinterpret_cast<void**>(&pService));
    if (FAILED(hr)) {
        pService = NULL;
        // TaskScheduler 1.0
        EXIT_ON_FAIL_TO_GET(hr = ::CoCreateInstance(CLSID_CTaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                                                    IID_ITaskScheduler, reinterpret_cast<void**>(&pScheduler)), pScheduler);
    }
    else {
        EXIT_ON_FAIL(hr = pService->Connect(CVariant(), CVariant(), CVariant(), CVariant()));
        EXIT_ON_FAIL_TO_GET(hr = pService->GetFolder(CBstr(L"\\"), &pTaskFolder), pTaskFolder);

        // resumeMargin<=0のときはタスク削除
        if (pSaveTask->resumeMargin <= 0) {
            pTaskFolder->DeleteTask(CBstr(pSaveTask->saveTaskName), 0);
            pTaskFolder->DeleteTask(CBstr(pSaveTask->saveTaskNameNoWake), 0);
            goto EXIT;
        }

        EXIT_ON_FAIL_TO_GET(hr = pService->NewTask(0, &pTaskDefinition), pTaskDefinition);
        EXIT_ON_FAIL_TO_GET(hr = pTaskDefinition->get_RegistrationInfo(&pRegistrationInfo), pRegistrationInfo);
        pRegistrationInfo->put_Description(CBstr(L"Launches TVTest at the reservation time."));

        EXIT_ON_FAIL_TO_GET(hr = pTaskDefinition->get_Settings(&pTaskSettings), pTaskSettings);
        pTaskSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        pTaskSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        pTaskSettings->put_WakeToRun(VARIANT_TRUE);
        pTaskSettings->put_ExecutionTimeLimit(CBstr(L"PT10M"));
        pTaskSettings->put_RestartInterval(CBstr(L"PT1M"));
        pTaskSettings->put_RestartCount(2);
        pTaskSettings->put_Priority(5);

        EXIT_ON_FAIL_TO_GET(hr = pTaskDefinition->get_Actions(&pActionCollection), pActionCollection);
        EXIT_ON_FAIL_TO_GET(hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction), pAction);
        EXIT_ON_FAIL_TO_GET(hr = pAction->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&pExecAction)), pExecAction);
        EXIT_ON_FAIL(hr = pExecAction->put_Path(CBstr(rundllPath)));
        EXIT_ON_FAIL(hr = pExecAction->put_Arguments(CBstr(pNewArgs)));

        // トリガ登録
        EXIT_ON_FAIL_TO_GET(hr = pTaskDefinition->get_Triggers(&pTriggerCollection), pTriggerCollection);
        for (int i = 0; i < pSaveTask->resumeTimeNum; i++) {
            ITrigger *pTrigger = NULL;
            ITimeTrigger *pTimeTrigger = NULL;
            if (!pSaveTask->resumeIsNoWake[i] && SUCCEEDED(pTriggerCollection->Create(TASK_TRIGGER_TIME, &pTrigger))) {
                if (SUCCEEDED(pTrigger->QueryInterface(IID_ITimeTrigger, reinterpret_cast<void**>(&pTimeTrigger)))) {
                    TCHAR szTime[64];
                    ::wsprintf(szTime, TEXT("%04d-%02d-%02dT%02d:%02d:00"),
                               pSaveTask->resumeTime[i].wYear,
                               pSaveTask->resumeTime[i].wMonth,
                               pSaveTask->resumeTime[i].wDay,
                               pSaveTask->resumeTime[i].wHour,
                               pSaveTask->resumeTime[i].wMinute);
                    pTimeTrigger->put_StartBoundary(CBstr(szTime));
                    pTimeTrigger->put_ExecutionTimeLimit(CBstr(L"PT15M"));
                    pTimeTrigger->Release();
                }
                pTrigger->Release();
            }
        }
        // 保存
        EXIT_ON_FAIL_TO_GET(hr = pTaskFolder->RegisterTaskDefinition(
            CBstr(pSaveTask->saveTaskName), pTaskDefinition, TASK_CREATE_OR_UPDATE, CVariant(), CVariant(),
            TASK_LOGON_INTERACTIVE_TOKEN, CVariant(L""), &pRegisteredTask), pRegisteredTask);

        // スリープ解除しないほうのトリガ登録(使いまわし)
        EXIT_ON_FAIL(hr = pTriggerCollection->Clear());
        for (int i = 0; i < pSaveTask->resumeTimeNum; i++) {
            ITrigger *pTrigger = NULL;
            ITimeTrigger *pTimeTrigger = NULL;
            if (pSaveTask->resumeIsNoWake[i] && SUCCEEDED(pTriggerCollection->Create(TASK_TRIGGER_TIME, &pTrigger))) {
                if (SUCCEEDED(pTrigger->QueryInterface(IID_ITimeTrigger, reinterpret_cast<void**>(&pTimeTrigger)))) {
                    TCHAR szTime[64];
                    ::wsprintf(szTime, TEXT("%04d-%02d-%02dT%02d:%02d:00"),
                               pSaveTask->resumeTime[i].wYear,
                               pSaveTask->resumeTime[i].wMonth,
                               pSaveTask->resumeTime[i].wDay,
                               pSaveTask->resumeTime[i].wHour,
                               pSaveTask->resumeTime[i].wMinute);
                    pTimeTrigger->put_StartBoundary(CBstr(szTime));
                    // Windows8.1でこのLimitを指定したトリガが1つもない状態において、手動でのスリープ復帰時にStartBoundaryを
                    // 過ぎたものが起動してしまう現象(おそらくはバグ)がみられたため(2014-03-28)
                    pTimeTrigger->put_ExecutionTimeLimit(CBstr(L"PT3M"));
                    pTimeTrigger->Release();
                }
                pTrigger->Release();
            }
        }
        pTaskSettings->put_WakeToRun(VARIANT_FALSE);
        // 保存
        EXIT_ON_FAIL_TO_GET(hr = pTaskFolder->RegisterTaskDefinition(
            CBstr(pSaveTask->saveTaskNameNoWake), pTaskDefinition, TASK_CREATE_OR_UPDATE, CVariant(), CVariant(),
            TASK_LOGON_INTERACTIVE_TOKEN, CVariant(L""), &pRegisteredTaskNoWake), pRegisteredTaskNoWake);
        goto EXIT;
    }

    // resumeMargin<=0のときはタスク削除
    if (pSaveTask->resumeMargin <= 0) {
        pScheduler->Delete(pSaveTask->saveTaskName);
        pScheduler->Delete(pSaveTask->saveTaskNameNoWake);
        goto EXIT;
    }

    hr = pScheduler->Activate(pSaveTask->saveTaskName, IID_ITask, reinterpret_cast<IUnknown**>(&pTask));
    if (FAILED(hr)) {
        EXIT_ON_FAIL_TO_GET(hr = pScheduler->NewWorkItem(pSaveTask->saveTaskName, CLSID_CTask, IID_ITask, reinterpret_cast<IUnknown**>(&pTask)), pTask);
    }

    TCHAR accountName[UNLEN + 1];
    DWORD accountLen = UNLEN + 1;
    if (!::GetUserName(accountName, &accountLen)) {
        hr = E_FAIL;
        goto EXIT;
    }
    EXIT_ON_FAIL(hr = pTask->SetApplicationName(rundllPath));
    EXIT_ON_FAIL(hr = pTask->SetParameters(pNewArgs));
    EXIT_ON_FAIL(hr = pTask->SetAccountInformation(accountName, NULL));
    EXIT_ON_FAIL(hr = pTask->SetFlags(TASK_FLAG_RUN_ONLY_IF_LOGGED_ON | TASK_FLAG_SYSTEM_REQUIRED));

    // 以前のトリガをクリア
    WORD count = 0;
    while (SUCCEEDED(pTask->GetTriggerCount(&count)) && count > 0) {
        EXIT_ON_FAIL(hr = pTask->DeleteTrigger(0));
    }
    // トリガ登録
    for (int i = 0; i < pSaveTask->resumeTimeNum; i++) {
        TASK_TRIGGER trigger = {0};
        trigger.cbTriggerSize = sizeof(trigger);
        trigger.wBeginYear = pSaveTask->resumeTime[i].wYear;
        trigger.wBeginMonth = pSaveTask->resumeTime[i].wMonth;
        trigger.wBeginDay = pSaveTask->resumeTime[i].wDay;
        trigger.wStartHour = pSaveTask->resumeTime[i].wHour;
        trigger.wStartMinute = pSaveTask->resumeTime[i].wMinute;
        trigger.TriggerType = TASK_TIME_TRIGGER_ONCE;

        WORD newTrigger;
        ITaskTrigger *pTaskTrigger = NULL;
        if (!pSaveTask->resumeIsNoWake[i] && SUCCEEDED(pTask->CreateTrigger(&newTrigger, &pTaskTrigger))) {
            pTaskTrigger->SetTrigger(&trigger);
            pTaskTrigger->Release();
        }
    }
    // 保存
    EXIT_ON_FAIL_TO_GET(hr = pTask->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pPersistFile)), pPersistFile);
    EXIT_ON_FAIL(hr = pPersistFile->Save(NULL, TRUE));

    // スリープ解除しないほうのタスク登録
    hr = pScheduler->Activate(pSaveTask->saveTaskNameNoWake, IID_ITask, reinterpret_cast<IUnknown**>(&pTaskNoWake));
    if (FAILED(hr)) {
        EXIT_ON_FAIL_TO_GET(hr = pScheduler->NewWorkItem(pSaveTask->saveTaskNameNoWake, CLSID_CTask, IID_ITask, reinterpret_cast<IUnknown**>(&pTaskNoWake)), pTaskNoWake);
    }
    EXIT_ON_FAIL(hr = pTaskNoWake->SetApplicationName(rundllPath));
    EXIT_ON_FAIL(hr = pTaskNoWake->SetParameters(pNewArgs));
    EXIT_ON_FAIL(hr = pTaskNoWake->SetAccountInformation(accountName, NULL));
    EXIT_ON_FAIL(hr = pTaskNoWake->SetFlags(TASK_FLAG_RUN_ONLY_IF_LOGGED_ON));

    // 以前のトリガをクリア
    count = 0;
    while (SUCCEEDED(pTaskNoWake->GetTriggerCount(&count)) && count > 0) {
        EXIT_ON_FAIL(hr = pTaskNoWake->DeleteTrigger(0));
    }
    // トリガ登録
    for (int i = 0; i < pSaveTask->resumeTimeNum; i++) {
        TASK_TRIGGER trigger = {0};
        trigger.cbTriggerSize = sizeof(trigger);
        trigger.wBeginYear = pSaveTask->resumeTime[i].wYear;
        trigger.wBeginMonth = pSaveTask->resumeTime[i].wMonth;
        trigger.wBeginDay = pSaveTask->resumeTime[i].wDay;
        trigger.wStartHour = pSaveTask->resumeTime[i].wHour;
        trigger.wStartMinute = pSaveTask->resumeTime[i].wMinute;
        trigger.TriggerType = TASK_TIME_TRIGGER_ONCE;

        WORD newTrigger;
        ITaskTrigger *pTaskTrigger = NULL;
        if (pSaveTask->resumeIsNoWake[i] && SUCCEEDED(pTaskNoWake->CreateTrigger(&newTrigger, &pTaskTrigger))) {
            pTaskTrigger->SetTrigger(&trigger);
            pTaskTrigger->Release();
        }
    }
    // 保存
    EXIT_ON_FAIL_TO_GET(hr = pTaskNoWake->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pPersistFileNoWake)), pPersistFileNoWake);
    EXIT_ON_FAIL(hr = pPersistFileNoWake->Save(NULL, TRUE));

EXIT:
    if (pSaveTask->hwndPost) {
        ::PostMessage(pSaveTask->hwndPost, pSaveTask->uMsgPost, SUCCEEDED(hr), hr);
    }
    if (pPersistFileNoWake) pPersistFileNoWake->Release();
    if (pTaskNoWake) pTaskNoWake->Release();
    if (pPersistFile) pPersistFile->Release();
    if (pTask) pTask->Release();
    if (pScheduler) pScheduler->Release();
    // TaskScheduler 2.0
    if (pRegisteredTaskNoWake) pRegisteredTaskNoWake->Release();
    if (pRegisteredTask) pRegisteredTask->Release();
    if (pTriggerCollection) pTriggerCollection->Release();
    if (pExecAction) pExecAction->Release();
    if (pAction) pAction->Release();
    if (pActionCollection) pActionCollection->Release();
    if (pTaskSettings) pTaskSettings->Release();
    if (pRegistrationInfo) pRegistrationInfo->Release();
    if (pTaskDefinition) pTaskDefinition->Release();
    if (pTaskFolder) pTaskFolder->Release();
    if (pService) pService->Release();
    delete [] pNewArgs;
    if (fInitialized) ::CoUninitialize();
    DEBUG_OUT(TEXT("CReserveList::SaveTaskThread(): "));
    DEBUG_OUT(SUCCEEDED(hr) ? TEXT("SUCCEEDED\n") : TEXT("FAILED\n"));
    return 0;
}


// 予約一覧メニューを作成
HMENU CReserveList::CreateListMenu(int idStart) const
{
    HMENU hmenu = ::CreateMenu();

    RESERVE *tail = m_head;
    for (int i = 0; tail && i < MENULIST_MAX; i++, tail = tail->next) {
        TCHAR szItem[128];
        SYSTEMTIME sysTime;
        FILETIME time = tail->GetTrimmedStartTime();
        ::FileTimeToSystemTime(&time, &sysTime);
        int len = ::wsprintf(szItem, TEXT("%02hu日(%s)%02hu時%02hu分%s "),
                             sysTime.wDay, GetDayOfWeekText(sysTime.wDayOfWeek),
                             sysTime.wHour, sysTime.wMinute,
                             tail->recOption.IsViewOnly() ? TEXT("▲") : TEXT(""));
        ::lstrcpyn(szItem + len, tail->eventName + (tail->eventName[0]==PREFIX_EPGORIGIN ? 1 : 0), 32);
        // プレフィクス対策
        TranslateText(szItem, TEXT("/&/_/"));
        ::AppendMenu(hmenu, MF_STRING | (tail->isEnabled ? MF_CHECKED : MF_UNCHECKED), idStart + i, szItem);
    }
    return hmenu;
}
