#ifndef INCLUDE_RESERVE_LIST_H
#define INCLUDE_RESERVE_LIST_H

#include <Lmcons.h>

struct RESERVE {
    WORD networkID;
    WORD transportStreamID;
    WORD serviceID;
    WORD eventID;
    FILETIME startTime;
    int duration;
    TCHAR eventName[EVENT_NAME_MAX];
    RECORDING_OPTION recOption;
    RESERVE *next;
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
        TCHAR tvTestAppName[MAX_PATH];
        TCHAR driverName[MAX_PATH];
        TCHAR tvTestCmdOption[CMD_OPTION_MAX];
        HWND hwndPost;
        UINT uMsgPost;
    };

    RESERVE *m_head;
    TCHAR m_saveFileName[MAX_PATH];
    TCHAR m_saveTaskName[64];
    TCHAR m_pluginShortPath[MAX_PATH];
    HANDLE m_hThread;
    CONTEXT_SAVE_TASK m_saveTask;
    CCriticalLock m_writeLock;  // メンバへの書き込み時に必ず獲得

    void Clear();
    static void ToString(const RESERVE &res, LPTSTR str);
    bool Insert(LPCTSTR str);
    bool Delete(DWORD networkID, DWORD transportStreamID, DWORD serviceID, DWORD eventID);
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    RESERVE *GetNearest(const RECORDING_OPTION &defaultRecOption, RESERVE **pPrev) const;
    static DWORD WINAPI SaveTaskThread(LPVOID pParam);
public:
    CReserveList();
    ~CReserveList();
    bool Insert(const RESERVE &in);
    bool Insert(HINSTANCE hInstance, HWND hWndParent, const RESERVE &in,
                const RECORDING_OPTION &defaultRecOption, LPCTSTR serviceName, LPCTSTR captionSuffix);
    const RESERVE *Get(DWORD networkID, DWORD transportStreamID, DWORD serviceID, DWORD eventID) const;
    const RESERVE *Get(int index) const;
    bool Load();
    bool Save() const;
    const RESERVE *GetNearest(const RECORDING_OPTION &defaultRecOption) const;
    bool GetNearest(RESERVE *pRes, const RECORDING_OPTION &defaultRecOption, int readyOffset) const;
    bool DeleteNearest(const RECORDING_OPTION &defaultRecOption);
    void SetPluginFileName(LPCTSTR fileName);
    bool RunSaveTask(int resumeMargin, int execWait, LPCTSTR tvTestAppName, LPCTSTR driverName,
                     LPCTSTR tvTestCmdOption, HWND hwndPost = NULL, UINT uMsgPost = 0);
    HMENU CreateListMenu(int idStart) const;
};

#endif // INCLUDE_RESERVE_LIST_H
