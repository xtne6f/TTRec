// TVTestの予約録画機能を拡張するプラグイン
// 最終更新: 2011-06-29
// 署名: 9a5ad966ee38e172c4b5766a2bb71fea
#include <Windows.h>
#include "Util.h"
#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TVTestPlugin.h"
#include "RecordingOption.h"
#include "QueryList.h"
#include "ReserveList.h"
#include "resource.h"
#include <Shlwapi.h>
#include <CommCtrl.h>

static const LPCTSTR TTREC_REC_WINDOW_CLASS = TEXT("TVTest TTRec Recording");

// プラグインクラス
class CTTRec : public TVTest::CTVTestPlugin
{
    // TVTestのProgramListを利用する処理(予約の追従・クエリチェック)の監視間隔(ミリ秒)
    // ProgramListの更新間隔は1～5分のようなので、あまり小さくしても無意味
    static const int CHECK_PROGRAMLIST_INTERVAL = 60000;
    // 直近予約の録画を処理する間隔(ミリ秒)
    static const int CHECK_RECORDING_INTERVAL = 2000;
    // TOT取得のタイムアウト(ミリ秒)
    static const unsigned int TOT_GRAB_TIMEOUT = 60000;
    // 終了確認ダイアログの表示時間(秒)
    static const int ON_STOPPED_DLG_TIMEOUT = 15;
    // 追従処理する予約の最大件数
    static const int FOLLOW_UP_MAX = 5;
    // TOT時刻補正の最大値の設定上限+1(分)
    static const int TOT_ADJUST_MAX_MAX = 16;
    // 予約待機状態に入るオフセット(秒)(予約開始まで安定に保つべき時間)
    static const int REC_READY_OFFSET = 10;
    
    static const int FOLLOW_UP_TIMER_ID         = 1;
    static const int CHECK_QUERY_LIST_TIMER_ID  = 2;
    static const int CHECK_RECORDING_TIMER_ID   = 3;

    // メニューのコマンド
    enum {
        COMMAND_RESERVE,        // 予約登録/変更
        COMMAND_QUERY,          // クエリ登録
        COMMAND_RESERVELIST,    // 予約一覧表示
        COMMAND_QUERYLIST = COMMAND_RESERVELIST + MENULIST_MAX, // クエリ一覧表示
        NUM_COMMANDS = COMMAND_QUERYLIST + MENULIST_MAX
    };
    
    bool m_fInitialized;
    bool m_fSettingsLoaded;
    TCHAR m_szPluginFileName[MAX_PATH];
    TCHAR m_szIniFileName[MAX_PATH];
    HWND m_hwndProgramGuide;

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
    RECORDING_OPTION m_defaultRecOption;
    COLORREF m_normalColor;
    COLORREF m_nearestColor;
    COLORREF m_recColor;
    COLORREF m_priorityColor;
    
    // 録画
    HWND m_hwndRecording;
    enum { REC_IDLE, REC_READY, REC_ACTIVE, REC_STOPPED } m_recordingState;
    CReserveList m_reserveList;
    CQueryList m_queryList;
    RESERVE m_nearest;
    BYTE m_onStopped;
    int m_checkQueryIndex;
    bool m_fChChanged;
    bool m_fSpunUp;
    bool m_fStopRecording;

    // 時刻補正
    CRITICAL_SECTION m_totSection;
    bool m_totIsValid;
    FILETIME m_totGrabbedTime;
    DWORD m_totGrabbedTick;
    FILETIME m_totAdjustedNow;
    DWORD m_totAdjustedTick;

    void LoadSettings();
    void SaveSettings() const;
    bool InitializePlugin();
    bool EnablePlugin(bool fEnable);
    static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);
    
    // プログラムガイド
    bool DrawBackground(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                        const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo) const;
    void DrawReservePriority(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                             const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo,
                             const RESERVE &res, COLORREF color) const;
    void DrawReserveFrame(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                          const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo,
                          const RESERVE &res, COLORREF color) const;
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
    void CheckRecording();
    void OnStopped();
    static INT_PTR CALLBACK OnStoppedDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK RecordingWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // 時刻補正
    void InitializeTotAdjust();
    void UpdateTotAdjust();
    static BOOL CALLBACK StreamCallback(BYTE *pData, void *pClientData);

public:
    CTTRec();
    ~CTTRec();
    virtual bool GetPluginInfo(TVTest::PluginInfo *pInfo);
    virtual bool Initialize();
    virtual bool Finalize();
};


CTTRec::CTTRec()
    : m_fInitialized(false)
    , m_fSettingsLoaded(false)
    , m_hwndProgramGuide(NULL)
    , m_totAdjustMax(0)
    , m_usesTask(false)
    , m_resumeMargin(0)
    , m_suspendMargin(0)
    , m_joinsEvents(false)
    , m_chChangeBefore(0)
    , m_spinUpBefore(0)
    , m_suspendWait(0)
    , m_execWait(0)
    , m_fForceSuspend(false)
    , m_normalColor(RGB(0,0,0))
    , m_nearestColor(RGB(0,0,0))
    , m_recColor(RGB(0,0,0))
    , m_priorityColor(RGB(0,0,0))
    , m_hwndRecording(NULL)
    , m_recordingState(REC_IDLE)
    , m_onStopped(ON_STOPPED_NONE)
    , m_checkQueryIndex(0)
    , m_fChChanged(false)
    , m_fSpunUp(false)
    , m_fStopRecording(false)
    , m_totIsValid(false)
    , m_totGrabbedTick(0)
    , m_totAdjustedTick(0)
{
    m_szPluginFileName[0] = 0;
    m_szIniFileName[0] = 0;
    m_szDriverName[0] = 0;
    m_szAppName[0] = 0;
    m_szCmdOption[0] = 0;
    m_nearest.eventID = 0xFFFF;
    ::InitializeCriticalSection(&m_totSection);
}


CTTRec::~CTTRec()
{
    ::DeleteCriticalSection(&m_totSection);
}


bool CTTRec::GetPluginInfo(TVTest::PluginInfo *pInfo)
{
    // プラグインの情報を返す
    pInfo->Type           = TVTest::PLUGIN_TYPE_NORMAL;
    pInfo->Flags          = TVTest::PLUGIN_FLAG_HASSETTINGS;
    pInfo->pszPluginName  = L"TTRec";
    pInfo->pszCopyright   = L"Public Domain";
    pInfo->pszDescription = L"予約録画機能を拡張";
    return true;
}


// 初期化処理
bool CTTRec::Initialize()
{
    // 初期化処理
#ifdef _DEBUG
    m_reserveList.SetTVTestApp(m_pApp);
    m_queryList.SetTVTestApp(m_pApp);
#endif
    
    // 番組表のイベントの通知を有効にする(m_hwndProgramGuideを取得し続けるため)
    m_pApp->EnableProgramGuideEvent(TVTest::PROGRAMGUIDE_EVENT_GENERAL);

    // イベントコールバック関数を登録
    m_pApp->SetEventCallback(EventCallback, this);
    return true;
}


// 終了処理
bool CTTRec::Finalize()
{
    if (m_pApp->IsPluginEnabled()) {
        // ストリームコールバックの登録解除
        m_pApp->SetStreamCallback(TVTest::STREAM_CALLBACK_REMOVE, StreamCallback);
        // 録画制御ウィンドウの破棄
        if (m_hwndRecording) ::DestroyWindow(m_hwndRecording);
    }
    return true;
}


// 設定の読み込み
void CTTRec::LoadSettings()
{
    if (m_fSettingsLoaded) return;
    
    if (!::GetModuleFileName(g_hinstDLL, m_szIniFileName, ARRAY_SIZE(m_szIniFileName)) ||
        !::PathRenameExtension(m_szIniFileName, TEXT(".ini"))) m_szIniFileName[0] = 0;

    // TODO: TVTest本体のファイル名をちゃんと取る
    TVTest::HostInfo hostInfo;
    if (m_pApp->GetHostInfo(&hostInfo)) ::wsprintf(m_szAppName, TEXT("..\\%s.exe"), hostInfo.pszAppName);

    ::GetPrivateProfileString(TEXT("Settings"), TEXT("Driver"), TEXT(""),
                              m_szDriverName, ARRAY_SIZE(m_szDriverName), m_szIniFileName);
    if (!m_szDriverName[0]) m_pApp->GetDriverName(m_szDriverName, ARRAY_SIZE(m_szDriverName));

    m_totAdjustMax = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("TotAdjustMax"), 0, m_szIniFileName);
    if (m_totAdjustMax < 0 || m_totAdjustMax >= TOT_ADJUST_MAX_MAX) m_totAdjustMax = 0;

    m_usesTask = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("UseTask"), 0, m_szIniFileName) != 0;

    m_resumeMargin = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ResumeMargin"), 5, m_szIniFileName);
    if (m_resumeMargin < 0) m_resumeMargin = 5;

    m_suspendMargin = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("SuspendMargin"), 5, m_szIniFileName);
    if (m_suspendMargin < 0) m_suspendMargin = 5;

    ::GetPrivateProfileString(TEXT("Settings"), TEXT("TVTestCmdOption"), TEXT(""),
                              m_szCmdOption, ARRAY_SIZE(m_szCmdOption), m_szIniFileName);

    m_joinsEvents = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("JoinEvents"), 0, m_szIniFileName) != 0;

    m_chChangeBefore = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ChChangeBefore"), 120, m_szIniFileName);
    if (m_chChangeBefore <= 0) m_chChangeBefore = 0;
    else if (m_chChangeBefore < 15) m_chChangeBefore = 15;

    m_spinUpBefore = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("SpinUpBefore"), 20, m_szIniFileName);
    if (m_spinUpBefore <= 0) m_spinUpBefore = 0;
    else if (m_spinUpBefore < 15) m_spinUpBefore = 15;
    
    m_suspendWait = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("SuspendWait"), 5, m_szIniFileName);
    if (m_suspendWait < 0) m_suspendWait = 0;

    m_execWait = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ExecWait"), 10, m_szIniFileName);
    if (m_execWait < 0) m_execWait = 0;
    
    m_fForceSuspend = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ForceSuspend"), 0, m_szIniFileName) != 0;

    int color;
    color = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("NormalColor"), 64255000, m_szIniFileName);
    m_normalColor = RGB(color/1000000%1000, color/1000%1000, color%1000);
    color = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("NearestColor"), 255160000, m_szIniFileName);
    m_nearestColor = RGB(color/1000000%1000, color/1000%1000, color%1000);
    color = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("RecColor"), 255064000, m_szIniFileName);
    m_recColor = RGB(color/1000000%1000, color/1000%1000, color%1000);
    color = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("PriorityColor"), 64064064, m_szIniFileName);
    m_priorityColor = RGB(color/1000000%1000, color/1000%1000, color%1000);

    RecordingOption::LoadSetting(m_szIniFileName, &m_defaultRecOption);

    // デフォルト保存先フォルダはTVTest本体の設定を使用する
    if (m_pApp->GetSetting(L"RecordFolder", m_defaultRecOption.saveDir,
                           ARRAY_SIZE(m_defaultRecOption.saveDir)) <= 0) m_defaultRecOption.saveDir[0] = 0;

    m_fSettingsLoaded = true;
}


// 設定の保存
void CTTRec::SaveSettings() const
{
    if (!m_fSettingsLoaded) return;
    
    ::WritePrivateProfileString(TEXT("Settings"), TEXT("Driver"), m_szDriverName, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("TotAdjustMax"), m_totAdjustMax, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("UseTask"), m_usesTask ? 1 : 0, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ResumeMargin"), m_resumeMargin, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SuspendMargin"), m_suspendMargin, m_szIniFileName);
    ::WritePrivateProfileString(TEXT("Settings"), TEXT("TVTestCmdOption"), m_szCmdOption, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("JoinEvents"), m_joinsEvents ? 1 : 0, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ChChangeBefore"), m_chChangeBefore, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SpinUpBefore"), m_spinUpBefore, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SuspendWait"), m_suspendWait, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ExecWait"), m_execWait, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ForceSuspend"), m_fForceSuspend ? 1 : 0, m_szIniFileName);

    WritePrivateProfileInt(TEXT("Settings"), TEXT("NormalColor"),
                           GetRValue(m_normalColor)*1000000 + GetGValue(m_normalColor)*1000 + GetBValue(m_normalColor),
                           m_szIniFileName);
    
    WritePrivateProfileInt(TEXT("Settings"), TEXT("NearestColor"),
                           GetRValue(m_nearestColor)*1000000 + GetGValue(m_nearestColor)*1000 + GetBValue(m_nearestColor),
                           m_szIniFileName);

    WritePrivateProfileInt(TEXT("Settings"), TEXT("RecColor"),
                           GetRValue(m_recColor)*1000000 + GetGValue(m_recColor)*1000 + GetBValue(m_recColor),
                           m_szIniFileName);
    
    WritePrivateProfileInt(TEXT("Settings"), TEXT("PriorityColor"),
                           GetRValue(m_priorityColor)*1000000 + GetGValue(m_priorityColor)*1000 + GetBValue(m_priorityColor),
                           m_szIniFileName);
    
    RecordingOption::SaveSetting(m_szIniFileName, m_defaultRecOption);
}


// プラグインが有効にされた時の初期化処理
bool CTTRec::InitializePlugin()
{
    if (m_fInitialized) return true;

    if (!m_pApp->QueryMessage(TVTest::MESSAGE_ENABLEPROGRAMGUIDEEVENT)) {
        m_pApp->AddLog(L"初期化失敗(TVTestのバージョンが古いようです)。");
        return false;
    }
    
    // ウィンドウクラスの登録
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = RecordingWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinstDLL;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = TTREC_REC_WINDOW_CLASS;
    if (::RegisterClass(&wc) == 0) return false;

    LoadSettings();
    
    if (!::GetModuleFileName(g_hinstDLL, m_szPluginFileName, ARRAY_SIZE(m_szPluginFileName))) return false;
    
    // 予約リスト初期化
    m_reserveList.SetPluginFileName(m_szPluginFileName);
    m_reserveList.Load();

    // クエリリスト初期化
    m_queryList.SetPluginFileName(m_szPluginFileName);
    m_queryList.Load();

    m_fInitialized = true;
    return true;
}


// プラグインの有効状態が変化した
bool CTTRec::EnablePlugin(bool fEnable) {
    if (fEnable) {
        if (!InitializePlugin()) return false;
        
        // ストリームコールバックの登録
        InitializeTotAdjust();
        m_pApp->SetStreamCallback(0, StreamCallback, this);

        // 録画制御ウィンドウの作成
        if (!m_hwndRecording) {
            m_hwndRecording = ::CreateWindow(TTREC_REC_WINDOW_CLASS, NULL, WS_POPUP, 
                                             0, 0, 0, 0, HWND_MESSAGE, NULL, g_hinstDLL, this);
            if (!m_hwndRecording) return false;
        }
    }
    else {
        // ストリームコールバックの登録解除
        m_pApp->SetStreamCallback(TVTest::STREAM_CALLBACK_REMOVE, StreamCallback);

        // 録画制御ウィンドウの破棄
        if (m_hwndRecording) {
            ::DestroyWindow(m_hwndRecording);
            m_hwndRecording = NULL;
        }
    }
    
    // 番組表のイベントの通知の有効/無効を設定する
    m_pApp->EnableProgramGuideEvent(TVTest::PROGRAMGUIDE_EVENT_GENERAL |
                                    (fEnable ? TVTest::PROGRAMGUIDE_EVENT_PROGRAM : 0));

    // 番組表が表示されている場合再描画させる
    if (m_hwndProgramGuide) ::InvalidateRect(m_hwndProgramGuide, NULL, TRUE);

    return true;
}


// イベントコールバック関数
// 何かイベントが起きると呼ばれる
LRESULT CALLBACK CTTRec::EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData)
{
    CTTRec *pThis = static_cast<CTTRec*>(pClientData);

    switch (Event) {
    case TVTest::EVENT_PLUGINENABLE:
        // プラグインの有効状態が変化した
        return pThis->EnablePlugin(lParam1 != 0);

    case TVTest::EVENT_PLUGINSETTINGS:
        // プラグインの設定を行う
        return pThis->PluginSettings(reinterpret_cast<HWND>(lParam1));

    case TVTest::EVENT_PROGRAMGUIDE_INITIALIZE:
        // 番組表の初期化処理
        pThis->m_hwndProgramGuide = reinterpret_cast<HWND>(lParam1);
        return true;

    case TVTest::EVENT_PROGRAMGUIDE_FINALIZE:
        // 番組表の終了処理
        pThis->m_hwndProgramGuide = NULL;
        return true;

    case TVTest::EVENT_PROGRAMGUIDE_INITIALIZEMENU:
        // メニューの初期化
        return pThis->InitializeMenu(reinterpret_cast<const TVTest::ProgramGuideInitializeMenuInfo*>(lParam1));
        
    case TVTest::EVENT_PROGRAMGUIDE_MENUSELECTED:
        // メニューが選択された
        return pThis->OnMenuOrProgramMenuSelected(NULL, static_cast<UINT>(lParam1));

    case TVTest::EVENT_PROGRAMGUIDE_PROGRAM_INITIALIZEMENU:
        // 番組のメニューの初期化
        return pThis->InitializeProgramMenu(
            reinterpret_cast<const TVTest::ProgramGuideProgramInfo*>(lParam1),
            reinterpret_cast<const TVTest::ProgramGuideProgramInitializeMenuInfo*>(lParam2));

    case TVTest::EVENT_PROGRAMGUIDE_PROGRAM_MENUSELECTED:
        // 番組のメニューが選択された
        return pThis->OnMenuOrProgramMenuSelected(
            reinterpret_cast<const TVTest::ProgramGuideProgramInfo*>(lParam1),
            static_cast<UINT>(lParam2));
        
    case TVTest::EVENT_PROGRAMGUIDE_PROGRAM_DRAWBACKGROUND:
        // 番組の背景を描画
        return pThis->DrawBackground(
            reinterpret_cast<const TVTest::ProgramGuideProgramInfo*>(lParam1),
            reinterpret_cast<const TVTest::ProgramGuideProgramDrawBackgroundInfo*>(lParam2));

    case TVTest::EVENT_RECORDSTATUSCHANGE:
        // 録画状態が変化した
        // REC_ACTIVE状態で録画が停止した(=ユーザによる停止)
        pThis->m_fStopRecording = lParam1 == TVTest::RECORD_STATUS_NOTRECORDING &&
                                  pThis->m_recordingState == REC_ACTIVE;
        return true;
    }
    return 0;
}


// 番組の背景を描画
bool CTTRec::DrawBackground(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                            const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo) const
{
    const RESERVE *pRes = m_reserveList.Get(pProgramInfo->NetworkID, pProgramInfo->TransportStreamID,
                                            pProgramInfo->ServiceID, pProgramInfo->EventID);
    if (!pRes) return false;

    DrawReserveFrame(pProgramInfo, pInfo, *pRes, m_normalColor);

    if (pRes->eventID == m_nearest.eventID && pRes->networkID == m_nearest.networkID &&
        pRes->transportStreamID == m_nearest.transportStreamID && pRes->serviceID == m_nearest.serviceID)
    {
        if (m_recordingState == REC_ACTIVE) DrawReserveFrame(pProgramInfo, pInfo, m_nearest, m_recColor);
        else DrawReserveFrame(pProgramInfo, pInfo, m_nearest, m_nearestColor);
    }

    DrawReservePriority(pProgramInfo, pInfo, *pRes, m_priorityColor);
    return true;
}


// 予約優先度を描画
void CTTRec::DrawReservePriority(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                                 const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo,
                                 const RESERVE &res, COLORREF color) const
{
    RECT frameRect;
    GetReserveFrameRect(pProgramInfo, res, pInfo->ItemRect, &frameRect);
    
    LOGBRUSH lb;
    lb.lbStyle = BS_SOLID;
    lb.lbColor = color;
    lb.lbHatch = 0;
    HPEN hPen = ::ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_SQUARE, 3, &lb, 0, NULL);
    HGDIOBJ hOld = ::SelectObject(pInfo->hdc, hPen);

    BYTE priority = res.recOption.priority == PRIORITY_DEFAULT ? m_defaultRecOption.priority : res.recOption.priority;
    if (priority == PRIORITY_LOWEST || priority == PRIORITY_HIGHEST) {
        int x = frameRect.right - 19;
        int y = frameRect.bottom - 9;
        ::MoveToEx(pInfo->hdc, x, y + 3, NULL);
        ::LineTo(pInfo->hdc, x + 6, y + 3);
        if (priority == PRIORITY_HIGHEST) {
            ::MoveToEx(pInfo->hdc, x + 3, y, NULL);
            ::LineTo(pInfo->hdc, x + 3, y + 6);
        }
    }
    if (priority != PRIORITY_NORMAL) {
        int x = frameRect.right - 9;
        int y = frameRect.bottom - 9;
        ::MoveToEx(pInfo->hdc, x, y + 3, NULL);
        ::LineTo(pInfo->hdc, x + 6, y + 3);
        if (priority >= PRIORITY_HIGH) {
            ::MoveToEx(pInfo->hdc, x + 3, y, NULL);
            ::LineTo(pInfo->hdc, x + 3, y + 6);
        }
    }

    ::SelectObject(pInfo->hdc, hOld);
    ::DeleteObject(hPen);
}


// 予約の枠を描画
void CTTRec::DrawReserveFrame(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                              const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo,
                              const RESERVE &res, COLORREF color) const
{
    RECT frameRect;
    GetReserveFrameRect(pProgramInfo, res, pInfo->ItemRect, &frameRect);
    
    LOGBRUSH lb;
    lb.lbStyle = BS_SOLID;
    lb.lbColor = color;
    lb.lbHatch = 0;
    HPEN hPen = ::ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_SQUARE, 4, &lb, 0, NULL);

    HGDIOBJ hOld = ::SelectObject(pInfo->hdc, hPen);
    ::MoveToEx(pInfo->hdc, frameRect.left + 2, frameRect.top + 2, NULL);
    ::LineTo(pInfo->hdc, frameRect.right - 2, frameRect.top + 2);
    ::LineTo(pInfo->hdc, frameRect.right - 2, frameRect.bottom - 2);
    ::LineTo(pInfo->hdc, frameRect.left + 2, frameRect.bottom - 2);
    ::LineTo(pInfo->hdc, frameRect.left + 2, frameRect.top + 2);
    ::SelectObject(pInfo->hdc, hOld);
    ::DeleteObject(hPen);
}


// 予約の枠の位置を取得
void CTTRec::GetReserveFrameRect(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                                 const RESERVE &res, const RECT &itemRect, RECT *pFrameRect) const
{
    FILETIME eventStart;
    ::SystemTimeToFileTime(&pProgramInfo->StartTime, &eventStart);

    int startOffset = static_cast<int>((res.startTime - eventStart) / FILETIME_SECOND);
    if (startOffset < 0) startOffset = 0;

    int endOffset = static_cast<int>((res.startTime - eventStart) / FILETIME_SECOND) + res.duration;
    if (endOffset > (int)pProgramInfo->Duration) endOffset = pProgramInfo->Duration;

    int height = itemRect.bottom - itemRect.top;
    pFrameRect->top = itemRect.top + startOffset * height / pProgramInfo->Duration;
    pFrameRect->bottom = itemRect.top + endOffset * height / pProgramInfo->Duration;
    pFrameRect->left=itemRect.left;
    pFrameRect->right=itemRect.right;
}


// メニューの初期化
int CTTRec::InitializeMenu(const TVTest::ProgramGuideInitializeMenuInfo *pInfo)
{
    if (!m_pApp->IsPluginEnabled()) return 0;

    // 予約一覧用サブメニュー作成
    HMENU hMenuReserve = m_reserveList.CreateListMenu(pInfo->Command + COMMAND_RESERVELIST);
    ::AppendMenu(pInfo->hmenu, MF_POPUP | (::GetMenuItemCount(hMenuReserve) <= 0 ? MF_DISABLED : MF_ENABLED),
                 reinterpret_cast<UINT_PTR>(hMenuReserve), TEXT("TTRec予約一覧"));

    // クエリ一覧用サブメニュー作成
    HMENU hMenuQuery = m_queryList.CreateListMenu(pInfo->Command + COMMAND_QUERYLIST);
    ::AppendMenu(pInfo->hmenu, MF_POPUP | (::GetMenuItemCount(hMenuQuery) <= 0 ? MF_DISABLED : MF_ENABLED),
                 reinterpret_cast<UINT_PTR>(hMenuQuery), TEXT("TTRecクエリ一覧"));
    
    // 使用するコマンド数を返す
    return NUM_COMMANDS;
}


// 番組のメニューの初期化
int CTTRec::InitializeProgramMenu(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                                  const TVTest::ProgramGuideProgramInitializeMenuInfo *pInfo)
{
    if (!m_pApp->IsPluginEnabled()) return 0;

    bool fReserved = m_reserveList.Get(pProgramInfo->NetworkID, pProgramInfo->TransportStreamID,
                                       pProgramInfo->ServiceID, pProgramInfo->EventID) != NULL;

    // メニュー追加
    ::AppendMenu(pInfo->hmenu, MF_STRING | MF_ENABLED, pInfo->Command + COMMAND_RESERVE,
                 fReserved ? TEXT("TTRec-予約変更") : TEXT("TTRec-予約登録"));

    ::AppendMenu(pInfo->hmenu, MF_STRING | MF_ENABLED, pInfo->Command + COMMAND_QUERY, TEXT("TTRec-クエリ登録"));
    
    // 予約一覧用サブメニュー作成
    HMENU hMenuReserve = m_reserveList.CreateListMenu(pInfo->Command + COMMAND_RESERVELIST);
    ::AppendMenu(pInfo->hmenu, MF_POPUP | (::GetMenuItemCount(hMenuReserve) <= 0 ? MF_DISABLED : MF_ENABLED),
                 reinterpret_cast<UINT_PTR>(hMenuReserve), TEXT("TTRec-予約一覧"));

    // クエリ一覧用サブメニュー作成
    HMENU hMenuQuery = m_queryList.CreateListMenu(pInfo->Command + COMMAND_QUERYLIST);
    ::AppendMenu(pInfo->hmenu, MF_POPUP | (::GetMenuItemCount(hMenuQuery) <= 0 ? MF_DISABLED : MF_ENABLED),
                 reinterpret_cast<UINT_PTR>(hMenuQuery), TEXT("TTRec-クエリ一覧"));
    
    // 使用するコマンド数を返す
    return NUM_COMMANDS;
}


TVTest::EpgEventInfo *CTTRec::GetEventInfo(const TVTest::ProgramGuideProgramInfo *pProgramInfo)
{
    TVTest::EpgEventQueryInfo QueryInfo;
        
    QueryInfo.NetworkID = pProgramInfo->NetworkID;
    QueryInfo.TransportStreamID = pProgramInfo->TransportStreamID;
    QueryInfo.ServiceID = pProgramInfo->ServiceID;
    QueryInfo.Type = TVTest::EPG_EVENT_QUERY_EVENTID;
    QueryInfo.Flags = 0;
    QueryInfo.EventID = pProgramInfo->EventID;
    return m_pApp->GetEpgEventInfo(&QueryInfo);
}


// メニューまたは番組のメニューが選択された
// 番組のメニューが選択された場合はpProgramInfo!=NULL
bool CTTRec::OnMenuOrProgramMenuSelected(const TVTest::ProgramGuideProgramInfo *pProgramInfo, UINT Command)
{
    if (Command == COMMAND_RESERVE && pProgramInfo) {
        // クリック位置の予約を取得
        const RESERVE *pRes = m_reserveList.Get(pProgramInfo->NetworkID, pProgramInfo->TransportStreamID,
                                                pProgramInfo->ServiceID, pProgramInfo->EventID);
        if (pRes) {
            // サービスの名前を取得
            TCHAR serviceName[64];
            if (!GetChannelName(serviceName, ARRAY_SIZE(serviceName), pRes->networkID, pRes->serviceID))
                serviceName[0] = 0;

            // 予約変更
            if (m_reserveList.Insert(g_hinstDLL, m_hwndProgramGuide, *pRes, m_defaultRecOption, serviceName)) {
                m_reserveList.Save();
                if (m_usesTask) m_reserveList.SaveTask(m_resumeMargin, m_execWait, m_szAppName, m_szDriverName, m_szCmdOption);
                ::InvalidateRect(m_hwndProgramGuide, NULL, TRUE);
            }
        }
        else {
            // 番組の情報を取得
            TVTest::EpgEventInfo *pEpgEventInfo = GetEventInfo(pProgramInfo);
            if (!pEpgEventInfo) return true;
            
            // サービスの名前を取得
            TCHAR serviceName[64];
            if (!GetChannelName(serviceName, ARRAY_SIZE(serviceName), pProgramInfo->NetworkID, pProgramInfo->ServiceID))
                serviceName[0] = 0;

            // 予約追加
            RESERVE res;
            res.networkID           = pProgramInfo->NetworkID;
            res.transportStreamID   = pProgramInfo->TransportStreamID;
            res.serviceID           = pProgramInfo->ServiceID;
            res.eventID             = pProgramInfo->EventID;
            res.duration            = pProgramInfo->Duration;
            res.recOption           = RecordingOption::DEFAULT;
            ::SystemTimeToFileTime(&pProgramInfo->StartTime, &res.startTime);
            ::lstrcpyn(res.eventName, pEpgEventInfo->pszEventName, ARRAY_SIZE(res.eventName));
            
            if (m_reserveList.Insert(g_hinstDLL, m_hwndProgramGuide, res, m_defaultRecOption, serviceName)) {
                m_reserveList.Save();
                if (m_usesTask) m_reserveList.SaveTask(m_resumeMargin, m_execWait, m_szAppName, m_szDriverName, m_szCmdOption);
                ::InvalidateRect(m_hwndProgramGuide, NULL, TRUE);
            }
        }
        return true;
    }
    else if (Command == COMMAND_QUERY && pProgramInfo) {
        // 番組の情報を取得
        TVTest::EpgEventInfo *pEpgEventInfo = GetEventInfo(pProgramInfo);
        if (!pEpgEventInfo) return true;
        
        // サービスの名前を取得
        TCHAR serviceName[64];
        if (!GetChannelName(serviceName, ARRAY_SIZE(serviceName), pProgramInfo->NetworkID, pProgramInfo->ServiceID))
            serviceName[0] = 0;

        // クエリ追加
        QUERY query;
        query.isEnabled         = true;
        query.networkID         = pProgramInfo->NetworkID;
        query.transportStreamID = pProgramInfo->TransportStreamID;
        query.serviceID         = pProgramInfo->ServiceID;
        query.nibble1           = pEpgEventInfo->ContentList ? pEpgEventInfo->ContentList->ContentNibbleLevel1 : 0xFF;
        query.nibble2           = pEpgEventInfo->ContentList ? pEpgEventInfo->ContentList->ContentNibbleLevel2 : 0xFF;
        for (int i = 0; i < 7; i++) query.daysOfWeek[i] = false;
        query.daysOfWeek[pProgramInfo->StartTime.wDayOfWeek] = true;
        query.start             = 0;
        query.duration          = 24 * 60 * 60;
        query.eventName[0]      = 0;
        query.reserveCount      = 0;
        query.recOption         = RecordingOption::DEFAULT;
        ::lstrcpyn(query.keyword, pEpgEventInfo->pszEventName, ARRAY_SIZE(query.keyword));

        int index = m_queryList.Insert(-1, g_hinstDLL, m_hwndProgramGuide, query, m_defaultRecOption, serviceName);
        if (index >= 0) {
            m_queryList.Save();
            // すぐにクエリチェックする(indexが存在していなくても大丈夫)
            m_checkQueryIndex = index;
            if (m_hwndRecording) ::PostMessage(m_hwndRecording, WM_TIMER, CHECK_QUERY_LIST_TIMER_ID, NULL);
        }
        return true;
    }
    else if (COMMAND_RESERVELIST <= Command && Command < COMMAND_RESERVELIST + MENULIST_MAX) {
        // 途中で予約が追加消滅した場合にindexがずれる可能性がある
        const RESERVE *pRes = m_reserveList.Get(Command - COMMAND_RESERVELIST);
        
        // サービスの名前を取得
        TCHAR serviceName[64];
        if (!pRes || !GetChannelName(serviceName, ARRAY_SIZE(serviceName), pRes->networkID, pRes->serviceID)) {
            serviceName[0] = 0;
        }
        if (pRes && m_reserveList.Insert(g_hinstDLL, m_hwndProgramGuide, *pRes, m_defaultRecOption, serviceName))
        {
            m_reserveList.Save();
            if (m_usesTask) m_reserveList.SaveTask(m_resumeMargin, m_execWait, m_szAppName, m_szDriverName, m_szCmdOption);
            ::InvalidateRect(m_hwndProgramGuide, NULL, TRUE);
        }
        return true;
    }
    else if (COMMAND_QUERYLIST <= Command && Command < COMMAND_QUERYLIST + MENULIST_MAX) {
        // サービスの名前を取得
        const QUERY *pQuery = m_queryList.Get(Command - COMMAND_QUERYLIST);
        TCHAR serviceName[64];
        if (!pQuery || !GetChannelName(serviceName, ARRAY_SIZE(serviceName), pQuery->networkID, pQuery->serviceID)) {
            serviceName[0] = 0;
        }

        int index = m_queryList.Insert(Command - COMMAND_QUERYLIST, g_hinstDLL, m_hwndProgramGuide,
                                       *m_queryList.Get(Command - COMMAND_QUERYLIST), m_defaultRecOption, serviceName);
        if (index >= 0) {
            m_queryList.Save();
            // すぐにクエリチェックする(indexが存在していなくても大丈夫)
            m_checkQueryIndex = index;
            if (m_hwndRecording) ::PostMessage(m_hwndRecording, WM_TIMER, CHECK_QUERY_LIST_TIMER_ID, NULL);
        }
        return true;
    }
    return false;
}


// プラグインの設定を行う
bool CTTRec::PluginSettings(HWND hwndOwner)
{
    LoadSettings();
    
    if (::DialogBoxParam(g_hinstDLL, MAKEINTRESOURCE(IDD_OPTIONS), hwndOwner,
        SettingsDlgProc, reinterpret_cast<LPARAM>(this)) != IDOK) return false;

    SaveSettings();
    if (m_fInitialized && m_usesTask) m_reserveList.SaveTask(m_resumeMargin, m_execWait, m_szAppName, m_szDriverName, m_szCmdOption);
    return true;
}


// 設定ダイアログプロシージャ
INT_PTR CALLBACK CTTRec::SettingsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static const int TIMER_ID = 1;
    switch (uMsg) {
    case WM_INITDIALOG:
        {
            ::SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            CTTRec *pThis = reinterpret_cast<CTTRec*>(lParam);

            TCHAR driverName[MAX_PATH];
            for (int i = 0; pThis->m_pApp->EnumDriver(i, driverName, ARRAY_SIZE(driverName)) != 0; i++) {
                ::SendDlgItemMessage(hDlg, IDC_COMBO_DRIVER_NAME, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(driverName));
            }
            ::SetDlgItemText(hDlg, IDC_COMBO_DRIVER_NAME, pThis->m_szDriverName);
            ::SendDlgItemMessage(hDlg, IDC_COMBO_DRIVER_NAME, EM_LIMITTEXT, ARRAY_SIZE(pThis->m_szDriverName) - 1, 0);
            
            TCHAR totList[TOT_ADJUST_MAX_MAX][32];
            LPCTSTR pTotList[TOT_ADJUST_MAX_MAX] = { TEXT("しない") };
            for (int i = 1; i < ARRAY_SIZE(pTotList); i++) {
                ::wsprintf(totList[i], TEXT("±%d分まで"), i);
                pTotList[i] = totList[i];
            }
            SetComboBoxList(hDlg, IDC_COMBO_TOT, pTotList, ARRAY_SIZE(pTotList));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_TOT, CB_SETCURSEL, pThis->m_totAdjustMax, 0);

            if (pThis->m_usesTask) {
                ::CheckDlgButton(hDlg, IDC_CHECK_USE_TASK, BST_CHECKED);
                ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_RSM_M), TRUE);
                ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_OPTION), TRUE);
            }
            ::SetDlgItemInt(hDlg, IDC_EDIT_RSM_M, pThis->m_resumeMargin, FALSE);
            ::SetDlgItemText(hDlg, IDC_EDIT_OPTION, pThis->m_szCmdOption);
            ::SendDlgItemMessage(hDlg, IDC_EDIT_OPTION, EM_LIMITTEXT, ARRAY_SIZE(pThis->m_szCmdOption) - 1, 0);
            ::CheckDlgButton(hDlg, IDC_CHECK_JOIN_EVENTS, pThis->m_joinsEvents ? BST_CHECKED : BST_UNCHECKED);
            ::SetDlgItemInt(hDlg, IDC_EDIT_CH_CHANGE, pThis->m_chChangeBefore, FALSE);
            ::SetDlgItemInt(hDlg, IDC_EDIT_SPIN_UP, pThis->m_spinUpBefore, FALSE);
            
            if (pThis->m_pApp->IsPluginEnabled()) ::SetTimer(hDlg, TIMER_ID, 500, NULL);
            
            return RecordingOption::DlgProc(hDlg, uMsg, wParam, &pThis->m_defaultRecOption, false);
        }
    case WM_TIMER:
        {
            CTTRec *pThis = reinterpret_cast<CTTRec*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
            FILETIME totNow = pThis->m_totAdjustedNow;
            totNow += (::GetTickCount() - pThis->m_totAdjustedTick) * FILETIME_MILLISECOND;
            SYSTEMTIME totSysTime;
            ::FileTimeToSystemTime(&totNow, &totSysTime);
            TCHAR text[256];
            ::wsprintf(text, TEXT("%02hu:%02hu:%02hu"), totSysTime.wHour, totSysTime.wMinute, totSysTime.wSecond);
            ::SetDlgItemText(hDlg, IDC_STATIC_TOT, text);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHECK_USE_TASK:
            {
                BOOL isChecked = ::IsDlgButtonChecked(hDlg, IDC_CHECK_USE_TASK) == BST_CHECKED;
                ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_RSM_M), isChecked);
                ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_OPTION), isChecked);
            }
            return TRUE;
        case IDOK:
            {
                CTTRec *pThis = reinterpret_cast<CTTRec*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
                
                TCHAR driverName[MAX_PATH];
                ::GetDlgItemText(hDlg, IDC_COMBO_DRIVER_NAME, driverName, ARRAY_SIZE(driverName) - 1);
                if (driverName[0]) ::lstrcpy(pThis->m_szDriverName, driverName);

                pThis->m_totAdjustMax = ::SendDlgItemMessage(hDlg, IDC_COMBO_TOT, CB_GETCURSEL, 0, 0);
                pThis->m_usesTask = ::IsDlgButtonChecked(hDlg, IDC_CHECK_USE_TASK) == BST_CHECKED;
                pThis->m_resumeMargin = ::GetDlgItemInt(hDlg, IDC_EDIT_RSM_M, NULL, FALSE);
                ::GetDlgItemText(hDlg, IDC_EDIT_OPTION, pThis->m_szCmdOption, ARRAY_SIZE(pThis->m_szCmdOption) - 1);
                pThis->m_joinsEvents = ::IsDlgButtonChecked(hDlg, IDC_CHECK_JOIN_EVENTS) == BST_CHECKED;

                pThis->m_chChangeBefore = ::GetDlgItemInt(hDlg, IDC_EDIT_CH_CHANGE, NULL, FALSE);
                if (pThis->m_chChangeBefore <= 0) pThis->m_chChangeBefore = 0;
                else if (pThis->m_chChangeBefore < 15) pThis->m_chChangeBefore = 15;

                pThis->m_spinUpBefore = ::GetDlgItemInt(hDlg, IDC_EDIT_SPIN_UP, NULL, FALSE);
                if (pThis->m_spinUpBefore <= 0) pThis->m_spinUpBefore = 0;
                else if (pThis->m_spinUpBefore < 15) pThis->m_spinUpBefore = 15;

                RecordingOption::DlgProc(hDlg, uMsg, wParam, &pThis->m_defaultRecOption, false);
            }
            // fall through!
        case IDCANCEL:
            ::KillTimer(hDlg, TIMER_ID);
            ::EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        default:
            {
                CTTRec *pThis = reinterpret_cast<CTTRec*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
                return RecordingOption::DlgProc(hDlg, uMsg, wParam, &pThis->m_defaultRecOption, false);
            }
        }
        break;
    }
    return FALSE;
}


bool CTTRec::IsEventMatch(const TVTest::EpgEventInfo &ev, const QUERY &q)
{
    // 曜日ではじく(early reject)
    if (!q.daysOfWeek[ev.StartTime.wDayOfWeek] &&
        !q.daysOfWeek[(ev.StartTime.wDayOfWeek + 6) % 7]) return false;
        
    // ジャンルではじく
    if (q.nibble1 != 0xFF) {
        bool fFound = false;
        for (int j = 0; j < ev.ContentListLength; j++) {
            if (ev.ContentList[j].ContentNibbleLevel1 == q.nibble1) {
                if (q.nibble2 == 0xFF || ev.ContentList[j].ContentNibbleLevel2 == q.nibble2) {
                    fFound = true;
                    break;
                }
            }
        }
        if (!fFound) return false;
    }

    int evStart = (ev.StartTime.wHour * 60 + ev.StartTime.wMinute) * 60 + ev.StartTime.wSecond;

    // 探索時間ではじく
    if (!(
        q.daysOfWeek[ev.StartTime.wDayOfWeek] && q.start <= evStart && evStart < q.start + q.duration ||
        q.daysOfWeek[(ev.StartTime.wDayOfWeek + 6) % 7] && evStart < q.start + q.duration - 24 * 60 * 60
        )) return false;

    // キーワードではじく
    if (!IsMatch(ev.pszEventName, q.keyword)) return false;

    return true;
}


// クエリにマッチする番組情報を探して予約に加える
void CTTRec::CheckQuery()
{
    // 負荷分散のためクエリチェックは1つずつ行う
    const QUERY *pQuery = m_queryList.Get(m_checkQueryIndex);
    int queryIndex = m_checkQueryIndex;

    if (++m_checkQueryIndex >= m_queryList.Length()) m_checkQueryIndex = 0;
    if (!pQuery || !pQuery->isEnabled) return;

    TVTest::EpgEventList eventList;
    eventList.NetworkID         = pQuery->networkID;
    eventList.TransportStreamID = pQuery->transportStreamID;
    eventList.ServiceID         = pQuery->serviceID;
    if (!m_pApp->GetEpgEventList(&eventList)) return;
    
    FILETIME now;
    GetLocalTimeAsFileTime(&now);
    bool fUpdated = false;
    
    // すでに開始しているイベントをスキップする
    int i = 0;
    for (; i < eventList.NumEvents; i++) {
        const TVTest::EpgEventInfo &ev = *eventList.EventList[i];
        FILETIME evStart;
        if (::SystemTimeToFileTime(&ev.StartTime, &evStart) && evStart - now > 0) break;
    }

    for (; i < eventList.NumEvents; i++) {
        const TVTest::EpgEventInfo &ev = *eventList.EventList[i];
        // イベントが条件にマッチするか
        // イベントがすでに予約されていないか
        if (!IsEventMatch(ev, *pQuery) ||
            m_reserveList.Get(pQuery->networkID, pQuery->transportStreamID,
                              pQuery->serviceID, ev.EventID)) continue;
            
        // クエリから予約を生成する
        RESERVE res;
        FILETIME evStart;
        if (::SystemTimeToFileTime(&ev.StartTime, &evStart) &&
            m_queryList.CreateReserve(queryIndex, res, ev.EventID, ev.pszEventName, evStart, ev.Duration) &&
            m_reserveList.Insert(res)) fUpdated = true;
    }
    m_pApp->FreeEpgEventList(&eventList);
    
    if (fUpdated) {
        m_queryList.Save();
        m_reserveList.Save();
        if (m_usesTask) m_reserveList.SaveTask(m_resumeMargin, m_execWait, m_szAppName, m_szDriverName, m_szCmdOption);
        if (m_hwndProgramGuide) ::InvalidateRect(m_hwndProgramGuide, NULL, TRUE);
        m_pApp->AddLog(L"クエリから新しい予約が生成されました。");
    }
}


// 予約を追従する
void CTTRec::FollowUpReserves()
{
    bool fUpdated = false;
    for (int i = 0; i < FOLLOW_UP_MAX; i++) {
        const RESERVE *pRes = m_reserveList.Get(i);
        if (!pRes) break;

        TVTest::EpgEventQueryInfo queryInfo;
        queryInfo.NetworkID         = pRes->networkID;
        queryInfo.TransportStreamID = pRes->transportStreamID;
        queryInfo.ServiceID         = pRes->serviceID;
        queryInfo.EventID           = pRes->eventID;
        queryInfo.Type              = TVTest::EPG_EVENT_QUERY_EVENTID;
        queryInfo.Flags             = 0;
        TVTest::EpgEventInfo *pEvent = m_pApp->GetEpgEventInfo(&queryInfo);
        if (!pEvent) continue;

        FILETIME startTime;
        ::SystemTimeToFileTime(&pEvent->StartTime, &startTime);

        // 予約時刻に変更あり
        // TODO: 必要以上の追従をしない
        if (startTime - pRes->startTime != 0 || pEvent->Duration - pRes->duration != 0) {
            RESERVE newRes = *pRes;
            newRes.startTime = startTime;
            newRes.duration = pEvent->Duration;
            if (m_reserveList.Insert(newRes)) fUpdated = true;
        }
        m_pApp->FreeEpgEventInfo(pEvent);
    }

    if (fUpdated) {
        m_reserveList.Save();
        if (m_usesTask) m_reserveList.SaveTask(m_resumeMargin, m_execWait, m_szAppName, m_szDriverName, m_szCmdOption);
        if (m_hwndProgramGuide) ::InvalidateRect(m_hwndProgramGuide, NULL, TRUE);
        m_pApp->AddLog(L"予約時刻に変更がありました。");
    }
}


// チャンネルを取得する
bool CTTRec::GetChannel(int *pSpace, int *pChannel, WORD networkID, WORD serviceID)
{
    if (!pSpace || !pChannel) return false;

    // networkIDからチューニング空間の種類を推測する
    // http://www.arib.or.jp/tyosakenkyu/sakutei/img/sakutei3-07.pdf
    int spaceType;
    if (networkID == 0x0004) spaceType = TVTest::TUNINGSPACE_BS;
    else if (0x0001 <= networkID && networkID <= 0x000A) spaceType = TVTest::TUNINGSPACE_110CS;
    else if (0x7880 <= networkID && networkID <= 0x7FEF) spaceType = TVTest::TUNINGSPACE_TERRESTRIAL;
    else return false;

    // spaceTypeの一致するチューニング空間を探す
    TVTest::TuningSpaceInfo spaceInfo;
    for (*pSpace = 0; m_pApp->GetTuningSpaceInfo(*pSpace, &spaceInfo); (*pSpace)++) {
        if (spaceInfo.Space == spaceType) {
            // serviceIDの一致するチャンネルを探す
            TVTest::ChannelInfo channelInfo;
            for (*pChannel = 0; m_pApp->GetChannelInfo(*pSpace, *pChannel, &channelInfo); (*pChannel)++)
                if (channelInfo.ServiceID == serviceID) return true;
        }
    }
    return false;
}


// チャンネルの名前を取得する
bool CTTRec::GetChannelName(LPTSTR name, int max, WORD networkID, WORD serviceID)
{
    int space, channel;
    if (!GetChannel(&space, &channel, networkID, serviceID)) return false;

    TVTest::ChannelInfo chInfo;
    chInfo.Size = sizeof(chInfo);
    if (!m_pApp->GetChannelInfo(space, channel, &chInfo)) return false;
    
    ::lstrcpyn(name, chInfo.szChannelName, max);
    return true;
}


// チャンネルを変更する
bool CTTRec::SetChannel(WORD networkID, WORD serviceID)
{
    // 必要な場合、ドライバを変更する
    TCHAR driverName[MAX_PATH];
    m_pApp->GetDriverName(driverName, ARRAY_SIZE(driverName));
    if (::lstrcmpi(::PathFindFileName(driverName), ::PathFindFileName(m_szDriverName))) {
        m_pApp->AddLog(TEXT("BonDriverを変更します。"));
        m_pApp->SetDriverName(m_szDriverName);
    }

    int space, channel;
    if (!GetChannel(&space, &channel, networkID, serviceID)) return false;

    return m_pApp->SetChannel(space, channel, serviceID);
}


// 録画を開始する
bool CTTRec::StartRecord(LPCTSTR saveDir, LPCTSTR saveName)
{
    TCHAR fullPath[MAX_PATH];
    ::PathCombine(fullPath, saveDir, saveName);
    
    TVTest::RecordInfo recordInfo;
    recordInfo.Mask = TVTest::RECORD_MASK_FILENAME;
    recordInfo.Flags = 0;
    recordInfo.StartTimeSpec = TVTest::RECORD_START_NOTSPECIFIED;
    recordInfo.StopTimeSpec = TVTest::RECORD_STOP_NOTSPECIFIED;
    recordInfo.pszFileName = fullPath;

    return m_pApp->StartRecord(&recordInfo);
}


// 録画停止中かどうか調べる
bool CTTRec::IsNotRecording()
{
    TVTest::RecordStatusInfo recInfo;
    return m_pApp->GetRecordStatus(&recInfo) &&
           recInfo.Status == TVTest::RECORD_STATUS_NOTRECORDING;
}


// 録画を制御する
void CTTRec::CheckRecording()
{
    WORD prevNetworkID = m_nearest.networkID;
    WORD prevTransportStreamID = m_nearest.transportStreamID;
    WORD prevServiceID = m_nearest.serviceID;
    WORD prevEventID = m_nearest.eventID;

    // 直近の予約とその開始までのオフセットを取得する
    LONGLONG startOffset;
    for (;;) {
        FILETIME &now = m_totAdjustedNow;
        // 追従処理等により予約は入れ替わることがある
        if (!m_reserveList.GetNearest(&m_nearest, m_defaultRecOption, REC_READY_OFFSET - 2)) {
            // 予約がない
            m_nearest.eventID = 0xFFFF;
            startOffset = LLONG_MAX;
            break;
        }
        else if (now - m_nearest.startTime < (m_nearest.duration + m_nearest.recOption.endMargin) * FILETIME_SECOND) {
            // 予約開始前か予約時間内
            startOffset = m_nearest.startTime - now - m_nearest.recOption.startMargin * FILETIME_SECOND;
            break;
        }
        else {
            // 予約時間を過ぎた
            m_reserveList.DeleteNearest(m_defaultRecOption);
            m_reserveList.Save();
            if (m_usesTask) m_reserveList.SaveTask(m_resumeMargin, m_execWait, m_szAppName, m_szDriverName, m_szCmdOption);
        }
    }
    // 予約サービスが変化したか
    bool fServiceChanged = prevNetworkID != m_nearest.networkID ||
                           prevTransportStreamID != m_nearest.transportStreamID ||
                           prevServiceID != m_nearest.serviceID;
    // 予約イベントが変化したか
    bool fEventChanged = fServiceChanged || prevEventID != m_nearest.eventID;

    if (fEventChanged && m_hwndProgramGuide) ::InvalidateRect(m_hwndProgramGuide, NULL, TRUE);

    // スリープを防ぐ
    if (startOffset < (m_suspendMargin + m_resumeMargin) * FILETIME_MINUTE)
        ::SetThreadExecutionState(ES_SYSTEM_REQUIRED);

    switch (m_recordingState) {
        case REC_IDLE:
            if (startOffset < REC_READY_OFFSET * FILETIME_SECOND) {
                // 予約待機時刻～
                m_recordingState = REC_READY;
                if (IsNotRecording()) SetChannel(m_nearest.networkID, m_nearest.serviceID);
                break;
            }
            // チャンネル変更
            if (startOffset < m_chChangeBefore * FILETIME_SECOND) {
                if (!m_fChChanged && IsNotRecording()) {
                    SetChannel(m_nearest.networkID, m_nearest.serviceID);
                    m_fChChanged = true;
                }
            }
            else m_fChChanged = false;
            // スピンアップ
            if (startOffset < m_spinUpBefore * FILETIME_SECOND) {
                if (!m_fSpunUp) {
                    WriteFileForSpinUp(m_nearest.recOption.saveDir);
                    m_fSpunUp = true;
                }
            }
            else m_fSpunUp = false;
            break;
        case REC_READY:
            if (startOffset < CHECK_RECORDING_INTERVAL * FILETIME_MILLISECOND && IsNotRecording()) {
                // 予約開始直前～かつ録画停止中
                // 録画開始
                m_recordingState = REC_ACTIVE;
                m_onStopped = m_nearest.recOption.onStopped;
                SetChannel(m_nearest.networkID, m_nearest.serviceID);
                // フォーマット指示子を"部分的に"置換
                TCHAR replacedName[MAX_PATH];
                FormatFileName(replacedName, ARRAY_SIZE(replacedName), m_nearest.eventID,
                               m_nearest.startTime, m_nearest.eventName, m_nearest.recOption.saveName);
                StartRecord(m_nearest.recOption.saveDir, replacedName);
            }
            else if (startOffset > REC_READY_OFFSET * FILETIME_SECOND) {
                // 予約待機時刻より前
                m_recordingState = REC_IDLE;
            }
            break;
        case REC_ACTIVE:
            if (m_joinsEvents && fEventChanged && !fServiceChanged &&
                startOffset < REC_READY_OFFSET * FILETIME_SECOND) {
                // イベントが変化したがサービスが同じで、かつ予約待機時刻を過ぎている
                // 連結録画(状態遷移しない)
                m_onStopped = m_nearest.recOption.onStopped;
            }
            else if (fEventChanged || startOffset > REC_READY_OFFSET * FILETIME_SECOND) {
                // 予約イベントが変わったか予約待機時刻より前
                m_recordingState = REC_STOPPED;
                m_pApp->StopRecord();
            }
            else if (m_fStopRecording) {
                // ユーザ操作により録画が停止した
                m_recordingState = REC_STOPPED;
                // 予約を削除
                m_reserveList.DeleteNearest(m_defaultRecOption);
                m_reserveList.Save();
                if (m_usesTask) m_reserveList.SaveTask(m_resumeMargin, m_execWait, m_szAppName, m_szDriverName, m_szCmdOption);
                m_fStopRecording = false;
            }
            break;
        case REC_STOPPED:
            m_recordingState = REC_IDLE;
            if (startOffset > (m_suspendMargin + m_resumeMargin) * FILETIME_MINUTE) OnStopped();
            break;
        default:
            m_recordingState = REC_IDLE;
            break;
    }
}


// 録画終了後の動作を行う
void CTTRec::OnStopped()
{
    if (m_onStopped != ON_STOPPED_CLOSE &&
        m_onStopped != ON_STOPPED_SUSPEND &&
        m_onStopped != ON_STOPPED_HIBERNATE) return;

    // 確認のダイアログを表示
    if (::DialogBoxParam(g_hinstDLL, MAKEINTRESOURCE(IDD_ONSTOP), m_pApp->GetAppWindow(),
        OnStoppedDlgProc, MAKELONG(ON_STOPPED_DLG_TIMEOUT, m_onStopped - 2)) != IDOK) return;

    if (m_onStopped == ON_STOPPED_SUSPEND || m_onStopped == ON_STOPPED_HIBERNATE) {
        TCHAR shortFileName[MAX_PATH];
        DWORD rv = ::GetShortPathName(m_szPluginFileName, shortFileName, MAX_PATH);
        if (rv == 0 || rv >= MAX_PATH) return;

        TCHAR rundllPath[MAX_PATH];
        if (!GetRundll32Path(rundllPath)) return;
        
        TCHAR cmdOption[MAX_PATH];
        ::wsprintf(cmdOption, TEXT(" %s,DelayedSuspend %d %s%s"), shortFileName, m_suspendWait,
                   m_onStopped == ON_STOPPED_SUSPEND ? TEXT("S") : TEXT("H"),
                   m_fForceSuspend ? TEXT("F") : TEXT(""));
        
        // スリープ用のプロセスを起動
        STARTUPINFO si;
        PROCESS_INFORMATION ps;
        ::GetStartupInfo(&si);
        ::CreateProcess(rundllPath, cmdOption, NULL, NULL, FALSE, 0, NULL, NULL, &si, &ps);
    }

    m_pApp->Close();
}


// 終了確認ダイアログプロシージャ
INT_PTR CALLBACK CTTRec::OnStoppedDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static const int TIMER_ID = 1;
    LPCTSTR message = TEXT("%2d秒後にTVTestを終了%sします");
    LPCTSTR mode[] = { TEXT(""), TEXT("してシステムをサスペンド"), TEXT("してシステムを休止状態に") };

    switch (uMsg) {
        case WM_INITDIALOG:
            {
                int modeIdx = HIWORD(lParam);
                int count = LOWORD(lParam);
                ::SetWindowLongPtr(hDlg, GWLP_USERDATA, MAKELONG(count, modeIdx));

                TCHAR text[256];
                ::wsprintf(text, message, count, mode[modeIdx]);
                ::SetDlgItemText(hDlg, IDC_STATIC_ONSTOP, text);
                ::SendDlgItemMessage(hDlg, IDC_PROGRESS_ONSTOP, PBM_SETRANGE, 0, MAKELONG(0, count - 1));
                ::SetTimer(hDlg, TIMER_ID, 1000, NULL);
            }
            return TRUE;
        case WM_TIMER:
            {
                LONG data = ::GetWindowLongPtr(hDlg, GWLP_USERDATA);
                int modeIdx = HIWORD(data);
                int count = LOWORD(data) - 1;
                ::SetWindowLongPtr(hDlg, GWLP_USERDATA, MAKELONG(count, modeIdx));

                TCHAR text[256];
                ::wsprintf(text, message, count, mode[modeIdx]);
                ::SetDlgItemText(hDlg, IDC_STATIC_ONSTOP, text);
                ::SendDlgItemMessage(hDlg, IDC_PROGRESS_ONSTOP, PBM_DELTAPOS, 1, 0);
                if (count <= 0) ::EndDialog(hDlg, IDOK);
            }
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL) ::EndDialog(hDlg, IDCANCEL);
            return TRUE;
        case WM_DESTROY:
            ::KillTimer(hDlg, TIMER_ID);
            return TRUE;
    }
    return FALSE;
}


// 録画制御用のウィンドウプロシージャ
LRESULT CALLBACK CTTRec::RecordingWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CREATE:
        {
            LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            CTTRec *pThis = static_cast<CTTRec*>(pcs->lpCreateParams);
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));

            ::SetTimer(hwnd, FOLLOW_UP_TIMER_ID, CHECK_PROGRAMLIST_INTERVAL, NULL);
            ::SetTimer(hwnd, CHECK_QUERY_LIST_TIMER_ID, CHECK_PROGRAMLIST_INTERVAL / (pThis->m_queryList.Length() + 1), NULL);
            ::SetTimer(hwnd, CHECK_RECORDING_TIMER_ID, CHECK_RECORDING_INTERVAL, NULL);
        }
        return 0;
    case WM_TIMER:
        {
            CTTRec *pThis = reinterpret_cast<CTTRec*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
            switch (wParam) {
            case FOLLOW_UP_TIMER_ID:
                pThis->FollowUpReserves();
                break;
            case CHECK_QUERY_LIST_TIMER_ID:
                pThis->CheckQuery();
                break;
            case CHECK_RECORDING_TIMER_ID:
                // 必ずCheckRecording()の直前に呼び出す
                pThis->UpdateTotAdjust();
                pThis->CheckRecording();
                break;
            }
        }
        return 0;
    case WM_DESTROY:
        ::KillTimer(hwnd, FOLLOW_UP_TIMER_ID);
        ::KillTimer(hwnd, CHECK_QUERY_LIST_TIMER_ID);
        ::KillTimer(hwnd, CHECK_RECORDING_TIMER_ID);
        return 0;
    }
    return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
}


void CTTRec::InitializeTotAdjust()
{
    GetLocalTimeAsFileTime(&m_totAdjustedNow);
    m_totAdjustedTick = ::GetTickCount();
    m_totIsValid = false;
}


void CTTRec::UpdateTotAdjust()
{
    FILETIME localNow;
    GetLocalTimeAsFileTime(&localNow);
    DWORD tick = ::GetTickCount();
    
    // TOT補正しない場合
    if (m_totAdjustMax <= 0) {
        m_totAdjustedNow = localNow;
        m_totAdjustedTick = tick;
        return;
    }
    m_totAdjustedNow += (tick - m_totAdjustedTick) * FILETIME_MILLISECOND;
    m_totAdjustedTick = tick;

    // 指定ドライバのTOTだけ使う
    TCHAR driverName[MAX_PATH];
    m_pApp->GetDriverName(driverName, ARRAY_SIZE(driverName));
    bool fDriver = ::lstrcmpi(::PathFindFileName(driverName), ::PathFindFileName(m_szDriverName)) == 0;
    
    ::EnterCriticalSection(&m_totSection);
    DWORD diff = tick - m_totGrabbedTick;
    // 有効なTOT時刻がタイムアウト以内に取得できているか
    m_totIsValid = m_totIsValid && fDriver && diff < TOT_GRAB_TIMEOUT;
    LONGLONG adjustDiff = !m_totIsValid ? localNow - m_totAdjustedNow/*ローカル方向に補正*/:
                          m_totGrabbedTime - m_totAdjustedNow + diff * FILETIME_MILLISECOND/*TOT方向に補正*/;
    ::LeaveCriticalSection(&m_totSection);
    
    // メソッド呼び出しのたびに、進める方向に最大4秒、遅らせる方向に最大1秒、それぞれ補正する
    // 進める方向にはより速く補正する(PC内部時計は遅れる場合が多いのと、遅れは録画失敗につながる場合が多いため)
    m_totAdjustedNow += -FILETIME_SECOND < adjustDiff && adjustDiff < 4 * FILETIME_SECOND ? adjustDiff :
                            adjustDiff > 0 ? 4 * FILETIME_SECOND : -FILETIME_SECOND;
    
    // m_totAdjustMax分以上補正されることはない
    if (m_totAdjustedNow - localNow > m_totAdjustMax * FILETIME_MINUTE) {
        m_totAdjustedNow = localNow;
        m_totAdjustedNow += m_totAdjustMax * FILETIME_MINUTE;
    }
    else if (localNow - m_totAdjustedNow > m_totAdjustMax * FILETIME_MINUTE) {
        m_totAdjustedNow = localNow;
        m_totAdjustedNow += -m_totAdjustMax * FILETIME_MINUTE;
    }
#ifdef _DEBUG
    if (adjustDiff < -FILETIME_SECOND || FILETIME_SECOND < adjustDiff) {
        // 大きく補正されている間はログに出す
        TCHAR szLog[256];
        ::wsprintf(szLog, TEXT("TOT補正中: 目標との差=%dmsec Local時間との差=%dmsec"),
                   (int)(adjustDiff / FILETIME_MILLISECOND),
                   (int)((m_totAdjustedNow - localNow) / FILETIME_MILLISECOND));
        m_pApp->AddLog(szLog);
    }
#endif
}


// TOT時刻を取得するストリームコールバック(別スレッド)
BOOL CALLBACK CTTRec::StreamCallback(BYTE *pData, void *pClientData)
{
    WORD pid = ((WORD)pData[1] & 0x1F) << 8 | pData[2];
    if (pid != 0x14) return TRUE;
    
    BYTE *pTable = &pData[5];
    BYTE tableID = pTable[0];
    // TOT or TDT (ARIB STD-B10)
    if (tableID != 0x73 && tableID != 0x70) return TRUE;
    
    // TOTパケットは地上波の実測で6秒に1個程度
    // ARIB規格では最低30秒に1個

    CTTRec *pThis = reinterpret_cast<CTTRec*>(pClientData);

    // TOT時刻とTickカウントを記録する
    ::EnterCriticalSection(&pThis->m_totSection);
    SYSTEMTIME totSysTime;
    if (AribToSystemTime(&pTable[3], &totSysTime) &&
        ::SystemTimeToFileTime(&totSysTime, &pThis->m_totGrabbedTime))
    {
        // バッファがあるので少し時刻を戻す(TVTest_0.7.19r2_Src/TVTest.cpp参考)
        pThis->m_totGrabbedTime += -2000 * FILETIME_MILLISECOND;
        pThis->m_totGrabbedTick = ::GetTickCount();
        pThis->m_totIsValid = true;
    }
    ::LeaveCriticalSection(&pThis->m_totSection);

    return TRUE;
}


TVTest::CTVTestPlugin *CreatePluginClass()
{
    return new CTTRec;
}
