#include <Windows.h>
#include <Shlwapi.h>
#include <MSTask.h>
#include "TVTestPlugin.h"
#include "resource.h"
#include "Util.h"
#include "RecordingOption.h"
#include "ReserveList.h"


CReserveList::CReserveList()
    : m_head(NULL)
    , m_hThread(NULL)
    , m_pApp(NULL)
{
    m_saveFileName[0] = 0;
    m_saveTaskName[0] = 0;
    m_pluginShortPath[0] = 0;
    m_pluginName[0] = 0;
}


CReserveList::~CReserveList()
{
    Clear();
    if (m_hThread) {
        ::WaitForSingleObject(m_hThread, INFINITE);
        ::CloseHandle(m_hThread);
    }
}


void CReserveList::Clear()
{
    CBlockLock lock(&m_writeLock);

    while (m_head) {
        RESERVE *pRes = m_head;
        m_head = pRes->next;
        delete pRes;
    }
}


void CReserveList::ToString(const RESERVE &res, LPTSTR str)
{
    TCHAR szStartTime[64];
    TCHAR szDuration[64];

    FileTimeToStr(&res.startTime, szStartTime);
    TimeSpanToStr(res.duration, szDuration);

    ::wsprintf(str, TEXT("0x%04X\t0x%04X\t0x%04X\t0x%04X\t%s\t%s\t%s\t"),
        (int)res.networkID, (int)res.transportStreamID,
        (int)res.serviceID, (int)res.eventID,
        szStartTime, szDuration, res.eventName);

    RecordingOption::ToString(res.recOption, str + ::lstrlen(str));
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

    CBlockLock lock(&m_writeLock);

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
    else if (m_head->startTime - pRes->startTime > 0) {
        // 予約をリストの先頭に挿入
        pRes->next = m_head;
        m_head = pRes;
    }
    else {
        // 予約をリストに挿入
        RESERVE *tail;
        for (tail = m_head; tail->next; tail = tail->next) {
            if (tail->next->startTime - pRes->startTime > 0) break;
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

    if (!StrToTimeSpan(str, &res.duration)) return false;
    if (!NextToken(&str)) return false;

    GetToken(str, res.eventName, ARRAY_SIZE(res.eventName));
    if (!NextToken(&str)) return false;

    if (!RecordingOption::FromString(str, &res.recOption)) return false;

    return Insert(res);
}


bool CReserveList::Insert(HINSTANCE hInstance, HWND hWndParent, const RESERVE &in,
                          const RECORDING_OPTION &defaultRecOption, LPCTSTR serviceName)
{
    DIALOG_PARAMS prms = { in, &defaultRecOption, serviceName, m_pluginName };
    INT_PTR rv = ::DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_RESERVATION), hWndParent,
                                  DlgProc, reinterpret_cast<LPARAM>(&prms));

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

            // タイトルバー文字列をいじる
            TCHAR cap[128];
            if (::lstrcmpi(pPrms->pluginName, DEFAULT_PLUGIN_NAME) && ::GetWindowText(hDlg, cap, 32)) {
                ::lstrcat(cap, TEXT(" ("));
                ::lstrcat(cap, pPrms->pluginName);
                ::lstrcat(cap, TEXT(")"));
                ::SetWindowText(hDlg, cap);
            }

            if (pPrms->serviceName && pPrms->serviceName[0]) {
                ::SetDlgItemText(hDlg, IDC_STATIC_SERVICE_NAME, pPrms->serviceName);
            }

            SYSTEMTIME sysTime;
            ::FileTimeToSystemTime(&pRes->startTime, &sysTime);
            TCHAR text[512];
            ::wsprintf(text, TEXT("%hu年%hu月%hu日(%s) %hu時%hu分%hu秒 から %d分間"),
                       sysTime.wYear, sysTime.wMonth, sysTime.wDay, GetDayOfWeekText(sysTime.wDayOfWeek),
                       sysTime.wHour, sysTime.wMinute, sysTime.wSecond, pRes->duration / 60);
            ::SetDlgItemText(hDlg, IDC_STATIC_RES_TIME, text);

            ::SetDlgItemText(hDlg, IDC_EDIT_EVENT_NAME, pRes->eventName);
            ::SendDlgItemMessage(hDlg, IDC_EDIT_EVENT_NAME, EM_LIMITTEXT, ARRAY_SIZE(pRes->eventName) - 1, 0);

            return RecordingOption::DlgProc(hDlg, uMsg, wParam, &pRes->recOption, true, pPrms->pDefaultRecOption);
        }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            {
                RESERVE *pRes = reinterpret_cast<RESERVE*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
                if (!::GetDlgItemText(hDlg, IDC_EDIT_EVENT_NAME, pRes->eventName, ARRAY_SIZE(pRes->eventName)))
                    pRes->eventName[0] = 0;
                RecordingOption::DlgProc(hDlg, uMsg, wParam, &pRes->recOption, true);
            }
            // fall through!
        case IDC_DELETE:
        case IDCANCEL:
            ::EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        default:
            {
                RESERVE *pRes = reinterpret_cast<RESERVE*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
                return RecordingOption::DlgProc(hDlg, uMsg, wParam, &pRes->recOption, true);
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

    CBlockLock lock(&m_writeLock);

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
    DWORD textSize = ReadTextFileToEnd(m_saveFileName, NULL, 0);
    if (textSize == 0) return false;

    LPTSTR text = new TCHAR[textSize];
    textSize = ReadTextFileToEnd(m_saveFileName, text, textSize);
    // UTF-16LEのBOM付きでなければならない
    if (textSize < 2 || text[0] != L'\xFEFF') {
        delete [] text;
        return false;
    }

    Clear();

    // Insertの効率のため(大して変わらんけど)逆順に読む
    LPCTSTR line = text + textSize - 1;
    do {
        line = ::StrRChr(text, line, TEXT('\n'));
        if (!line) line = text;
        if (!Insert(line + 1/*改行またはBOMの分*/)) {
            if (m_pApp) m_pApp->AddLog(L"エラー: 予約1行読み込み失敗");
        }
    } while (line > text);

    delete [] text;
    return true;
}


bool CReserveList::Save() const
{
    HANDLE hFile = ::CreateFile(m_saveFileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (m_pApp) m_pApp->AddLog(L"エラー: 予約ファイルに書き込めません");
        return false;
    }

    DWORD writtenBytes;
    WCHAR bom = L'\xFEFF';
    ::WriteFile(hFile, &bom, sizeof(bom), &writtenBytes, NULL);

    for (RESERVE *tail = m_head; tail; tail = tail->next) {
        TCHAR buf[512];
        ToString(*tail, buf);
        ::lstrcat(buf, TEXT("\r\n"));
        ::WriteFile(hFile, buf, ::lstrlen(buf) * sizeof(TCHAR), &writtenBytes, NULL);
    }

    ::CloseHandle(hFile);
    return true;
}


#define GET_PRIORITY(x) ((x) % PRIORITY_MOD == PRIORITY_DEFAULT ? (x) + defaultRecOption.priority : (x))
#define GET_START_MARGIN(x) ((x) < 0 ? defaultRecOption.startMargin : (x))


// 直近の予約を取得
RESERVE *CReserveList::GetNearest(const RECORDING_OPTION &defaultRecOption, RESERVE **pPrev) const
{
    *pPrev = NULL;

    RESERVE *pMinRes = NULL;
    FILETIME minStart;
    minStart.dwLowDateTime = 0xFFFFFFFF;
    minStart.dwHighDateTime = 0x7FFFFFFF;

    // 開始マージンを含めてもっとも直近の予約を探す
    for (RESERVE *tail = m_head; tail; tail = tail->next) {
        FILETIME start = tail->startTime;
        start += -GET_START_MARGIN(tail->recOption.startMargin) * FILETIME_SECOND;

        // 同時刻の予約は優先度の高いものを選択
        if (minStart - start > 0 || minStart - start == 0 &&
            GET_PRIORITY(pMinRes->recOption.priority) < GET_PRIORITY(tail->recOption.priority))
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


const RESERVE *CReserveList::GetNearest(const RECORDING_OPTION &defaultRecOption) const
{
    RESERVE *prev;
    return GetNearest(defaultRecOption, &prev);
}


// 直近の予約を取得(予約時間が調整される場合がある)
bool CReserveList::GetNearest(RESERVE *pRes, const RECORDING_OPTION &defaultRecOption, int readyOffset) const
{
    const RESERVE *pNearest = GetNearest(defaultRecOption);
    if (!pNearest || !pRes) return false;
    RESERVE &res = *pRes;
    res = *pNearest;
    RecordingOption::ApplyDefault(&res.recOption, defaultRecOption);

    // 終了マージンは録画時間を超えて負であってはならない
    if (res.recOption.endMargin < -res.duration) res.recOption.endMargin = -res.duration;

    FILETIME resEnd = res.startTime;
    resEnd += (res.duration + res.recOption.endMargin) * FILETIME_SECOND;

    // 優先度の高い別の予約があれば録画終了時刻を早める
    FILETIME resRealEnd = resEnd;
    for (const RESERVE *tail = m_head; tail; tail = tail->next) {
        FILETIME start = tail->startTime;
        // 録画はすぐに切り替わらないので、readyOffset秒の余裕をもたせる
        start += -(GET_START_MARGIN(tail->recOption.startMargin) + readyOffset) * FILETIME_SECOND;

        if (res.recOption.priority < GET_PRIORITY(tail->recOption.priority) &&
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
bool CReserveList::DeleteNearest(const RECORDING_OPTION &defaultRecOption)
{
    RESERVE *prev;
    RESERVE *pRes = GetNearest(defaultRecOption, &prev);
    if (!pRes) return false;

    CBlockLock lock(&m_writeLock);

    if (!prev) m_head = pRes->next;
    else prev->next = pRes->next;
    delete pRes;
    return true;
}


void CReserveList::SetPluginFileName(LPCTSTR fileName)
{
    CBlockLock lock(&m_writeLock);

    TCHAR saveFileName[MAX_PATH + 32];
    ::lstrcpy(saveFileName, fileName);
    ::PathRemoveExtension(saveFileName);
    ::lstrcat(saveFileName, TEXT("_Reserves"));

    ::lstrcpyn(m_saveTaskName, ::PathFindFileName(saveFileName), ARRAY_SIZE(m_saveTaskName));
    ::lstrcat(saveFileName, TEXT(".txt"));
    ::lstrcpyn(m_saveFileName, saveFileName, ARRAY_SIZE(m_saveFileName));

    ::GetShortPathName(fileName, m_pluginShortPath, ARRAY_SIZE(m_pluginShortPath));
    ::lstrcpyn(m_pluginName, ::PathFindFileName(fileName), ARRAY_SIZE(m_pluginName));
}


bool CReserveList::RunSaveTask(int resumeMargin, int execWait, LPCTSTR tvTestAppName, LPCTSTR driverName, LPCTSTR tvTestCmdOption)
{
    if (!m_pluginShortPath[0] || !m_saveTaskName[0] || !tvTestAppName[0] || !driverName[0]) return false;

    TCHAR rundllPath[MAX_PATH];
    if (!GetRundll32Path(rundllPath)) return false;

    TCHAR accountName[UNLEN + 1];
    DWORD accountLen = UNLEN + 1;
    if (!::GetUserName(accountName, &accountLen)) return false;

    m_writeLock.Lock();

    // タスクスケジューラ登録に必要な情報をセット
    m_saveTask.resumeMargin = resumeMargin;
    m_saveTask.execWait = execWait;
    ::lstrcpyn(m_saveTask.tvTestAppName, tvTestAppName, ARRAY_SIZE(m_saveTask.tvTestAppName));
    ::lstrcpyn(m_saveTask.driverName, driverName, ARRAY_SIZE(m_saveTask.driverName));
    ::lstrcpyn(m_saveTask.tvTestCmdOption, tvTestCmdOption, ARRAY_SIZE(m_saveTask.tvTestCmdOption));
    ::lstrcpyn(m_saveTask.rundllPath, rundllPath, ARRAY_SIZE(m_saveTask.rundllPath));
    ::lstrcpyn(m_saveTask.accountName, accountName, ARRAY_SIZE(m_saveTask.accountName));

    if (m_hThread) {
        ::WaitForSingleObject(m_hThread, INFINITE);
        ::CloseHandle(m_hThread);
    }
    m_hThread = ::CreateThread(NULL, 0, SaveTaskThread, this, 0, NULL);
    if (!m_hThread) {
        m_writeLock.Unlock();
        return false;
    }

    // Lockの開放はスレッドに任せる
    return true;
}


DWORD WINAPI CReserveList::SaveTaskThread(LPVOID pParam)
{
    // メンバへの書き込みは禁止!
    CReserveList *pThis = reinterpret_cast<CReserveList*>(pParam);
    CONTEXT_SAVE_TASK *pSaveTask = &pThis->m_saveTask;

    HRESULT hr;
    ITaskScheduler *pScheduler = NULL;
    ITask *pTask = NULL;
    IPersistFile *pPersistFile = NULL;

    if (FAILED(::CoInitialize(NULL))) {
        pThis->m_writeLock.Unlock();
        return FALSE;
    }

    hr = ::CoCreateInstance(CLSID_CTaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                            IID_ITaskScheduler, reinterpret_cast<LPVOID*>(&pScheduler));
    if (hr != S_OK) goto ERROR_EXIT;

    // resumeMargin<=0のときはタスク削除
    if (pSaveTask->resumeMargin <= 0) {
        pScheduler->Delete(pThis->m_saveTaskName);
        goto EXIT;
    }

    hr = pScheduler->Activate(pThis->m_saveTaskName, IID_ITask, reinterpret_cast<IUnknown**>(&pTask));
    if (hr != S_OK) {
        hr = pScheduler->NewWorkItem(pThis->m_saveTaskName, CLSID_CTask, IID_ITask, reinterpret_cast<IUnknown**>(&pTask));
        if (hr != S_OK) goto ERROR_EXIT;
    }

    hr = pTask->SetApplicationName(pSaveTask->rundllPath);
    TCHAR parameters[MAX_PATH * 3 + CMD_OPTION_MAX + 32];
    ::wsprintf(parameters, TEXT("%s,DelayedExecute %d \"%s\" /D %s %s"), pThis->m_pluginShortPath,
               pSaveTask->execWait, pSaveTask->tvTestAppName, pSaveTask->driverName, pSaveTask->tvTestCmdOption);
    hr = pTask->SetParameters(parameters);
    hr = pTask->SetAccountInformation(pSaveTask->accountName, NULL);
    hr = pTask->SetFlags(TASK_FLAG_RUN_ONLY_IF_LOGGED_ON | TASK_FLAG_SYSTEM_REQUIRED);

    WORD count = 0;
    for (;;) {
        hr = pTask->GetTriggerCount(&count);
        if (count <= 0) break;
        pTask->DeleteTrigger(0);
    }

    RESERVE *tail = pThis->m_head;
    for (int i = 0; tail && i < TASK_TRIGGER_MAX; tail = tail->next, i++) {
        FILETIME resumeTime = tail->startTime;
        resumeTime += -pSaveTask->resumeMargin * FILETIME_MINUTE;
        SYSTEMTIME resumeSysTime;
        if (!::FileTimeToSystemTime(&resumeTime, &resumeSysTime)) continue;

        TASK_TRIGGER trigger = {0};
        trigger.cbTriggerSize = sizeof(trigger);
        trigger.wBeginYear = resumeSysTime.wYear;
        trigger.wBeginMonth = resumeSysTime.wMonth;
        trigger.wBeginDay = resumeSysTime.wDay;
        trigger.wStartHour = resumeSysTime.wHour;
        trigger.wStartMinute = resumeSysTime.wMinute;
        trigger.TriggerType = TASK_TIME_TRIGGER_ONCE;

        WORD newTrigger;
        ITaskTrigger *pTaskTrigger = NULL;
        hr = pTask->CreateTrigger(&newTrigger, &pTaskTrigger);
        hr = pTaskTrigger->SetTrigger(&trigger);
    }

    hr = pTask->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pPersistFile));
    if (hr != S_OK) goto ERROR_EXIT;
    hr = pPersistFile->Save(NULL, TRUE);
    if (hr != S_OK) goto ERROR_EXIT;

EXIT:
    ::CoUninitialize();
    pThis->m_writeLock.Unlock();
    return TRUE;

ERROR_EXIT:
    ::CoUninitialize();
    pThis->m_writeLock.Unlock();
    return FALSE;
}


// 予約一覧メニューを作成
HMENU CReserveList::CreateListMenu(int idStart) const
{
    HMENU hmenu = ::CreateMenu();

    RESERVE *tail = m_head;
    for (int i = 0; tail && i < MENULIST_MAX; i++, tail = tail->next) {
        TCHAR szItem[128];
        SYSTEMTIME sysTime;
        ::FileTimeToSystemTime(&tail->startTime, &sysTime);
        ::wsprintf(szItem, TEXT("%02hu日(%s)%02hu時%02hu分%s "),
                   sysTime.wDay, GetDayOfWeekText(sysTime.wDayOfWeek),
                   sysTime.wHour, sysTime.wMinute,
                   RecordingOption::ViewsOnly(tail->recOption) ? TEXT("▲") : TEXT(""));

        ::lstrcpyn(szItem + ::lstrlen(szItem), tail->eventName, 32);
        ::AppendMenu(hmenu, MF_STRING, idStart + i, szItem);
    }
    return hmenu;
}


#ifdef _DEBUG
void CReserveList::SetTVTestApp(TVTest::CTVTestApp *pApp)
{
    m_pApp = pApp;
}
#endif
