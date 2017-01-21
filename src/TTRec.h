#ifndef INCLUDE_TTREC_H
#define INCLUDE_TTREC_H

// プラグインクラス
class CTTRec : public TVTest::CTVTestPlugin
{
    // TVTestのProgramListを利用する処理(予約の追従・クエリチェック)の監視間隔(ミリ秒)
    // ProgramListの更新間隔は1～5分のようなので、あまり小さくしても無意味
    static const int CHECK_PROGRAMLIST_INTERVAL = 30000;
    // 直近予約の録画を処理する間隔(ミリ秒)
    static const int CHECK_RECORDING_INTERVAL = 2000;
    // TOT取得のタイムアウト(ミリ秒)
    static const unsigned int TOT_GRAB_TIMEOUT = 60000;
    // バルーンチップの表示時間(ミリ秒)
    static const int BALLOON_TIP_TIMEOUT = 10000;
    // 終了確認ダイアログの表示時間(秒)
    static const int ON_STOPPED_DLG_TIMEOUT = 15;
    // 追従処理する予約の最大件数
    static const int FOLLOW_UP_MAX = 5;
    // TOT時刻補正の最大値の設定上限(分)
    static const int TOT_ADJUST_MAX_MAX = 15;
    // 予約待機状態に入るオフセット(秒)(予約開始まで安定に保つべき時間)
    static const int REC_READY_OFFSET = 10;

    enum {
        FOLLOW_UP_TIMER_ID = 1,
        CHECK_QUERY_LIST_TIMER_ID,
        CHECK_RECORDING_TIMER_ID,
        HIDE_BALLOON_TIP_TIMER_ID,
    };
    // メニューのコマンド
    enum {
        COMMAND_RESERVE,        // 予約登録/変更
        COMMAND_QUERY,          // クエリ登録
        COMMAND_RESERVELIST,    // 予約一覧表示
        COMMAND_QUERYLIST = COMMAND_RESERVELIST + MENULIST_MAX, // クエリ一覧表示
        NUM_COMMANDS = COMMAND_QUERYLIST + MENULIST_MAX
    };
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
    void DrawReserveFrame(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                          const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo,
                          const RESERVE &res, COLORREF color, bool fDash) const;
    void GetReserveFrameRect(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                             const RESERVE &res, const RECT &itemRect, RECT *pFrameRect) const;
    int InitializeMenu(const TVTest::ProgramGuideInitializeMenuInfo *pInfo);
    int InitializeProgramMenu(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                              const TVTest::ProgramGuideProgramInitializeMenuInfo *pInfo);
    TVTest::EpgEventInfo *GetEventInfo(const TVTest::ProgramGuideProgramInfo *pProgramInfo);
    bool OnMenuOrProgramMenuSelected(const TVTest::ProgramGuideProgramInfo *pProgramInfo,UINT Command);
    // プラグイン設定
    bool PluginSettings(HWND hwndOwner);
    static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg,UINT uMsg,WPARAM wParam,LPARAM lParam);
    // 録画
    bool IsEventMatch(const TVTest::EpgEventInfo &ev, const QUERY &q);
    void CheckQuery();
    void FollowUpReserves();
    bool UpdateNearest(const FILETIME &now);
    bool GetChannel(int *pSpace, int *pChannel, WORD networkID, WORD serviceID);
    bool GetChannelName(LPTSTR name, int max, WORD networkID, WORD serviceID);
    bool SetChannel(WORD networkID, WORD serviceID);
    bool StartRecord(LPCTSTR saveDir, LPCTSTR saveName);
    bool IsNotRecording();
    void ResetRecording();
    void CheckRecording();
    HWND GetFullscreenWindow();
    void OnStopped(BYTE mode);
    static INT_PTR CALLBACK OnStoppedDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
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
    bool m_fVistaOrLater;
    CBalloonTip m_balloonTip;

    // 設定
    TCHAR m_szDriverName[MAX_PATH];
    TCHAR m_szAppName[MAX_PATH];
    int m_totAdjustMax;
    bool m_usesTask;
    int m_resumeMargin;
    int m_suspendMargin;
    TCHAR m_szCmdOption[CMD_OPTION_MAX];
    bool m_joinsEvents;
    int m_chChangeBefore;
    int m_spinUpBefore;
    int m_suspendWait;
    int m_execWait;
    bool m_fForceSuspend;
    bool m_fDoSetPreview;
    int m_notifyLevel;
    int m_logLevel;
    RECORDING_OPTION m_defaultRecOption;
    COLORREF m_normalColor;
    COLORREF m_nearestColor;
    COLORREF m_recColor;
    COLORREF m_priorityColor;

    // 録画
    HWND m_hwndRecording;
    enum { REC_IDLE, REC_STANDBY, REC_READY, REC_ACTIVE, REC_ACTIVE_VIEW_ONLY, REC_STOPPED } m_recordingState;
    CReserveList m_reserveList;
    CQueryList m_queryList;
    RESERVE m_nearest;
    BYTE m_onStopped;
    int m_checkQueryIndex;
    int m_followUpIndex;
    bool m_fChChanged;
    bool m_fSpunUp;
    bool m_fStopRecording;
    EXECUTION_STATE m_prevExecState;

    // 時刻補正
    CCriticalLock m_totLock;
    bool m_totIsValid;
    FILETIME m_totGrabbedTime;
    DWORD m_totGrabbedTick;
    FILETIME m_totAdjustedNow;
    DWORD m_totAdjustedTick;
};

#endif // INCLUDE_TTREC_H
