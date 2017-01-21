#ifndef INCLUDE_RESERVE_LIST_H
#define INCLUDE_RESERVE_LIST_H

struct RESERVE {
    bool isEnabled;
    WORD networkID;
    WORD transportStreamID;
    WORD serviceID;
    WORD eventID;
    FILETIME startTime;
    int duration;
    BYTE updateByPf; // 1=EIT[p/f]によりstartTimeまたはdurationが更新された, 2=durationはさらに延長中
    TCHAR eventName[EVENT_NAME_MAX];
    RECORDING_OPTION recOption;
    RESERVE *next;
    FILETIME GetTrimmedStartTime() const {
        FILETIME time = startTime;
        time += max(min(recOption.startTrim, duration), 0) * FILETIME_SECOND;
        return time;
    }
    int GetTrimmedDuration() const {
        return max(duration - recOption.startTrim - recOption.endTrim, 0);
    }
};

class CReserveList
{
    struct DIALOG_PARAMS {
        RESERVE res;
        const RECORDING_OPTION *pDefaultRecOption;
        LPCTSTR serviceName;
        LPCTSTR captionSuffix;
    };

    struct CONTEXT_SAVE_TASK {
        int resumeMargin;
        int execWait;
        TCHAR appPath[MAX_PATH];
        TCHAR driverName[MAX_PATH];
        TCHAR appCmdOption[CMD_OPTION_MAX];
        HWND hwndPost;
        UINT uMsgPost;
        TCHAR saveTaskName[64];
        TCHAR saveTaskNameNoWake[68];
        TCHAR pluginPath[MAX_PATH];
        int resumeTimeNum;
        SYSTEMTIME resumeTime[TASK_TRIGGER_MAX];
        bool resumeIsNoWake[TASK_TRIGGER_MAX];
    };

    RESERVE *m_head;
    TCHAR m_saveFileName[MAX_PATH];
    TCHAR m_saveTaskName[64];
    TCHAR m_pluginPath[MAX_PATH];
    HANDLE m_hThread;
    CONTEXT_SAVE_TASK m_saveTask;

    void Clear();
    static void ToString(const RESERVE &res, LPTSTR str);
    bool Insert(LPCTSTR str);
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    RESERVE *GetNearest(const RECORDING_OPTION &defaultRecOption, RESERVE **pPrev, bool fEnabledOnly) const;
    static DWORD WINAPI SaveTaskThread(LPVOID pParam);
public:
    CReserveList();
    ~CReserveList();
    bool Insert(const RESERVE &in);
    bool Insert(HINSTANCE hInstance, HWND hWndParent, const RESERVE &in,
                const RECORDING_OPTION &defaultRecOption, LPCTSTR serviceName, LPCTSTR captionSuffix);
    bool Delete(DWORD networkID, DWORD transportStreamID, DWORD serviceID, DWORD eventID);
    const RESERVE *Get(DWORD networkID, DWORD transportStreamID, DWORD serviceID, DWORD eventID) const;
    const RESERVE *Get(int index) const;
    bool Load();
    bool Save() const;
    const RESERVE *GetNearest(const RECORDING_OPTION &defaultRecOption, bool fEnabledOnly = true) const;
    bool GetNearest(RESERVE *pRes, const RECORDING_OPTION &defaultRecOption, int readyOffset) const;
    bool DeleteNearest(const RECORDING_OPTION &defaultRecOption, bool fEnabledOnly = true);
    void SetPluginFileName(LPCTSTR fileName);
    bool RunSaveTask(bool fNoWakeViewOnly, int resumeMargin, int execWait, LPCTSTR appName, LPCTSTR driverName,
                     LPCTSTR appCmdOption, HWND hwndPost = NULL, UINT uMsgPost = 0);
    HMENU CreateListMenu(int idStart) const;
};

#endif // INCLUDE_RESERVE_LIST_H
