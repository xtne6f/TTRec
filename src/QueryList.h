#ifndef INCLUDE_QUERY_LIST_H
#define INCLUDE_QUERY_LIST_H

struct QUERY {
    bool isEnabled;
    WORD networkID;                     // TR-B14
    WORD transportStreamID;
    WORD serviceID;
    BYTE nibble1;
    BYTE nibble2;
    bool daysOfWeek[7];
    int start;                          // 探索開始時刻[秒](00:00:00～23:59:59)
    int duration;                       // 探索時間[秒](00:00:00～24:00:00)
    TCHAR keyword[EVENT_NAME_MAX];
    TCHAR eventName[EVENT_NAME_MAX];    // ""なら予約生成時に自動付加
    int reserveCount;                   // >0なら予約が生成されるたびに増分
    RECORDING_OPTION recOption;
};

class CQueryList
{
    static const int QUERIES_MAX = MENULIST_MAX;

    struct DIALOG_PARAMS {
        QUERY query;
        const RECORDING_OPTION *pDefaultRecOption;
        LPCTSTR serviceName;
        LPCTSTR captionSuffix;
    };

    QUERY *m_queries[QUERIES_MAX];
    int m_queriesLen;
    TCHAR m_saveFileName[MAX_PATH];

    void Clear();
    static void ToString(const QUERY &query, LPTSTR str);
    int Insert(int index, const QUERY &query);
    int Insert(int index, LPCTSTR str);
    int Delete(int index);
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
public:
    CQueryList();
    ~CQueryList();
    int Length() const;
    int Insert(int index, HINSTANCE hInstance, HWND hWndParent, const QUERY &in,
               const RECORDING_OPTION &defaultRecOption, LPCTSTR serviceName, LPCTSTR captionSuffix);
    const QUERY *Get(int index) const;
    bool CreateReserve(int index, RESERVE *pRes, WORD eventID, LPCTSTR eventName, FILETIME startTime, int duration);
    bool Load();
    bool Save() const;
    void SetPluginFileName(LPCTSTR fileName);
    HMENU CreateListMenu(int idStart) const;
};

#endif // INCLUDE_QUERY_LIST_H
