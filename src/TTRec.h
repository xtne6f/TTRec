#ifndef INCLUDE_TTREC_H
#define INCLUDE_TTREC_H

// プラグインクラス
class CTTRec : public TVTest::CTVTestPlugin
{
    // 直近予約の録画を処理する間隔(ミリ秒)
    static const int CHECK_RECORDING_INTERVAL = 2000;
    // クエリチェックが一巡する間隔(CHECK_RECORDING_INTERVALに対する倍率)
    static const int CHECK_QUERY_INTERVAL = 30;
    // 予約の追従間隔
    static const int FOLLOWUP_INTERVAL = 15;
    // 終了時刻未定の予約があるとき必要なら余分に引き延ばす秒数
    static const int FOLLOWUP_UNDEF_DURATION = 180;
    // イベントリレー予約を作成するタイミング(秒)
    static const int EVENT_RELAY_CREATE_TIME = 60 + FOLLOWUP_INTERVAL;
    // イベントリレー予約の長さ(秒)
    static const int EVENT_RELAY_CREATE_DURATION = 300;
    // TOT取得のタイムアウト(ミリ秒)
    static const unsigned int TOT_GRAB_TIMEOUT = 60000;
    // バルーンチップの表示時間(ミリ秒)
    static const int BALLOON_TIP_TIMEOUT = 10000;
    // 予約開始時にステータス(エラーパケット数など)を取得するまでの待ち時間(ミリ秒)
    static const int GET_START_STATUS_INFO_DELAY = 10000;
    // 終了確認ダイアログの表示時間(秒)
    static const int ON_STOPPED_DLG_TIMEOUT = 15;
    // 追従処理する予約の最大件数
    static const int FOLLOW_UP_MAX = 5;
    // TOT時刻補正の最大値の設定上限(分)
    static const int TOT_ADJUST_MAX_MAX = 15;
    // 予約待機状態に入るオフセット(秒)(予約開始まで安定に保つべき時間)
    static const int REC_READY_OFFSET = 10;
    // 1チャンネルあたりのEPG取得時間の最悪値(秒)
    // 根拠はTVTest_0.8.0_Src/TVTest.cpp/TIMER_ID_PROGRAMGUIDEUPDATE
    static const int EPGCAP_TIMEOUT = 360 + 30;
    // 根拠はTVTest_0.7.23_Src/TVTest.cpp/BeginProgramGuideUpdate()
    static const int EPGCAP_TIMEOUT_OLD = 120 + 30;

    struct RECORDING_INFO {
        bool fEnabled;
        RESERVE reserve;
        TVTest::StatusInfo startStatusInfo;
        TVTest::StatusInfo endStatusInfo;
        TCHAR filePath[MAX_PATH];
        TCHAR serviceName[64];
        TVTest::EpgEventInfo *pEpgEventInfo; // 解放忘れ注意
    };
    enum {
        CHECK_RECORDING_TIMER_ID = 1,
        HIDE_BALLOON_TIP_TIMER_ID,
        GET_START_STATUS_INFO_TIMER_ID,
        DONE_APP_SUSPEND_TIMER_ID,
        WATCH_EPGCAP_TIMER_ID,
    };
    // 番組表メニュー・ダブルクリックコマンド
    enum {
        COMMAND_RESERVE,        // 予約登録/変更
        COMMAND_QUERY,          // クエリ登録
        COMMAND_RESERVE_DEFAULT,           // デフォルト予約登録/設定
        COMMAND_RESERVE_DEFAULT_OR_DELETE, // デフォルト予約登録/削除
        COMMAND_RESERVELIST,    // 予約一覧表示
        COMMAND_QUERYLIST = COMMAND_RESERVELIST + MENULIST_MAX, // クエリ一覧表示
        NUM_COMMANDS = COMMAND_QUERYLIST + MENULIST_MAX
    };
    static const TVTest::ProgramGuideCommandInfo PROGRAM_GUIDE_COMMAND_LIST[4];
public:
    CTTRec();
    virtual bool GetPluginInfo(TVTest::PluginInfo *pInfo);
    virtual bool Initialize();
    virtual bool Finalize();
private:
    void LoadSettings();
    void SaveSettings() const;
    bool InitializePlugin();
    bool EnablePlugin(bool fEnable, bool fExit = false);
    static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);
    void ShowBalloonTip(LPCTSTR text, int notifyLevel);
    void RunSaveTask();
    // プログラムガイド
    bool DrawBackground(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                        const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo) const;
    void DrawReservePriority(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                             const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo,
                             const RESERVE &res, COLORREF color) const;
    static void DrawReserveFrame(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                                 const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo,
                                 const RESERVE &res, COLORREF color, bool fDash, bool fNarrow);
    static void GetReserveFrameRect(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                                    const RESERVE &res, const RECT &itemRect, RECT *pFrameRect);
    int InitializeMenu(const TVTest::ProgramGuideInitializeMenuInfo *pInfo);
    int InitializeProgramMenu(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                              const TVTest::ProgramGuideProgramInitializeMenuInfo *pInfo);
    TVTest::EpgEventInfo *GetEventInfo(const TVTest::ProgramGuideProgramInfo *pProgramInfo);
    bool OnMenuOrProgramMenuSelected(const TVTest::ProgramGuideProgramInfo *pProgramInfo,UINT Command);
    void RedrawProgramGuide() const { if (m_hwndProgramGuide) ::InvalidateRect(m_hwndProgramGuide, NULL, TRUE); }
    static INT_PTR CALLBACK ShowModalDialogDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static INT_PTR ShowModalDialog(HINSTANCE hinst, LPCWSTR pszTemplate, TVTest::DialogMessageFunc pMessageFunc,
                                   void *pClientData, HWND hwndOwner, void *pParam);
    // プラグイン設定
    bool PluginSettings(HWND hwndOwner);
    static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam, void *pClientData);
    // 録画
    bool IsEventMatch(const TVTest::EpgEventInfo &ev, const QUERY &q);
    void CheckQuery();
    void FollowUpReserves();
    bool GetChannel(int *pSpace, int *pChannel, WORD networkID, WORD serviceID);
    bool GetChannelName(LPTSTR name, int max, WORD networkID, WORD serviceID);
    bool SetChannel(WORD networkID, WORD serviceID);
    bool StartRecord(LPCTSTR saveDir, LPCTSTR saveName);
    bool IsNotRecording();
    void ResetRecording();
    void CheckRecording();
    static bool ExecuteCommandLine(LPTSTR commandLine, LPCTSTR currentDirectory, const RECORDING_INFO &info, LPCTSTR envExec);
    void OnStartRecording();
    void OnEndRecording();
    HWND GetTTRecWindow();
    HWND GetFullscreenWindow();
    bool OnStopped(BYTE mode);
    static INT_PTR CALLBACK OnStoppedDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam, void *pClientData);
    static LRESULT CALLBACK RecordingWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    // 時刻補正
    void InitializeTotAdjust();
    void UpdateTotAdjust();
    static BOOL CALLBACK StreamCallback(BYTE *pData, void *pClientData);

    HANDLE m_hMutex;
    HANDLE m_hModuleMutex;
    bool m_fInitialized;
    bool m_fSettingsLoaded;
    TCHAR m_szIniFileName[MAX_PATH];
    HWND m_hwndProgramGuide;
    TCHAR m_szCaptionSuffix[32];
    TCHAR m_szDefaultStatusItemPrefix[32];
    CBalloonTip m_balloonTip;
    CNotifyIcon m_notifyIcon;

    // 設定
    TCHAR m_szDriverName[MAX_PATH];
    TCHAR m_szSubDriverName[MAX_PATH];
    int m_totAdjustMax;
    bool m_usesTask;
    bool m_fNoWakeViewOnly;
    int m_resumeMargin;
    int m_suspendMargin;
    TCHAR m_szCmdOption[CMD_OPTION_MAX];
    bool m_joinsEvents;
    bool m_fEventRelay;
    int m_chChangeBefore;
    int m_spinUpBefore;
    int m_suspendWait;
    int m_execWait;
    bool m_fForceSuspend;
    bool m_fDoSetPreview;
    bool m_fDoSetPreviewNoViewOnly;
    bool m_fShowDlgOnAppSuspend;
    bool m_fShowNotifyIcon;
    bool m_fStatusItemVisible;
    bool m_fAlwaysDrawProgramRect;
    int m_appSuspendTimeout;
    int m_notifyLevel;
    int m_logLevel;
    RECORDING_OPTION m_defaultRecOption;
    COLORREF m_normalColor;
    COLORREF m_disabledColor;
    COLORREF m_inactiveNormalColor;
    COLORREF m_inactiveDisabledColor;
    COLORREF m_nearestColor;
    COLORREF m_recColor;
    COLORREF m_priorityColor;
    TCHAR m_szExecOnStartRec[MAX_PATH];
    TCHAR m_szExecOnEndRec[MAX_PATH];
    TCHAR m_szEventNameTr[512];
    TCHAR m_szEventNameRm[512];
    TCHAR m_szStatusItemPrefix[32];

    // 録画
    HWND m_hwndRecording;
    enum { REC_IDLE, REC_STANDBY, REC_READY, REC_ACTIVE, REC_ACTIVE_VIEW_ONLY, REC_ENDED, REC_CANCELED } m_recordingState;
    CReserveList m_reserveList;
    CQueryList m_queryList;
    RESERVE m_nearest;
    BYTE m_onStopped;
    DWORD m_checkRecordingCount;
    int m_checkQueryIndex;
    int m_followUpIndex;
    bool m_fFollowUpFast;
    bool m_fChChanged;
    bool m_fSpunUp;
    bool m_fStopRecording;
    bool m_fOnStoppedPostponed;
    bool m_fAwayModeSet;
    int m_epgCapTimeout;
    int m_epgCapSpace;
    int m_epgCapChannel;
    RECORDING_INFO m_recordingInfo;

    // 時刻補正
    CCriticalLock m_totLock;
    bool m_totIsValid;
    FILETIME m_totGrabbedTime;
    DWORD m_totGrabbedTick;
    FILETIME m_totAdjustedNow;
    DWORD m_totAdjustedTick;
};

#endif // INCLUDE_TTREC_H
