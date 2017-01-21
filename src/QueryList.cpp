#include "QueryList.h"
#include "resource.h"
#include <Windows.h>
#include <Shlwapi.h>

extern LPCTSTR g_nibble1List[14];
extern LPCTSTR *g_nibble2List[14];
extern int g_nibble2ListSize[14];

CQueryList::CQueryList()
    : m_queriesLen(0)
    , m_pApp(NULL)
{
    m_saveFileName[0] = 0;
}



CQueryList::~CQueryList()
{
    Clear();
}


void CQueryList::Clear()
{
    while (m_queriesLen > 0) {
        delete m_queries[--m_queriesLen];
    }
}


int CQueryList::Length() const
{
    return m_queriesLen;
}


void CQueryList::ToString(const QUERY &query, LPTSTR str)
{
    TCHAR szDaysOfWeek[8];
    TCHAR szStart[64];
    TCHAR szDuration[64];

    FlagArrayToStr(query.daysOfWeek, szDaysOfWeek, 7);
    TimeSpanToStr(query.start, szStart);
    TimeSpanToStr(query.duration, szDuration);
    
    ::wsprintf(str, TEXT("%d\t0x%04X\t0x%04X\t0x%04X\t0x%02X%02X\t%s\t%s\t%s\t%d\t%s\t%s\t"),
        (int)query.isEnabled,
        (int)query.networkID, (int)query.transportStreamID, (int)query.serviceID,
        (int)query.nibble1, (int)query.nibble2,
        szDaysOfWeek, szStart, szDuration, query.reserveCount, query.keyword,
        query.eventName[0] ? query.eventName : TEXT("*"));

    RecordingOption::ToString(query.recOption, str + ::lstrlen(str));
}


int CQueryList::Insert(int index, const QUERY &query)
{
    // キーワードは必須
    if (!query.keyword[0] || index >= m_queriesLen) return -1;

    if (index < 0) {
        if (m_queriesLen < QUERIES_MAX) {
            index = m_queriesLen++;
            m_queries[index] = new QUERY;
        }
        else {
            return -1;
        }
    }
    *m_queries[index] = query;
    
    // 入力チェック
    ReplaceTokenDelimiters(m_queries[index]->keyword);
    ReplaceTokenDelimiters(m_queries[index]->eventName);
    ReplaceTokenDelimiters(m_queries[index]->recOption.saveDir);
    ReplaceTokenDelimiters(m_queries[index]->recOption.saveName);
    return index;
}


int CQueryList::Insert(int index, LPCTSTR str)
{
    QUERY query;
    int i = 0;
    
    query.isEnabled = ::StrToInt(str) != 0;
    if (!NextToken(&str)) return -1;

    ::StrToIntEx(str, STIF_SUPPORT_HEX, &i);
    query.networkID = static_cast<WORD>(i);
    if (!NextToken(&str)) return -1;
    
    ::StrToIntEx(str, STIF_SUPPORT_HEX, &i);
    query.transportStreamID = static_cast<WORD>(i);
    if (!NextToken(&str)) return -1;
    
    ::StrToIntEx(str, STIF_SUPPORT_HEX, &i);
    query.serviceID = static_cast<WORD>(i);
    if (!NextToken(&str)) return -1;
    
    ::StrToIntEx(str, STIF_SUPPORT_HEX, &i);
    query.nibble1 = i >> 8 & 0xff;
    query.nibble2 = i & 0xff;
    if (!NextToken(&str)) return -1;
    
    if (!FlagStrToArray(str, query.daysOfWeek, 7)) return -1;
    if (!NextToken(&str)) return -1;
    
    if (!StrToTimeSpan(str, &query.start)) return -1;
    if (query.start >= 24 * 60 * 60) query.start = 0;
    if (!NextToken(&str)) return -1;
    
    if (!StrToTimeSpan(str, &query.duration)) return -1;
    if (query.duration > 24 * 60 * 60) query.duration = 24 * 60 * 60;
    if (!NextToken(&str)) return -1;
    
    query.reserveCount = ::StrToInt(str);
    if (query.reserveCount < 0) query.reserveCount = 0;
    if (!NextToken(&str)) return -1;

    GetToken(str, query.keyword, ARRAY_SIZE(query.keyword));
    if (!NextToken(&str)) return -1;
    
    GetToken(str, query.eventName, ARRAY_SIZE(query.eventName));
    if (!::lstrcmp(query.eventName, TEXT("*"))) query.eventName[0] = 0;
    if (!NextToken(&str)) return -1;
    
    if (!RecordingOption::FromString(str, &query.recOption)) return -1;

    return Insert(index, query);
}


// indexが負ならばリストへの追加、非負ならばその対象クエリを変更する
// 対象クエリに変更(クエリの追加・削除を含む)があればそのインデックスを、なければ負を返す
int CQueryList::Insert(int index, HINSTANCE hInstance, HWND hWndParent, const QUERY &in, const RECORDING_OPTION &defaultRecOption, LPCTSTR serviceName)
{
    QUERY query = in;
    void *lParams[3] = { &query, const_cast<RECORDING_OPTION*>(&defaultRecOption), const_cast<LPTSTR>(serviceName) };

    int rv = ::DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_QUERY), hWndParent,
                              DlgProc, reinterpret_cast<LPARAM>(lParams));
    
    if (rv == IDC_DISABLE) {
        if (index < 0 || m_queriesLen <= index) return -1;
        m_queries[index]->isEnabled = !query.isEnabled;
        return index;
    }
    return rv == IDOK ? Insert(index, query) : rv == IDC_DELETE ? Delete(index) : -1;
}


INT_PTR CALLBACK CQueryList::DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_INITDIALOG:
        {
            
            void **lParams = reinterpret_cast<void**>(lParam);
            QUERY *pQuery = reinterpret_cast<QUERY*>(lParams[0]);
            const RECORDING_OPTION *pOption = reinterpret_cast<RECORDING_OPTION*>(lParams[1]);
            LPCTSTR serviceName = reinterpret_cast<LPCTSTR>(lParams[2]);
            
            ::SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pQuery));
            if (serviceName && serviceName[0]) ::SetDlgItemText(hDlg, IDC_STATIC_SERVICE_NAME, serviceName);

            ::SetDlgItemText(hDlg, IDC_EDIT_KEYWORD, pQuery->keyword);
			::SendDlgItemMessage(hDlg, IDC_EDIT_KEYWORD, EM_LIMITTEXT, ARRAY_SIZE(pQuery->keyword) - 1, 0);

            for (int i = 0; i < 7; i++)
                ::CheckDlgButton(hDlg, IDC_CHECK_SUN + i, pQuery->daysOfWeek[i] ? BST_CHECKED : BST_UNCHECKED);
            
            TCHAR hourList[25][3], *pHourList[25];
            for (int i = 0; i < 25; i++) {
                ::wsprintf(hourList[i], TEXT("%d"), i);
                pHourList[i] = hourList[i];
            }
            TCHAR minuteList[6][3], *pMinuteList[6];
            for (int i = 0; i < 6; i++) {
                ::wsprintf(minuteList[i], TEXT("%d"), i * 10);
                pMinuteList[i] = minuteList[i];
            }

            SetComboBoxList(hDlg, IDC_COMBO_STA_HOUR, pHourList, ARRAY_SIZE(pHourList) - 1);
            ::SendDlgItemMessage(hDlg, IDC_COMBO_STA_HOUR, CB_SETCURSEL, pQuery->start / 3600 % 24, 0);
            
            SetComboBoxList(hDlg, IDC_COMBO_STA_MIN, pMinuteList, ARRAY_SIZE(pMinuteList));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_STA_MIN, CB_SETCURSEL, pQuery->start / 600 % 6, 0);
            
            SetComboBoxList(hDlg, IDC_COMBO_DUR_HOUR, pHourList, ARRAY_SIZE(pHourList));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_DUR_HOUR, CB_SETCURSEL, pQuery->duration / 3600 % 25, 0);
            
            SetComboBoxList(hDlg, IDC_COMBO_DUR_MIN, pMinuteList, ARRAY_SIZE(pMinuteList));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_DUR_MIN, CB_SETCURSEL, pQuery->duration / 600 % 6, 0);
            if (pQuery->duration / 3600 % 25 == 24) ::EnableWindow(GetDlgItem(hDlg, IDC_COMBO_DUR_MIN), false);

            SetComboBoxList(hDlg, IDC_COMBO_NIBBLE1, g_nibble1List, ARRAY_SIZE(g_nibble1List));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_NIBBLE1, CB_SETCURSEL,
                                 pQuery->nibble1 == 0xFF ? 0 :
                                 pQuery->nibble1 == 0x0F ? ARRAY_SIZE(g_nibble1List) - 1 :
                                 pQuery->nibble1 + 1, 0);
            
            int nibble1 = ::SendDlgItemMessage(hDlg, IDC_COMBO_NIBBLE1, CB_GETCURSEL, 0, 0);
            SetComboBoxList(hDlg, IDC_COMBO_NIBBLE2, g_nibble2List[nibble1], g_nibble2ListSize[nibble1]);
            ::SendDlgItemMessage(hDlg, IDC_COMBO_NIBBLE2, CB_SETCURSEL,
                                 pQuery->nibble2 == 0xFF ? 0 :
                                 pQuery->nibble2 == 0x0F ? g_nibble2ListSize[nibble1] - 1 :
                                 pQuery->nibble2 + 1, 0);
            
            if (pQuery->eventName[0]) {
                ::CheckDlgButton(hDlg, IDC_CHECK_EVENT_NAME, BST_CHECKED);
                ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_EVENT_NAME), TRUE);
                ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_EVENT_NUM), TRUE);
            }
            ::SetDlgItemText(hDlg, IDC_EDIT_EVENT_NAME, pQuery->eventName);
            ::SendDlgItemMessage(hDlg, IDC_EDIT_EVENT_NAME, EM_LIMITTEXT, ARRAY_SIZE(pQuery->eventName) - 1, 0);
            ::SetDlgItemInt(hDlg, IDC_EDIT_EVENT_NUM, pQuery->reserveCount, FALSE);
            
            if (!pQuery->isEnabled) ::SetDlgItemText(hDlg, IDC_DISABLE, TEXT("有効にする"));
            
            return RecordingOption::DlgProc(hDlg, uMsg, wParam, &pQuery->recOption, true, pOption);
        }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_COMBO_NIBBLE1:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                while(::SendDlgItemMessage(hDlg, IDC_COMBO_NIBBLE2, CB_GETCOUNT, 0, 0) != 0)
                    ::SendDlgItemMessage(hDlg, IDC_COMBO_NIBBLE2, CB_DELETESTRING, 0, 0);

                int nibble1 = ::SendDlgItemMessage(hDlg, IDC_COMBO_NIBBLE1, CB_GETCURSEL, 0, 0);
                SetComboBoxList(hDlg, IDC_COMBO_NIBBLE2, g_nibble2List[nibble1], g_nibble2ListSize[nibble1]);
                ::SendDlgItemMessage(hDlg, IDC_COMBO_NIBBLE2, CB_SETCURSEL, 0, 0);
                return TRUE;
            }
            break;
        case IDC_COMBO_DUR_HOUR:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                bool isMax = ::SendDlgItemMessage(hDlg, IDC_COMBO_DUR_HOUR, CB_GETCURSEL, 0, 0) == 24;
                if (isMax) ::SendDlgItemMessage(hDlg, IDC_COMBO_DUR_MIN, CB_SETCURSEL, 0, 0);
                ::EnableWindow(GetDlgItem(hDlg, IDC_COMBO_DUR_MIN), !isMax);
                return TRUE;
            }
            break;
        case IDC_CHECK_EVENT_NAME:
            {
                BOOL isChecked = ::IsDlgButtonChecked(hDlg, IDC_CHECK_EVENT_NAME) == BST_CHECKED;
                ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_EVENT_NAME), isChecked);
                ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_EVENT_NUM), isChecked);
            }
            return TRUE;
        case IDOK:
            {
                QUERY *pQuery = reinterpret_cast<QUERY*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
            
                ::GetDlgItemText(hDlg, IDC_EDIT_KEYWORD, pQuery->keyword, ARRAY_SIZE(pQuery->keyword) - 1);
                
                for (int i = 0; i < 7; i++)
                    pQuery->daysOfWeek[i] = ::IsDlgButtonChecked(hDlg, IDC_CHECK_SUN + i) == BST_CHECKED;
            
                pQuery->start = ::SendDlgItemMessage(hDlg, IDC_COMBO_STA_HOUR, CB_GETCURSEL, 0, 0) * 3600 +
                                ::SendDlgItemMessage(hDlg, IDC_COMBO_STA_MIN, CB_GETCURSEL, 0, 0) * 600;
                
                pQuery->duration = ::SendDlgItemMessage(hDlg, IDC_COMBO_DUR_HOUR, CB_GETCURSEL, 0, 0) * 3600 +
                                   ::SendDlgItemMessage(hDlg, IDC_COMBO_DUR_MIN, CB_GETCURSEL, 0, 0) * 600;

                int nibble1 = ::SendDlgItemMessage(hDlg, IDC_COMBO_NIBBLE1, CB_GETCURSEL, 0, 0);
                int nibble2 = ::SendDlgItemMessage(hDlg, IDC_COMBO_NIBBLE2, CB_GETCURSEL, 0, 0);
                pQuery->nibble1 = nibble1 == 0 ? 0xFF : nibble1 == ARRAY_SIZE(g_nibble1List) - 1 ? 0x0F : nibble1 - 1;

                pQuery->nibble2 = nibble1 == 0 || nibble1 == ARRAY_SIZE(g_nibble1List) - 1 || nibble2 == 0 ? 0xFF :
                                  nibble2 == g_nibble2ListSize[nibble1] - 1 ? 0x0F : nibble2 - 1;
                
                pQuery->eventName[0] = 0;
                pQuery->reserveCount = 0;
                if (::IsDlgButtonChecked(hDlg, IDC_CHECK_EVENT_NAME) == BST_CHECKED) {
                    ::GetDlgItemText(hDlg, IDC_EDIT_EVENT_NAME, pQuery->eventName, ARRAY_SIZE(pQuery->eventName) - 1);
                    pQuery->reserveCount = ::GetDlgItemInt(hDlg, IDC_EDIT_EVENT_NUM, NULL, FALSE);
                }

                RecordingOption::DlgProc(hDlg, uMsg, wParam, &pQuery->recOption, true);
            }
            // fall through!
        case IDC_DISABLE:
        case IDC_DELETE:
        case IDCANCEL:
            ::EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        default:
            {
                QUERY *pQuery = reinterpret_cast<QUERY*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
                return RecordingOption::DlgProc(hDlg, uMsg, wParam, &pQuery->recOption, true);
            }
        }
        break;
    }
    return FALSE;
}


int CQueryList::Delete(int index)
{
    if (index < 0 || m_queriesLen <= index) return -1;

    delete m_queries[index];

    // 前方に詰める
    for (int i = index; i + 1 < m_queriesLen; i++) {
        m_queries[i] = m_queries[i + 1];
    }
    m_queriesLen--;

    return index;
}


const QUERY *CQueryList::Get(int index) const
{
    if (index < 0 || m_queriesLen <= index) return NULL;

    return m_queries[index];
}


bool CQueryList::CreateReserve(int index, RESERVE &res, WORD eventID, LPCTSTR eventName, FILETIME startTime, int duration)
{
    if (index < 0 || m_queriesLen <= index) return false;

    res.networkID         = m_queries[index]->networkID;
    res.transportStreamID = m_queries[index]->transportStreamID;
    res.serviceID         = m_queries[index]->serviceID;
    res.eventID           = eventID;
    res.startTime         = startTime;
    res.duration          = duration;
    res.recOption         = m_queries[index]->recOption;
    res.next              = NULL;
    
    // クエリにイベント名が指定されていればそっちを使う
    if (m_queries[index]->eventName[0]) {
        FormatEventName(res.eventName, ARRAY_SIZE(res.eventName),
                        m_queries[index]->reserveCount, m_queries[index]->eventName);
    }
    else {
        ::lstrcpyn(res.eventName, eventName, ARRAY_SIZE(res.eventName));
    }

    if (m_queries[index]->reserveCount > 0) m_queries[index]->reserveCount++;
    return true;
}


bool CQueryList::Load()
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

    for (LPCTSTR line = text; line; ) {
        if (Insert(-1, ++line) < 0) {
            if (m_pApp) m_pApp->AddLog(L"エラー: クエリ1行読み込み失敗");
        }
        line = ::StrChr(line, TEXT('\n'));
    }

    delete [] text;
    return true;
}


bool CQueryList::Save() const
{
    HANDLE hFile = ::CreateFile(m_saveFileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (m_pApp) m_pApp->AddLog(L"エラー: クエリファイルに書き込めません");
        return false;
    }
    
    DWORD writtenBytes;
    WCHAR bom = L'\xFEFF';
    ::WriteFile(hFile, &bom, sizeof(bom), &writtenBytes, NULL);

    for (int i = 0; i < m_queriesLen; i++) {
        TCHAR buf[512];
        ToString(*m_queries[i], buf);
        ::lstrcat(buf, TEXT("\r\n"));
        ::WriteFile(hFile, buf, ::lstrlen(buf) * sizeof(TCHAR), &writtenBytes, NULL);
    }

    ::CloseHandle(hFile);
    return true;
}


void CQueryList::SetPluginFileName(LPCTSTR fileName)
{
    TCHAR saveFileName[MAX_PATH + 32];
    ::lstrcpy(saveFileName, fileName);
    ::PathRemoveExtension(saveFileName);
    ::lstrcat(saveFileName, TEXT("_Queries.txt"));
    ::lstrcpyn(m_saveFileName, saveFileName, ARRAY_SIZE(m_saveFileName));
}


// クエリ一覧メニューを作成
HMENU CQueryList::CreateListMenu(int idStart) const
{
    HMENU hmenu = ::CreateMenu();
    for (int i = 0; i < m_queriesLen && i < MENULIST_MAX; i++) {
        TCHAR szItem[128];
        ::wsprintf(szItem, TEXT("%02d "), i);
        ::lstrcpyn(szItem + ::lstrlen(szItem), m_queries[i]->keyword, 32);
        ::AppendMenu(hmenu, MF_STRING | (m_queries[i]->isEnabled ? MF_CHECKED : MF_UNCHECKED), idStart + i, szItem);
    }
    return hmenu;
}


#ifdef _DEBUG
void CQueryList::SetTVTestApp(TVTest::CTVTestApp *pApp)
{
    m_pApp = pApp;
}
#endif
