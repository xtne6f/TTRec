#ifndef INCLUDE_RESERVE_LIST_H
#define INCLUDE_RESERVE_LIST_H

#include "Util.h"
#include "RecordingOption.h"
#include "TVTestPlugin.h"
#include <Windows.h>

typedef struct RESERVE {
    WORD networkID;
    WORD transportStreamID;
    WORD serviceID;
    WORD eventID;
    FILETIME startTime;
    int duration;
    TCHAR eventName[EVENT_NAME_MAX];
    RECORDING_OPTION recOption;
    RESERVE *next;
} RESERVE;

class CReserveList
{
    RESERVE *m_head;
    TCHAR m_saveFileName[MAX_PATH];
    TCHAR m_saveTaskName[64];
    TCHAR m_pluginShortPath[MAX_PATH];
    TVTest::CTVTestApp *m_pApp; // デバッグ用

    void Clear();
    static void ToString(const RESERVE &res, LPTSTR str);
    bool Insert(LPCTSTR str);
    bool Delete(DWORD networkID, DWORD transportStreamID, DWORD serviceID, DWORD eventID);
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    RESERVE *GetNearest(const RECORDING_OPTION &defaultRecOption, RESERVE **pPrev) const;
public:
    CReserveList();
    ~CReserveList();
    bool Insert(const RESERVE &in);
    bool Insert(HINSTANCE hInstance, HWND hWndParent, const RESERVE &in, const RECORDING_OPTION &defaultRecOption, LPCTSTR serviceName);
    const RESERVE *Get(DWORD networkID, DWORD transportStreamID, DWORD serviceID, DWORD eventID) const;
    const RESERVE *Get(int index) const;
    bool Load();
    bool Save() const;
    const RESERVE *GetNearest(const RECORDING_OPTION &defaultRecOption) const;
    bool GetNearest(RESERVE *pRes, const RECORDING_OPTION &defaultRecOption, int readyOffset) const;
    bool DeleteNearest(const RECORDING_OPTION &defaultRecOption);
    void SetPluginFileName(LPCTSTR fileName);
    bool SaveTask(int resumeMargin, int execWait, LPCTSTR tvTestAppName, LPCTSTR driverName, LPCTSTR tvTestCmdOption) const;
    HMENU CreateListMenu(int idStart) const;
#ifdef _DEBUG
    void SetTVTestApp(TVTest::CTVTestApp *pApp);
#endif
};

#endif // INCLUDE_RESERVE_LIST_H
