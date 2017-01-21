// TVTestの予約録画機能を拡張するプラグイン
// NO_CRT(CRT非依存)でx86ビルドするときはlldiv.asm,llmul.asm(,mm.inc,cruntime.inc)も必要
// 最終更新: 2013-06-27
// 署名: 9a5ad966ee38e172c4b5766a2bb71fea
#include <Windows.h>
#include <Shlwapi.h>
#include <CommCtrl.h>
#include "resource.h"
#include "Util.h"
#include "RecordingOption.h"
#include "ReserveList.h"
#include "QueryList.h"
#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#define TVTEST_PLUGIN_VERSION TVTEST_PLUGIN_VERSION_(0,0,13)
#include "TVTestPlugin.h"
#include "TTRec.h"

#ifndef ASSERT
#include <cassert>
#define ASSERT assert
#endif

static const LPCTSTR INFO_PLUGIN_NAME = TEXT("TTRec");
static const LPCTSTR INFO_DESCRIPTION = TEXT("予約録画機能を拡張 (ver.1.0)");
static const LPCTSTR TTREC_WINDOW_CLASS = TEXT("TVTest TTRec");
static const LPCTSTR DEFAULT_PLUGIN_NAME = TEXT("TTRec.tvtp");

#define WM_RUN_SAVE_TASK_DONE   (WM_APP + 1)
#define WM_NOTIFY_ICON          (WM_APP + 2)

const TVTest::ProgramGuideCommandInfo CTTRec::PROGRAM_GUIDE_COMMAND_LIST[] = {
    { TVTest::PROGRAMGUIDE_COMMAND_TYPE_PROGRAM, 0, COMMAND_RESERVE, L"Reserve", L"TTRec-予約設定" },
    { TVTest::PROGRAMGUIDE_COMMAND_TYPE_PROGRAM, 0, COMMAND_QUERY, L"Query", L"TTRec-クエリ設定" },
    { TVTest::PROGRAMGUIDE_COMMAND_TYPE_PROGRAM, 0, COMMAND_RESERVE_DEFAULT, L"ReserveDefault", L"TTRec-デフォルト予約登録/設定" },
    { TVTest::PROGRAMGUIDE_COMMAND_TYPE_PROGRAM, 0, COMMAND_RESERVE_DEFAULT_OR_DELETE, L"ReserveDefaultOrDelete", L"TTRec-デフォルト予約登録/削除" },
};

CTTRec::CTTRec()
    : m_hMutex(NULL)
    , m_hModuleMutex(NULL)
    , m_fInitialized(false)
    , m_fSettingsLoaded(false)
    , m_hwndProgramGuide(NULL)
    , m_fVistaOrLater(false)
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
    , m_fDoSetPreview(false)
    , m_fDoSetPreviewNoViewOnly(false)
    , m_fShowDlgOnAppSuspend(false)
    , m_fShowNotifyIcon(false)
    , m_appSuspendTimeout(0)
    , m_notifyLevel(0)
    , m_logLevel(0)
    , m_normalColor(RGB(0,0,0))
    , m_nearestColor(RGB(0,0,0))
    , m_recColor(RGB(0,0,0))
    , m_priorityColor(RGB(0,0,0))
    , m_hwndRecording(NULL)
    , m_recordingState(REC_IDLE)
    , m_onStopped(ON_STOPPED_NONE)
    , m_checkQueryIndex(0)
    , m_followUpIndex(FOLLOW_UP_MAX)
    , m_fChChanged(false)
    , m_fSpunUp(false)
    , m_fStopRecording(false)
    , m_fOnStoppedPostponed(false)
    , m_epgCapTimeout(0)
    , m_epgCapSpace(-1)
    , m_epgCapChannel(0)
    , m_prevExecState(0)
    , m_totIsValid(false)
    , m_totGrabbedTick(0)
    , m_totAdjustedTick(0)
{
    m_szIniFileName[0] = 0;
    m_szCaptionSuffix[0] = 0;
    m_szDriverName[0] = 0;
    m_szCmdOption[0] = 0;
    m_defaultRecOption.SetEmpty(false);
    m_szExecOnStartRec[0] = 0;
    m_szExecOnEndRec[0] = 0;
    m_nearest.networkID = m_nearest.transportStreamID =
        m_nearest.serviceID = m_nearest.eventID = 0xFFFF;
    m_recordingInfo.fEnabled = false;
}


bool CTTRec::GetPluginInfo(TVTest::PluginInfo *pInfo)
{
    // プラグインの情報を返す
    pInfo->Type           = TVTest::PLUGIN_TYPE_NORMAL;
    pInfo->Flags          = TVTest::PLUGIN_FLAG_HASSETTINGS | TVTest::PLUGIN_FLAG_DISABLEONSTART;
    pInfo->pszPluginName  = INFO_PLUGIN_NAME;
    pInfo->pszCopyright   = L"Public Domain";
    pInfo->pszDescription = INFO_DESCRIPTION;
    return true;
}


// 初期化処理
bool CTTRec::Initialize()
{
    // 番組表のイベントの通知を有効にする(m_hwndProgramGuideを取得し続けるため)
    m_pApp->EnableProgramGuideEvent(TVTest::PROGRAMGUIDE_EVENT_GENERAL);

    // イベントコールバック関数を登録
    m_pApp->SetEventCallback(EventCallback, this);

    // プラグイン名が変更されていればキャプションを修飾する
    TCHAR name[MAX_PATH];
    if (GetLongModuleFileName(g_hinstDLL, name, ARRAY_SIZE(name)) &&
        ::lstrcmpi(::PathFindFileName(name), DEFAULT_PLUGIN_NAME))
    {
        ::lstrcpy(m_szCaptionSuffix, TEXT(" ("));
        ::lstrcpyn(m_szCaptionSuffix + 2, ::PathFindFileName(name), ARRAY_SIZE(m_szCaptionSuffix) - 3);
        ::lstrcat(m_szCaptionSuffix, TEXT(")"));
    }
    OSVERSIONINFO vi;
    vi.dwOSVersionInfoSize = sizeof(vi);
    m_fVistaOrLater = ::GetVersionEx(&vi) && vi.dwMajorVersion >= 6;
    return true;
}


// 終了処理
bool CTTRec::Finalize()
{
    if (m_pApp->IsPluginEnabled()) EnablePlugin(false, true);

    if (m_recordingInfo.fEnabled && m_recordingInfo.pEpgEventInfo) {
        m_pApp->FreeEpgEventInfo(m_recordingInfo.pEpgEventInfo);
        m_recordingInfo.pEpgEventInfo = NULL;
    }

    // 1度プラグインを有効化すると、TVTestを閉じるまで別プロセスで同名のプラグインを有効にはできない
    if (m_hMutex) ::CloseHandle(m_hMutex);
    if (m_hModuleMutex) ::CloseHandle(m_hModuleMutex);
    return true;
}


// 設定の読み込み
void CTTRec::LoadSettings()
{
    if (m_fSettingsLoaded) return;

    if (!GetLongModuleFileName(g_hinstDLL, m_szIniFileName, ARRAY_SIZE(m_szIniFileName)) ||
        !::PathRenameExtension(m_szIniFileName, TEXT(".ini"))) m_szIniFileName[0] = 0;

    ::GetPrivateProfileString(TEXT("Settings"), TEXT("Driver"), TEXT(""),
                              m_szDriverName, ARRAY_SIZE(m_szDriverName), m_szIniFileName);

    m_totAdjustMax = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("TotAdjustMax"), 0, m_szIniFileName);
    m_totAdjustMax = min(max(m_totAdjustMax, 0), TOT_ADJUST_MAX_MAX);
    m_usesTask = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("UseTask"), 0, m_szIniFileName) != 0;
    m_resumeMargin = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ResumeMargin"), 5, m_szIniFileName);
    m_resumeMargin = max(m_resumeMargin, 0);
    m_suspendMargin = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("SuspendMargin"), 5, m_szIniFileName);
    m_suspendMargin = max(m_suspendMargin, 0);
    m_joinsEvents = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("JoinEvents"), 0, m_szIniFileName) != 0;

    ::GetPrivateProfileString(TEXT("Settings"), TEXT("TVTestCmdOption"), TEXT(""),
                              m_szCmdOption, ARRAY_SIZE(m_szCmdOption), m_szIniFileName);

    m_chChangeBefore = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ChChangeBefore"), 120, m_szIniFileName);
    if (m_chChangeBefore <= 0) m_chChangeBefore = 0;
    else if (m_chChangeBefore < 15) m_chChangeBefore = 15;

    m_spinUpBefore = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("SpinUpBefore"), 20, m_szIniFileName);
    if (m_spinUpBefore <= 0) m_spinUpBefore = 0;
    else if (m_spinUpBefore < 15) m_spinUpBefore = 15;

    m_suspendWait = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("SuspendWait"), 10, m_szIniFileName);
	m_suspendWait = min(max(m_suspendWait, 0), 120);
    m_execWait = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ExecWait"), 10, m_szIniFileName);
	m_execWait = min(max(m_execWait, 0), 120);
    m_fForceSuspend = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ForceSuspend"), 0, m_szIniFileName) != 0;
    m_fDoSetPreview = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("SetPreview"), 1, m_szIniFileName) != 0;
    m_fDoSetPreviewNoViewOnly = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("SetPreviewNoViewOnly"), 0, m_szIniFileName) != 0;
    m_fShowDlgOnAppSuspend = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ShowDialogOnAppSuspend"), 1, m_szIniFileName) != 0;
    m_fShowNotifyIcon = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ShowTrayWhileAppSuspend"), 1, m_szIniFileName) != 0;
    m_appSuspendTimeout = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("AppSuspendTimeout"), 20, m_szIniFileName);
    m_appSuspendTimeout = max(m_appSuspendTimeout, 1);
    m_notifyLevel = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("NotifyLevel"), 1, m_szIniFileName);
    m_notifyLevel = min(max(m_notifyLevel, 0), 3);
    m_logLevel = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("LogLevel"), 1, m_szIniFileName);
    m_logLevel = min(max(m_logLevel, 0), 3);

    ::GetPrivateProfileString(TEXT("Settings"), TEXT("ExecOnStartRec"), TEXT(";\"\"Plugins\\TTRec_Exec.bat\"\""),
                              m_szExecOnStartRec, ARRAY_SIZE(m_szExecOnStartRec), m_szIniFileName);
    ::GetPrivateProfileString(TEXT("Settings"), TEXT("ExecOnEndRec"), TEXT(";\"\"Plugins\\TTRec_Exec.bat\"\""),
                              m_szExecOnEndRec, ARRAY_SIZE(m_szExecOnEndRec), m_szIniFileName);

    int color;
    color = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("NormalColor"), 64255000, m_szIniFileName);
    m_normalColor = RGB(color/1000000%1000, color/1000%1000, color%1000);
    color = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("NearestColor"), 255160000, m_szIniFileName);
    m_nearestColor = RGB(color/1000000%1000, color/1000%1000, color%1000);
    color = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("RecColor"), 255064000, m_szIniFileName);
    m_recColor = RGB(color/1000000%1000, color/1000%1000, color%1000);
    color = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("PriorityColor"), 64064064, m_szIniFileName);
    m_priorityColor = RGB(color/1000000%1000, color/1000%1000, color%1000);

    m_defaultRecOption.LoadDefaultSetting(m_szIniFileName);

    // デフォルト保存先フォルダはTVTest本体の設定を使用する
    if (m_pApp->GetSetting(L"RecordFolder", m_defaultRecOption.saveDir,
                           ARRAY_SIZE(m_defaultRecOption.saveDir)) <= 0) m_defaultRecOption.saveDir[0] = 0;

    m_fSettingsLoaded = true;

    // デフォルトの設定キーを出力するため
    if (::GetPrivateProfileInt(TEXT("Settings"), TEXT("ShowTrayWhileAppSuspend"), 99, m_szIniFileName) == 99)
        SaveSettings();
}


// 設定の保存
void CTTRec::SaveSettings() const
{
    if (!m_fSettingsLoaded) return;

    ::WritePrivateProfileString(TEXT("Settings"), TEXT("Driver"), m_szDriverName, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("TotAdjustMax"), m_totAdjustMax, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("UseTask"), m_usesTask, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ResumeMargin"), m_resumeMargin, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SuspendMargin"), m_suspendMargin, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("JoinEvents"), m_joinsEvents, m_szIniFileName);
    WritePrivateProfileStringQuote(TEXT("Settings"), TEXT("TVTestCmdOption"), m_szCmdOption, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ChChangeBefore"), m_chChangeBefore, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SpinUpBefore"), m_spinUpBefore, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SuspendWait"), m_suspendWait, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ExecWait"), m_execWait, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ForceSuspend"), m_fForceSuspend, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SetPreview"), m_fDoSetPreview, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SetPreviewNoViewOnly"), m_fDoSetPreviewNoViewOnly, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ShowDialogOnAppSuspend"), m_fShowDlgOnAppSuspend, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ShowTrayWhileAppSuspend"), m_fShowNotifyIcon, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("AppSuspendTimeout"), m_appSuspendTimeout, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("NotifyLevel"), m_notifyLevel, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("LogLevel"), m_logLevel, m_szIniFileName);
    WritePrivateProfileStringQuote(TEXT("Settings"), TEXT("ExecOnStartRec"), m_szExecOnStartRec, m_szIniFileName);
    WritePrivateProfileStringQuote(TEXT("Settings"), TEXT("ExecOnEndRec"), m_szExecOnEndRec, m_szIniFileName);

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

    m_defaultRecOption.SaveDefaultSetting(m_szIniFileName);
}


// プラグインが有効にされた時の初期化処理
bool CTTRec::InitializePlugin()
{
    if (m_fInitialized) return true;

    if (!m_pApp->QueryMessage(TVTest::MESSAGE_ENABLEPROGRAMGUIDEEVENT)) {
        m_pApp->AddLog(L"有効化できません(TVTestのバージョンが古いようです)。");
        return false;
    }

    LoadSettings();

    if (!m_szDriverName[0]) {
        m_pApp->AddLog(L"有効化しません(ドライバ名を指定してください)。");
        return false;
    }
    // 使用中のドライバ名から自分が有効になるべきか判断する
    TCHAR driverName[MAX_PATH];
    m_pApp->GetDriverName(driverName, ARRAY_SIZE(driverName));
    if (::lstrcmpi(::PathFindFileName(driverName), ::PathFindFileName(m_szDriverName))) return false;

    // 同名のプラグインは複数有効化できない
    if (!m_hMutex) {
        TCHAR name[MAX_PATH];
        if (!GetIdentifierFromModule(g_hinstDLL, name, MAX_PATH)) return false;
        m_hMutex = CreateFullAccessMutex(FALSE, name);
        if (!m_hMutex) return false;

        if (::GetLastError() == ERROR_ALREADY_EXISTS) {
            m_pApp->AddLog(L"有効化できません(プラグインは既に使用されています)。");
            ::CloseHandle(m_hMutex);
            m_hMutex = NULL;
            return false;
        }
    }

    // プラグイン全体のMutex(スリープ可能かどうかの判断につかう)
    if (!m_hModuleMutex) {
        m_hModuleMutex = CreateFullAccessMutex(FALSE, MODULE_ID);
        if (!m_hModuleMutex) return false;
    }

    // ウィンドウクラスの登録
    WNDCLASS wc = {0};
    wc.lpfnWndProc = RecordingWndProc;
    wc.hInstance = g_hinstDLL;
    wc.lpszClassName = TTREC_WINDOW_CLASS;
    if (::RegisterClass(&wc) == 0) return false;

    TCHAR pluginFileName[MAX_PATH];
    if (!GetLongModuleFileName(g_hinstDLL, pluginFileName, ARRAY_SIZE(pluginFileName))) return false;

    // 予約リスト初期化
    m_reserveList.SetPluginFileName(pluginFileName);
    if (!m_reserveList.Load()) {
        m_pApp->AddLog(L"_Reserves.txtの読み込みエラーが発生しました。");
        return false;
    }
    // クエリリスト初期化
    m_queryList.SetPluginFileName(pluginFileName);
    if (!m_queryList.Load()) {
        m_pApp->AddLog(L"_Queries.txtの読み込みエラーが発生しました。");
        return false;
    }

    // 番組表のコマンドを登録
    m_pApp->RegisterProgramGuideCommand(PROGRAM_GUIDE_COMMAND_LIST, ARRAY_SIZE(PROGRAM_GUIDE_COMMAND_LIST));

    m_fInitialized = true;
    return true;
}


// プラグインの有効状態が変化した
bool CTTRec::EnablePlugin(bool fEnable, bool fExit) {
    if (fEnable) {
        if (!InitializePlugin()) return false;
        // バルーンチップ作成
        m_balloonTip.Initialize(m_pApp->GetAppWindow(), g_hinstDLL);

        // 録画制御ウィンドウの作成
        if (!m_hwndRecording) {
            InitializeTotAdjust();
            ResetRecording();
            // WM_POWERBROADCASTを受け取るためオーナーをHWND_MESSAGEにしない
            m_hwndRecording = ::CreateWindow(TTREC_WINDOW_CLASS, NULL, 0,
                                             0, 0, 0, 0, NULL, NULL, g_hinstDLL, this);
            if (!m_hwndRecording) return false;
        }
        // トレイアイコン準備
        m_notifyIcon.Initialize(m_hwndRecording, 1, WM_NOTIFY_ICON);
        // ストリームコールバックの登録
        m_pApp->SetStreamCallback(0, StreamCallback, this);
    }
    else {
        // ストリームコールバックの登録解除
        m_pApp->SetStreamCallback(TVTest::STREAM_CALLBACK_REMOVE, StreamCallback);
        // トレイアイコン破棄
        m_notifyIcon.Finalize();
        // 録画制御ウィンドウの破棄
        if (m_hwndRecording) {
            ::DestroyWindow(m_hwndRecording);
            ResetRecording();
            m_hwndRecording = NULL;
        }
        // バルーンチップ破棄
        m_balloonTip.Finalize();
    }

    if (!fExit) {
        // 番組表のイベントの通知の有効/無効を設定する
        m_pApp->EnableProgramGuideEvent(TVTest::PROGRAMGUIDE_EVENT_GENERAL |
                                        (fEnable ? TVTest::PROGRAMGUIDE_EVENT_PROGRAM : 0));
        RedrawProgramGuide();
    }
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
        return TRUE;
    case TVTest::EVENT_PROGRAMGUIDE_FINALIZE:
        // 番組表の終了処理
        pThis->m_hwndProgramGuide = NULL;
        return TRUE;
    case TVTest::EVENT_PROGRAMGUIDE_COMMAND:
        // 番組表のコマンド実行
        {
            const TVTest::ProgramGuideCommandParam *pCommandParam =
                reinterpret_cast<const TVTest::ProgramGuideCommandParam*>(lParam2);
            if (pCommandParam->Action == TVTest::PROGRAMGUIDE_COMMAND_ACTION_MOUSE) {
                return pThis->OnMenuOrProgramMenuSelected(&pCommandParam->Program, static_cast<UINT>(lParam1));
            }
        }
        break;
    case TVTest::EVENT_PROGRAMGUIDE_INITIALIZEMENU:
        // メニューの初期化
        return pThis->InitializeMenu(
            reinterpret_cast<const TVTest::ProgramGuideInitializeMenuInfo*>(lParam1));
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
        if (lParam1 == TVTest::RECORD_STATUS_NOTRECORDING &&
            pThis->m_recordingState == REC_ACTIVE) pThis->m_fStopRecording = true;
        // FALL THROUGH!
    case TVTest::EVENT_CHANNELCHANGE:
    case TVTest::EVENT_SERVICECHANGE:
        // REC_ACTIVE_VIEW_ONLY状態で上記のイベントが起きた(=ユーザによる視聴停止)
        if (pThis->m_recordingState == REC_ACTIVE_VIEW_ONLY) pThis->m_fStopRecording = true;
        break;
    case TVTest::EVENT_STANDBY:
        // 待機状態が変化した
        if (pThis->m_pApp->IsPluginEnabled()) {
            if (!lParam1) {
                pThis->m_notifyIcon.Hide();
                pThis->m_fOnStoppedPostponed = false;
            }
            else if (pThis->m_fOnStoppedPostponed && pThis->m_fShowNotifyIcon) {
                pThis->m_notifyIcon.Show();
            }
        }
        break;
    case TVTest::EVENT_STATUSRESET:
        // ステータスがリセットされた
        pThis->m_recordingInfo.startStatusInfo.Size = 0;
        break;
    case TVTest::EVENT_STARTUPDONE:
        // 起動時の処理が終わった
        // プラグインの有効化を試みる(成否は使用中のドライバによる)
        pThis->m_pApp->EnablePlugin(true);
        break;
    }
    return 0;
}


// 必要であればバルーンチップを表示する
// notifyLevel==1:警告,2:録画イベント,3:その他イベント
void CTTRec::ShowBalloonTip(LPCTSTR text, int notifyLevel)
{
    if (m_hwndRecording && notifyLevel <= m_notifyLevel) {
        TCHAR cap[128];
        ::lstrcpy(cap, INFO_PLUGIN_NAME);
        ::lstrcat(cap, m_szCaptionSuffix);
        m_balloonTip.Show(text, cap, NULL, notifyLevel == 1 ? CBalloonTip::ICON_WARNING : CBalloonTip::ICON_INFO);
        ::SetTimer(m_hwndRecording, HIDE_BALLOON_TIP_TIMER_ID, BALLOON_TIP_TIMEOUT, NULL);
    }
    if (notifyLevel <= m_logLevel) {
        // ログ出力時は文字数制限して改行を置換
        TCHAR tmp[256];
        ::lstrcpyn(tmp, text, ARRAY_SIZE(tmp));
        for (TCHAR *p = tmp; *p; ++p) if (*p == TEXT('\n')) *p = TEXT('/');
        m_pApp->AddLog(tmp);
    }
}


// 必要であればタスクスケジューラ登録を行う
void CTTRec::RunSaveTask()
{
    if (m_fInitialized && m_usesTask) {
        TCHAR appName[MAX_PATH];
        if (!GetLongModuleFileName(NULL, appName, ARRAY_SIZE(appName)) ||
            !m_reserveList.RunSaveTask(m_resumeMargin, m_execWait, appName, m_szDriverName,
                                        m_szCmdOption, m_hwndRecording, WM_RUN_SAVE_TASK_DONE))
        {
            ShowBalloonTip(TEXT("タスクスケジューラ登録に失敗しました。"), 1);
        }
    }
}


// 番組の背景を描画
bool CTTRec::DrawBackground(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                            const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo) const
{
    ASSERT(m_pApp->IsPluginEnabled());

    const RESERVE *pRes = m_reserveList.Get(pProgramInfo->NetworkID, pProgramInfo->TransportStreamID,
                                            pProgramInfo->ServiceID, pProgramInfo->EventID);
    if (!pRes) return false;

    DrawReserveFrame(pProgramInfo, pInfo, *pRes, m_normalColor, pRes->recOption.IsViewOnly());

    // 予約の状態を正しく描画するためm_nearestを直接参照する
    if (pRes->eventID == m_nearest.eventID && pRes->networkID == m_nearest.networkID &&
        pRes->transportStreamID == m_nearest.transportStreamID && pRes->serviceID == m_nearest.serviceID)
    {
        if (m_recordingState == REC_ACTIVE)
            DrawReserveFrame(pProgramInfo, pInfo, m_nearest, m_recColor, false);
        else if (m_recordingState == REC_ACTIVE_VIEW_ONLY)
            DrawReserveFrame(pProgramInfo, pInfo, m_nearest, m_recColor, true);
        else
            DrawReserveFrame(pProgramInfo, pInfo, m_nearest, m_nearestColor, m_nearest.recOption.IsViewOnly());
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

    BYTE priority = res.recOption.priority % PRIORITY_MOD == PRIORITY_DEFAULT ?
                    m_defaultRecOption.priority % PRIORITY_MOD : res.recOption.priority % PRIORITY_MOD;
    BYTE onStopped = res.recOption.onStopped == ON_STOPPED_DEFAULT ?
                     m_defaultRecOption.onStopped : res.recOption.onStopped;
    int x = frameRect.right - 9;
    int y = frameRect.bottom - 9;
    if (onStopped >= ON_STOPPED_S_NONE) {
        ::MoveToEx(pInfo->hdc, x, y, NULL);
        ::LineTo(pInfo->hdc, x + 6, y);
        ::MoveToEx(pInfo->hdc, x + 3, y, NULL);
        ::LineTo(pInfo->hdc, x + 3, y + 6);
        x -= 10;
    }
    if (priority != PRIORITY_NORMAL) {
        ::MoveToEx(pInfo->hdc, x, y + 3, NULL);
        ::LineTo(pInfo->hdc, x + 6, y + 3);
        if (priority >= PRIORITY_HIGH) {
            ::MoveToEx(pInfo->hdc, x + 3, y, NULL);
            ::LineTo(pInfo->hdc, x + 3, y + 6);
        }
        x -= 10;
    }
    if (priority == PRIORITY_LOWEST || priority == PRIORITY_HIGHEST) {
        ::MoveToEx(pInfo->hdc, x, y + 3, NULL);
        ::LineTo(pInfo->hdc, x + 6, y + 3);
        if (priority == PRIORITY_HIGHEST) {
            ::MoveToEx(pInfo->hdc, x + 3, y, NULL);
            ::LineTo(pInfo->hdc, x + 3, y + 6);
        }
        x -= 10;
    }

    ::SelectObject(pInfo->hdc, hOld);
    ::DeleteObject(hPen);
}


// 予約の枠を描画
void CTTRec::DrawReserveFrame(const TVTest::ProgramGuideProgramInfo *pProgramInfo,
                              const TVTest::ProgramGuideProgramDrawBackgroundInfo *pInfo,
                              const RESERVE &res, COLORREF color, bool fDash) const
{
    RECT frameRect;
    GetReserveFrameRect(pProgramInfo, res, pInfo->ItemRect, &frameRect);

    LOGBRUSH lb;
    lb.lbStyle = BS_SOLID;
    lb.lbColor = color;
    lb.lbHatch = 0;
    HPEN hPen = ::ExtCreatePen((fDash ? PS_DASH : PS_SOLID) | PS_GEOMETRIC | PS_ENDCAP_SQUARE, 4, &lb, 0, NULL);

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

    int startOffset = static_cast<int>((res.GetTrimmedStartTime() - eventStart) / FILETIME_SECOND);
    if (startOffset < 0) startOffset = 0;

    int endOffset = static_cast<int>((res.GetTrimmedStartTime() - eventStart) / FILETIME_SECOND) + res.GetTrimmedDuration();
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
                 reinterpret_cast<UINT_PTR>(hMenuReserve), TEXT("TTRec-予約一覧"));

    // クエリ一覧用サブメニュー作成
    HMENU hMenuQuery = m_queryList.CreateListMenu(pInfo->Command + COMMAND_QUERYLIST);
    ::AppendMenu(pInfo->hmenu, MF_POPUP | (::GetMenuItemCount(hMenuQuery) <= 0 ? MF_DISABLED : MF_ENABLED),
                 reinterpret_cast<UINT_PTR>(hMenuQuery), TEXT("TTRec-クエリ一覧"));

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
    if (!m_pApp->IsPluginEnabled()) return false;

    bool fRet = true;
    bool fUpdated = false;
    if ((Command == COMMAND_RESERVE ||
         Command == COMMAND_RESERVE_DEFAULT ||
         Command == COMMAND_RESERVE_DEFAULT_OR_DELETE) && pProgramInfo)
    {
        // クリック位置の予約を取得
        const RESERVE *pRes = m_reserveList.Get(pProgramInfo->NetworkID, pProgramInfo->TransportStreamID,
                                                pProgramInfo->ServiceID, pProgramInfo->EventID);
        if (pRes) {
            if (Command == COMMAND_RESERVE_DEFAULT_OR_DELETE) {
                // 予約削除
                fUpdated = m_reserveList.Delete(pProgramInfo->NetworkID, pProgramInfo->TransportStreamID,
                                                pProgramInfo->ServiceID, pProgramInfo->EventID);
            }
            else {
                // サービスの名前を取得
                TCHAR serviceName[64];
                if (!GetChannelName(serviceName, ARRAY_SIZE(serviceName), pRes->networkID, pRes->serviceID))
                    serviceName[0] = 0;

                // 予約変更
                fUpdated = m_reserveList.Insert(g_hinstDLL, m_hwndProgramGuide, *pRes,
                                                m_defaultRecOption, serviceName, m_szCaptionSuffix);
            }
        }
        else {
            // 番組の情報を取得
            TVTest::EpgEventInfo *pEpgEventInfo = GetEventInfo(pProgramInfo);
            if (pEpgEventInfo) {
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
                res.recOption.SetDefault(m_defaultRecOption.IsViewOnly());
                ::SystemTimeToFileTime(&pProgramInfo->StartTime, &res.startTime);
                ::lstrcpyn(res.eventName, pEpgEventInfo->pszEventName ? pEpgEventInfo->pszEventName : TEXT(""), ARRAY_SIZE(res.eventName));

                if (Command == COMMAND_RESERVE_DEFAULT || Command == COMMAND_RESERVE_DEFAULT_OR_DELETE) {
                    fUpdated = m_reserveList.Insert(res);
                }
                else {
                    fUpdated = m_reserveList.Insert(g_hinstDLL, m_hwndProgramGuide, res,
                                                    m_defaultRecOption, serviceName, m_szCaptionSuffix);
                }
                m_pApp->FreeEpgEventInfo(pEpgEventInfo);
            }
        }
    }
    else if (Command == COMMAND_QUERY && pProgramInfo) {
        // 番組の情報を取得
        TVTest::EpgEventInfo *pEpgEventInfo = GetEventInfo(pProgramInfo);
        if (pEpgEventInfo) {
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
            query.recOption.SetDefault(m_defaultRecOption.IsViewOnly());
            query.keyword[0] = PREFIX_IGNORECASE;
            ::lstrcpyn(query.keyword + 1, pEpgEventInfo->pszEventName ? pEpgEventInfo->pszEventName : TEXT(""), ARRAY_SIZE(query.keyword) - 1);

            int index = m_queryList.Insert(-1, g_hinstDLL, m_hwndProgramGuide, query,
                                           m_defaultRecOption, serviceName, m_szCaptionSuffix);
            if (index >= 0) {
                if (!m_queryList.Save()) {
                    ShowBalloonTip(TEXT("_Queries.txtの書き込みエラーが発生しました。"), 1);
                }
                // すぐにクエリチェックする(indexが存在していなくても大丈夫)
                m_checkQueryIndex = index;
                if (m_hwndRecording) ::PostMessage(m_hwndRecording, WM_TIMER, CHECK_QUERY_LIST_TIMER_ID, NULL);
            }
            m_pApp->FreeEpgEventInfo(pEpgEventInfo);
        }
    }
    else if (COMMAND_RESERVELIST <= Command && Command < COMMAND_RESERVELIST + MENULIST_MAX) {
        // 途中で予約が追加消滅した場合にindexがずれる可能性がある
        const RESERVE *pRes = m_reserveList.Get(Command - COMMAND_RESERVELIST);
        if (pRes) {
            // サービスの名前を取得
            TCHAR serviceName[64];
            if (!GetChannelName(serviceName, ARRAY_SIZE(serviceName), pRes->networkID, pRes->serviceID))
                serviceName[0] = 0;

            // 予約変更
            fUpdated = m_reserveList.Insert(g_hinstDLL, m_hwndProgramGuide, *pRes,
                                            m_defaultRecOption, serviceName, m_szCaptionSuffix);
        }
    }
    else if (COMMAND_QUERYLIST <= Command && Command < COMMAND_QUERYLIST + MENULIST_MAX) {
        const QUERY *pQuery = m_queryList.Get(Command - COMMAND_QUERYLIST);
        if (pQuery) {
            // サービスの名前を取得
            TCHAR serviceName[64];
            if (!GetChannelName(serviceName, ARRAY_SIZE(serviceName), pQuery->networkID, pQuery->serviceID))
                serviceName[0] = 0;

            int index = m_queryList.Insert(Command - COMMAND_QUERYLIST, g_hinstDLL, m_hwndProgramGuide, *pQuery,
                                           m_defaultRecOption, serviceName, m_szCaptionSuffix);
            if (index >= 0) {
                if (!m_queryList.Save()) {
                    ShowBalloonTip(TEXT("_Queries.txtの書き込みエラーが発生しました。"), 1);
                }
                // すぐにクエリチェックする(indexが存在していなくても大丈夫)
                m_checkQueryIndex = index;
                if (m_hwndRecording) ::PostMessage(m_hwndRecording, WM_TIMER, CHECK_QUERY_LIST_TIMER_ID, NULL);
            }
        }
    }
    else {
        fRet = false;
    }

    if (fUpdated) {
        if (!m_reserveList.Save()) {
            ShowBalloonTip(TEXT("_Reserves.txtの書き込みエラーが発生しました。"), 1);
        }
        RunSaveTask();
        RedrawProgramGuide();
    }
    return fRet;
}


// プラグインの設定を行う
bool CTTRec::PluginSettings(HWND hwndOwner)
{
    LoadSettings();

    if (::DialogBoxParam(g_hinstDLL, MAKEINTRESOURCE(IDD_OPTIONS), hwndOwner,
        SettingsDlgProc, reinterpret_cast<LPARAM>(this)) != IDOK) return false;

    SaveSettings();
    RunSaveTask();
    return true;
}


// 設定ダイアログプロシージャ
INT_PTR CALLBACK CTTRec::SettingsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // WM_INITDIALOGのとき不定
    CTTRec *pThis = reinterpret_cast<CTTRec*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
    static const int TIMER_ID = 1;

    switch (uMsg) {
    case WM_INITDIALOG:
        {
            ::SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            pThis = reinterpret_cast<CTTRec*>(lParam);

            // キャプションをいじる
            TCHAR cap[128];
            if (pThis->m_szCaptionSuffix[0] && ::GetWindowText(hDlg, cap, 32)) {
                ::lstrcat(cap, pThis->m_szCaptionSuffix);
                ::SetWindowText(hDlg, cap);
            }

            TCHAR driverName[MAX_PATH];
            for (int i = 0; pThis->m_pApp->EnumDriver(i, driverName, ARRAY_SIZE(driverName)) != 0; i++) {
                ::SendDlgItemMessage(hDlg, IDC_COMBO_DRIVER_NAME, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(driverName));
            }
            ::SetDlgItemText(hDlg, IDC_COMBO_DRIVER_NAME, pThis->m_szDriverName);
            ::SendDlgItemMessage(hDlg, IDC_COMBO_DRIVER_NAME, EM_LIMITTEXT, ARRAY_SIZE(pThis->m_szDriverName) - 1, 0);

            TCHAR totList[TOT_ADJUST_MAX_MAX+1][32];
            LPCTSTR pTotList[TOT_ADJUST_MAX_MAX+1] = { TEXT("しない") };
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
            ::CheckDlgButton(hDlg, IDC_CHECK_SET_PREVIEW, pThis->m_fDoSetPreview ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(hDlg, IDC_CHECK_SET_PREVIEW_NO_VIEW_ONLY, pThis->m_fDoSetPreviewNoViewOnly ? BST_CHECKED : BST_UNCHECKED);
            ::SetDlgItemInt(hDlg, IDC_EDIT_CH_CHANGE, pThis->m_chChangeBefore, FALSE);
            ::SetDlgItemInt(hDlg, IDC_EDIT_SPIN_UP, pThis->m_spinUpBefore, FALSE);

            LPCTSTR pNotifyList[] = { TEXT("しない"), TEXT("警告のみ"), TEXT("警告と録画イベント"), TEXT("すべて") };
            SetComboBoxList(hDlg, IDC_COMBO_NOTIFY_LEVEL, pNotifyList, ARRAY_SIZE(pNotifyList));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_NOTIFY_LEVEL, CB_SETCURSEL, pThis->m_notifyLevel, 0);
            SetComboBoxList(hDlg, IDC_COMBO_LOG_LEVEL, pNotifyList, ARRAY_SIZE(pNotifyList));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_LOG_LEVEL, CB_SETCURSEL, pThis->m_logLevel, 0);

            if (pThis->m_pApp->IsPluginEnabled()) ::SetTimer(hDlg, TIMER_ID, 500, NULL);

            return pThis->m_defaultRecOption.DlgProc(hDlg, uMsg, wParam, false);
        }
    case WM_TIMER:
        if (wParam == TIMER_ID) {
            FILETIME totNow = pThis->m_totAdjustedNow;
            totNow += (::GetTickCount() - pThis->m_totAdjustedTick) * FILETIME_MILLISECOND;
            SYSTEMTIME totSysTime;
            ::FileTimeToSystemTime(&totNow, &totSysTime);
            TCHAR text[256];
            ::wsprintf(text, TEXT("%02hu:%02hu:%02hu"), totSysTime.wHour, totSysTime.wMinute, totSysTime.wSecond);
            ::SetDlgItemText(hDlg, IDC_STATIC_TOT, text);
            return TRUE;
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
            if (!::GetDlgItemText(hDlg, IDC_COMBO_DRIVER_NAME, pThis->m_szDriverName, ARRAY_SIZE(pThis->m_szDriverName)))
                pThis->m_szDriverName[0] = 0;

            pThis->m_totAdjustMax = static_cast<int>(::SendDlgItemMessage(hDlg, IDC_COMBO_TOT, CB_GETCURSEL, 0, 0));
            if (pThis->m_totAdjustMax < 0) pThis->m_totAdjustMax = 0;

            pThis->m_usesTask = ::IsDlgButtonChecked(hDlg, IDC_CHECK_USE_TASK) == BST_CHECKED;
            pThis->m_resumeMargin = ::GetDlgItemInt(hDlg, IDC_EDIT_RSM_M, NULL, FALSE);
            pThis->m_joinsEvents = ::IsDlgButtonChecked(hDlg, IDC_CHECK_JOIN_EVENTS) == BST_CHECKED;
            pThis->m_fDoSetPreview = ::IsDlgButtonChecked(hDlg, IDC_CHECK_SET_PREVIEW) == BST_CHECKED;
            pThis->m_fDoSetPreviewNoViewOnly = ::IsDlgButtonChecked(hDlg, IDC_CHECK_SET_PREVIEW_NO_VIEW_ONLY) == BST_CHECKED;

            if (!::GetDlgItemText(hDlg, IDC_EDIT_OPTION, pThis->m_szCmdOption, ARRAY_SIZE(pThis->m_szCmdOption)))
                pThis->m_szCmdOption[0] = 0;

            pThis->m_chChangeBefore = ::GetDlgItemInt(hDlg, IDC_EDIT_CH_CHANGE, NULL, FALSE);
            if (pThis->m_chChangeBefore <= 0) pThis->m_chChangeBefore = 0;
            else if (pThis->m_chChangeBefore < 15) pThis->m_chChangeBefore = 15;

            pThis->m_spinUpBefore = ::GetDlgItemInt(hDlg, IDC_EDIT_SPIN_UP, NULL, FALSE);
            if (pThis->m_spinUpBefore <= 0) pThis->m_spinUpBefore = 0;
            else if (pThis->m_spinUpBefore < 15) pThis->m_spinUpBefore = 15;

            pThis->m_notifyLevel = static_cast<int>(::SendDlgItemMessage(hDlg, IDC_COMBO_NOTIFY_LEVEL, CB_GETCURSEL, 0, 0));
            if (pThis->m_notifyLevel < 0) pThis->m_notifyLevel = 0;
            pThis->m_logLevel = static_cast<int>(::SendDlgItemMessage(hDlg, IDC_COMBO_LOG_LEVEL, CB_GETCURSEL, 0, 0));
            if (pThis->m_logLevel < 0) pThis->m_logLevel = 0;

            pThis->m_defaultRecOption.DlgProc(hDlg, uMsg, wParam, false);
            // FALL THROUGH!
        case IDCANCEL:
            ::KillTimer(hDlg, TIMER_ID);
            ::EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        default:
            return pThis->m_defaultRecOption.DlgProc(hDlg, uMsg, wParam, false);
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
    if (!MatchKeyword(ev.pszEventName ? ev.pszEventName : TEXT(""), q.keyword)) return false;

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

    DEBUG_OUT(TEXT("CTTRec::CheckQuery()\n"));

    FILETIME now;
    GetLocalTimeAsFileTime(&now);
    bool fUpdated = false;
    TCHAR updatedEventName[32];

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
            m_queryList.CreateReserve(queryIndex, &res, ev.EventID, ev.pszEventName ? ev.pszEventName : TEXT(""), evStart, ev.Duration) &&
            m_reserveList.Insert(res))
        {
            ::lstrcpyn(updatedEventName, res.eventName, ARRAY_SIZE(updatedEventName));
            fUpdated = true;
        }
    }
    m_pApp->FreeEpgEventList(&eventList);

    if (fUpdated) {
        if (!m_queryList.Save()) {
            ShowBalloonTip(TEXT("_Queries.txtの書き込みエラーが発生しました。"), 1);
        }
        if (!m_reserveList.Save()) {
            ShowBalloonTip(TEXT("_Reserves.txtの書き込みエラーが発生しました。"), 1);
        }
        RunSaveTask();
        RedrawProgramGuide();

        TCHAR text[128];
        ::wsprintf(text, TEXT("クエリから新しい予約が生成されました:\n%s"), updatedEventName);
        ShowBalloonTip(text, 3);
    }
}


// 予約を追従する
void CTTRec::FollowUpReserves()
{
    DEBUG_OUT(TEXT("CTTRec::FollowUpReserves()\n"));

    bool fUpdated = false;
    TCHAR updatedEventName[32];

    for (int i = 0; i < FOLLOW_UP_MAX + 1; ++i) {
        // 直近FOLLOW_UP_MAX個より後ろの予約は1つずつチェックする
        const RESERVE *pRes;
        if (i == FOLLOW_UP_MAX) {
            pRes = m_reserveList.Get(m_followUpIndex++);
        }
        else {
            pRes = m_reserveList.Get(i);
        }
        if (!pRes) {
            m_followUpIndex = FOLLOW_UP_MAX;
            break;
        }

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
            if (m_reserveList.Insert(newRes)) {
                ::lstrcpyn(updatedEventName, newRes.eventName, ARRAY_SIZE(updatedEventName));
                fUpdated = true;
            }
        }
        m_pApp->FreeEpgEventInfo(pEvent);
    }

    if (fUpdated) {
        if (!m_reserveList.Save()) {
            ShowBalloonTip(TEXT("_Reserves.txtの書き込みエラーが発生しました。"), 1);
        }
        RunSaveTask();
        RedrawProgramGuide();

        TCHAR text[128];
        ::wsprintf(text, TEXT("予約時刻に変更がありました:\n%s"), updatedEventName);
        ShowBalloonTip(text, 3);
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
    if (m_szDriverName[0] && ::lstrcmpi(::PathFindFileName(driverName), ::PathFindFileName(m_szDriverName))) {
        ShowBalloonTip(TEXT("BonDriverを変更します。"), 3);
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


// 録画の制御をリセットする
// CheckRecording()使用の開始前と終了後に呼ぶ
void CTTRec::ResetRecording()
{
    m_nearest.networkID = m_nearest.transportStreamID =
        m_nearest.serviceID = m_nearest.eventID = 0xFFFF;

    m_recordingState = REC_IDLE;
    m_onStopped = ON_STOPPED_NONE;
    m_fChChanged = m_fSpunUp = false;
    m_fStopRecording = false;
    m_fOnStoppedPostponed = false;

    if (m_prevExecState) {
        if (m_fVistaOrLater) ::SetThreadExecutionState(m_prevExecState);
        m_prevExecState = 0;
    }
}


// 録画を制御する
void CTTRec::CheckRecording()
{
    bool fUpdated = false;
    bool fOnStopped = false;
    WORD prevNetworkID = m_nearest.networkID;
    WORD prevTransportStreamID = m_nearest.transportStreamID;
    WORD prevServiceID = m_nearest.serviceID;
    WORD prevEventID = m_nearest.eventID;

    // 直近の予約とその開始までのオフセットを取得する
    LONGLONG startOffset;
    for (;;) {
        FILETIME &now = m_totAdjustedNow;
        // 追従処理等により予約は入れ替わることがある
        if (!m_reserveList.GetNearest(&m_nearest, m_defaultRecOption, REC_READY_OFFSET)) {
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
            fUpdated = true;
        }
    }

    // 予約サービスが変化したか
    bool fServiceChanged = prevNetworkID != m_nearest.networkID ||
                           prevTransportStreamID != m_nearest.transportStreamID ||
                           prevServiceID != m_nearest.serviceID;
    // 予約イベントが変化したか
    bool fEventChanged = fServiceChanged || prevEventID != m_nearest.eventID;
    // ┌───┐
    // │      ↓
    // │  ┌─REC_IDLE
    // │  │  ↓
    // │  │  REC_STANDBY──┐
    // │  │  ↓             │
    // │  └→REC_READY───┤
    // │      ↓    ↓       │
    // │REC_ACTIVE VIEW_ONLY │
    // │      ↓    ↓       ↓
    // │      REC_ENDED REC_CANCELED
    // └───┴──────-┘
    // startOffset==LLONG_MAXのとき予約がない(m_nearestは無効)ので注意
    switch (m_recordingState) {
        case REC_IDLE:
        case REC_STANDBY:
            if (m_recordingState == REC_STANDBY) {
                if (startOffset >= (m_suspendMargin + m_resumeMargin) * FILETIME_MINUTE) {
                    // 待機時刻より前
                    m_recordingState = REC_CANCELED;
                    m_onStopped = m_defaultRecOption.onStopped;
                    ShowBalloonTip(TEXT("直近の予約時刻に変更がありました。"), 2);
                }
            }
            else {
                if (startOffset < (m_suspendMargin + m_resumeMargin) * FILETIME_MINUTE) {
                    // 待機時刻～
                    m_recordingState = REC_STANDBY;
                    if (m_fOnStoppedPostponed) {
                        m_fOnStoppedPostponed = false;
                        m_pApp->SetStandby(false);
                    }
                }
            }

            if (startOffset < REC_READY_OFFSET * FILETIME_SECOND) {
                // 準備時刻～
                m_recordingState = REC_READY;
                if (m_fOnStoppedPostponed) {
                    m_fOnStoppedPostponed = false;
                    m_pApp->SetStandby(false);
                }
                if (IsNotRecording()) {
                    SetChannel(m_nearest.networkID, m_nearest.serviceID);
                    m_fChChanged = true;
                }
                TCHAR text[128];
                int len = ::wsprintf(text, TEXT("%sが始まります:\n"),
                                     m_nearest.recOption.IsViewOnly() ? TEXT("見るだけ予約") : TEXT("録画"));
                ::lstrcpyn(text + len, m_nearest.eventName, 32);
                ShowBalloonTip(text, 2);
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
                if (!m_fSpunUp && m_spinUpBefore != 0 && !m_nearest.recOption.IsViewOnly()) {
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
                m_onStopped = m_nearest.recOption.onStopped;

                if (m_fDoSetPreview && m_nearest.recOption.IsViewOnly() ||
                    m_fDoSetPreviewNoViewOnly && !m_nearest.recOption.IsViewOnly())
                {
                    // 再生オン
                    if (m_pApp->GetStandby()) {
                        m_pApp->SetStandby(false);
                    }
                    else {
                        HWND hwnd = m_pApp->GetAppWindow();
                        if (::IsIconic(hwnd)) ::ShowWindow(hwnd, SW_RESTORE);
                        m_pApp->SetPreview(true);
                    }
                }
                if (m_nearest.recOption.IsViewOnly()) {
                    SetChannel(m_nearest.networkID, m_nearest.serviceID);
                    m_recordingState = REC_ACTIVE_VIEW_ONLY;
                }
                else {
                    SetChannel(m_nearest.networkID, m_nearest.serviceID);
                    m_recordingState = REC_ACTIVE;
                    // フォーマット指示子を"部分的に"置換
                    TCHAR replacedName[MAX_PATH];
                    FormatFileName(replacedName, ARRAY_SIZE(replacedName), m_nearest.eventID,
                                   m_nearest.startTime, m_nearest.eventName, m_nearest.recOption.saveName);
                    StartRecord(m_nearest.recOption.saveDir, replacedName);
                }
                OnStartRecording();
                RedrawProgramGuide();
            }
            else if (startOffset >= REC_READY_OFFSET * FILETIME_SECOND) {
                // 準備時刻より前
                m_recordingState = REC_CANCELED;
                m_onStopped = m_defaultRecOption.onStopped;
                ShowBalloonTip(TEXT("直近の予約時刻に変更がありました。"), 2);
            }
            break;
        case REC_ACTIVE:
            if (m_joinsEvents && fEventChanged && !fServiceChanged &&
                startOffset < (REC_READY_OFFSET + 2) * FILETIME_SECOND &&
                !m_nearest.recOption.IsViewOnly()) {
                // イベントが変化したがサービスが同じで、かつ準備時刻を過ぎている、かつ"見るだけ"ではない
                // 連結録画(状態遷移しない)
                m_onStopped = m_nearest.recOption.onStopped;
            }
            else if (fEventChanged || startOffset >= REC_READY_OFFSET * FILETIME_SECOND) {
                // 予約イベントが変わったか準備時刻より前
                m_recordingState = REC_ENDED;
                m_pApp->StopRecord();
            }
            else if (m_fStopRecording) {
                // ユーザ操作により録画が停止した
                m_recordingState = REC_ENDED;
                // 予約を削除
                m_reserveList.DeleteNearest(m_defaultRecOption);
                fUpdated = true;
            }
            else {
                m_onStopped = m_nearest.recOption.onStopped;
            }
            if (m_recordingState == REC_ENDED) {
                ShowBalloonTip(TEXT("録画が終了しました。"), 2);
            }
            break;
        case REC_ACTIVE_VIEW_ONLY:
            if (fEventChanged || startOffset >= REC_READY_OFFSET * FILETIME_SECOND) {
                // 予約イベントが変わったか準備時刻より前
                m_recordingState = REC_ENDED;
            }
            else if (m_fStopRecording) {
                // ユーザ操作により視聴が停止した
                m_recordingState = REC_ENDED;
                m_onStopped = ON_STOPPED_NONE;
                // 予約を削除
                m_reserveList.DeleteNearest(m_defaultRecOption);
                fUpdated = true;
            }
            else {
                m_onStopped = m_nearest.recOption.onStopped;
            }
            if (m_recordingState == REC_ENDED) {
                ShowBalloonTip(TEXT("見るだけ予約が終了しました。"), 2);
            }
            break;
        case REC_ENDED:
            OnEndRecording();
            // FALL THROUGH!
        case REC_CANCELED:
            m_recordingState = REC_IDLE;
            if (startOffset >= (m_suspendMargin + m_resumeMargin) * FILETIME_MINUTE && IsNotRecording()) {
                // 予約開始まで時間に余裕があり、かつ録画停止中
                fOnStopped = true;
            }
            m_fChChanged = m_fSpunUp = false;
            m_fStopRecording = false;
            break;
        default:
            m_recordingState = REC_IDLE;
            break;
    }

    // スリープを防ぐ
    if (m_recordingState != REC_IDLE || m_fOnStoppedPostponed) {
        // なるべくES_CONTINUOUSは使わない
        ::SetThreadExecutionState(ES_SYSTEM_REQUIRED);
        if (!m_prevExecState) {
            // Vista以降は(「見るだけ」か否かにかかわらず)AWAY MODEに移るようにする
            m_prevExecState = m_fVistaOrLater ?
                              ::SetThreadExecutionState(ES_CONTINUOUS | ES_AWAYMODE_REQUIRED) : ES_CONTINUOUS;
        }
    }
    else {
        if (m_prevExecState) {
            if (m_fVistaOrLater) ::SetThreadExecutionState(m_prevExecState);
            m_prevExecState = 0;
        }
    }

    if (fUpdated) {
        if (!m_reserveList.Save()) {
            ShowBalloonTip(TEXT("_Reserves.txtの書き込みエラーが発生しました。"), 1);
        }
        RunSaveTask();
    }
    if (fUpdated || fEventChanged) {
        RedrawProgramGuide();
    }
    if (fOnStopped) {
        BYTE onStopped = m_onStopped;
        if (onStopped == ON_STOPPED_S_NONE ||
            onStopped == ON_STOPPED_S_CLOSE ||
            onStopped == ON_STOPPED_S_SUSPEND ||
            onStopped == ON_STOPPED_S_HIBERNATE)
        {
            m_onStopped = (BYTE)(onStopped == ON_STOPPED_S_NONE ? ON_STOPPED_NONE :
                                 onStopped == ON_STOPPED_S_CLOSE ? ON_STOPPED_CLOSE :
                                 onStopped == ON_STOPPED_S_SUSPEND ? ON_STOPPED_SUSPEND : ON_STOPPED_HIBERNATE);
            // TVTestを待機状態にするのに十分な余裕があるか
            if (startOffset >= (m_appSuspendTimeout + m_suspendMargin + m_resumeMargin) * FILETIME_MINUTE && !m_pApp->GetStandby()) {
                // 録画後動作を延期する
                m_fOnStoppedPostponed = true;
                ::SetTimer(m_hwndRecording, DONE_APP_SUSPEND_TIMER_ID, m_appSuspendTimeout * 60000, NULL);
            }
            else {
                // 録画後動作のみ行う
                onStopped = m_onStopped;
            }
        }
        // メッセージループに入るので注意
        OnStopped(onStopped);
    }
}


// 録画開始/終了時プロセスを起動
bool CTTRec::ExecuteCommandLine(LPTSTR commandLine, LPCTSTR currentDirectory, const RECORDING_INFO &info)
{
    if (!info.fEnabled) return false;

    // 子プロセスに渡すために環境変数をセット
    TCHAR str[64];
    FILETIME time = info.reserve.startTime;
    time += -info.reserve.recOption.startMargin * FILETIME_SECOND;
    FileTimeToStr(&time, str);
    ::SetEnvironmentVariable(TEXT("TTRecStartTime"), str);
    TimeSpanToStr(info.reserve.duration +
                  info.reserve.recOption.startMargin +
                  info.reserve.recOption.endMargin, str);
    ::SetEnvironmentVariable(TEXT("TTRecDuration"), str);

    ::wsprintf(str, TEXT("%d"), info.reserve.networkID);
    ::SetEnvironmentVariable(TEXT("TTRecONID"), str);
    ::wsprintf(str, TEXT("%d"), info.reserve.transportStreamID);
    ::SetEnvironmentVariable(TEXT("TTRecTSID"), str);
    ::wsprintf(str, TEXT("%d"), info.reserve.serviceID);
    ::SetEnvironmentVariable(TEXT("TTRecSID"), str);
    ::wsprintf(str, TEXT("%d"), info.reserve.eventID);
    ::SetEnvironmentVariable(TEXT("TTRecEID"), str);

    bool fErr = !info.startStatusInfo.Size || !info.endStatusInfo.Size;
    ::wsprintf(str, TEXT("%d"), fErr ? -1 : max((int)info.endStatusInfo.ErrorPacketCount - (int)info.startStatusInfo.ErrorPacketCount, -1));
    ::SetEnvironmentVariable(TEXT("TTRecErrors"), str);
    ::wsprintf(str, TEXT("%d"), fErr ? -1 : max((int)info.endStatusInfo.ScramblePacketCount - (int)info.startStatusInfo.ScramblePacketCount, -1));
    ::SetEnvironmentVariable(TEXT("TTRecScrambles"), str);
    ::wsprintf(str, TEXT("%d"), fErr ? -1 : max((int)info.endStatusInfo.DropPacketCount - (int)info.startStatusInfo.DropPacketCount, -1));
    ::SetEnvironmentVariable(TEXT("TTRecDrops"), str);

    ::SetEnvironmentVariable(TEXT("TTRecFilePath"), info.filePath[0] ? info.filePath : NULL);
    ::SetEnvironmentVariable(TEXT("TTRecServiceName"), info.serviceName[0] ? info.serviceName : NULL);

    if (info.pEpgEventInfo) {
        ::SystemTimeToFileTime(&info.pEpgEventInfo->StartTime, &time);
        FileTimeToStr(&time, str);
        ::SetEnvironmentVariable(TEXT("TTRecEventStartTime"), str);
        TimeSpanToStr(info.pEpgEventInfo->Duration, str);
        ::SetEnvironmentVariable(TEXT("TTRecEventDuration"), str);
    }
    else {
        ::SetEnvironmentVariable(TEXT("TTRecEventStartTime"), NULL);
        ::SetEnvironmentVariable(TEXT("TTRecEventDuration"), NULL);
    }
    ::SetEnvironmentVariable(TEXT("TTRecEventName"), info.pEpgEventInfo ? info.pEpgEventInfo->pszEventName : NULL);
    ::SetEnvironmentVariable(TEXT("TTRecEventText"), info.pEpgEventInfo ? info.pEpgEventInfo->pszEventText : NULL);
    ::SetEnvironmentVariable(TEXT("TTRecEventExText"), info.pEpgEventInfo ? info.pEpgEventInfo->pszEventExtendedText : NULL);

    // ファイルパスをコマンドライン引数に追加
    TCHAR commandLinePath[MAX_PATH * 2 + 3];
    ::lstrcpyn(commandLinePath, commandLine, MAX_PATH);
    if (info.filePath[0] && !::StrChr(info.filePath, TEXT('"'))) {
        ::lstrcat(commandLinePath, TEXT(" \""));
        ::lstrcat(commandLinePath, info.filePath);
        ::lstrcat(commandLinePath, TEXT("\""));
    }

    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION ps;
    if (::CreateProcess(NULL, commandLinePath, NULL, NULL, FALSE, 0, NULL, currentDirectory, &si, &ps)) {
        ::CloseHandle(ps.hThread);
        ::CloseHandle(ps.hProcess);
        return true;
    }
    return false;
}


void CTTRec::OnStartRecording()
{
    if (m_szExecOnStartRec[0] && m_szExecOnStartRec[0] != TEXT(';') ||
        m_szExecOnEndRec[0] && m_szExecOnEndRec[0] != TEXT(';'))
    {
        TVTest::RecordStatusInfo rsi;
        rsi.pszFileName = m_recordingInfo.filePath;
        rsi.MaxFileName = ARRAY_SIZE(m_recordingInfo.filePath);
        if (m_nearest.recOption.IsViewOnly() || !m_pApp->GetRecordStatus(&rsi)) {
            m_recordingInfo.filePath[0] = 0;
        }
        if (!GetChannelName(m_recordingInfo.serviceName, ARRAY_SIZE(m_recordingInfo.serviceName),
                            m_nearest.networkID, m_nearest.serviceID)) {
            m_recordingInfo.serviceName[0] = 0;
        }
        if (m_recordingInfo.fEnabled && m_recordingInfo.pEpgEventInfo) {
            m_pApp->FreeEpgEventInfo(m_recordingInfo.pEpgEventInfo);
            m_recordingInfo.pEpgEventInfo = NULL;
        }
        TVTest::EpgEventQueryInfo queryInfo;
        queryInfo.NetworkID         = m_nearest.networkID;
        queryInfo.TransportStreamID = m_nearest.transportStreamID;
        queryInfo.ServiceID         = m_nearest.serviceID;
        queryInfo.EventID           = m_nearest.eventID;
        queryInfo.Type              = TVTest::EPG_EVENT_QUERY_EVENTID;
        queryInfo.Flags             = 0;
        m_recordingInfo.pEpgEventInfo = m_pApp->GetEpgEventInfo(&queryInfo);

        // 開始時のステータスはカウントの収束を待つため取得を遅らせる
        ::SetTimer(m_hwndRecording, GET_START_STATUS_INFO_TIMER_ID, GET_START_STATUS_INFO_DELAY, NULL);
        m_recordingInfo.startStatusInfo.Size = 0;
        m_recordingInfo.endStatusInfo.Size = 0;
        m_recordingInfo.reserve = m_nearest;
        m_recordingInfo.fEnabled = true;

        if (m_szExecOnStartRec[0] && m_szExecOnStartRec[0] != TEXT(';')) {
            ::SetEnvironmentVariable(TEXT("TTRecExec"), TEXT("StartRec"));
            TCHAR appDir[MAX_PATH];
            if (!GetLongModuleFileName(NULL, appDir, ARRAY_SIZE(appDir)) ||
                !::PathRemoveFileSpec(appDir) ||
                !ExecuteCommandLine(m_szExecOnStartRec, appDir, m_recordingInfo))
            {
                ShowBalloonTip(TEXT("コマンドラインの起動に失敗しました。"), 1);
            }
        }
    }
}


void CTTRec::OnEndRecording()
{
    if (m_szExecOnEndRec[0] && m_szExecOnEndRec[0] != TEXT(';')) {
        // 終了時のステータスを取得
        if (!m_pApp->GetStatus(&m_recordingInfo.endStatusInfo)) {
            m_recordingInfo.endStatusInfo.Size = 0;
        }
        ::SetEnvironmentVariable(TEXT("TTRecExec"), TEXT("EndRec"));
        TCHAR appDir[MAX_PATH];
        if (!GetLongModuleFileName(NULL, appDir, ARRAY_SIZE(appDir)) ||
            !::PathRemoveFileSpec(appDir) ||
            !ExecuteCommandLine(m_szExecOnEndRec, appDir, m_recordingInfo))
        {
            ShowBalloonTip(TEXT("コマンドラインの起動に失敗しました。"), 1);
        }
    }
}


// TVTestのフルスクリーンHWNDを取得する
// 必ず取得できると仮定してはいけない
HWND CTTRec::GetFullscreenWindow()
{
    TVTest::HostInfo hostInfo;
    if (m_pApp->GetFullscreen() && m_pApp->GetHostInfo(&hostInfo)) {
        TCHAR className[64];
        ::lstrcpyn(className, hostInfo.pszAppName, 48);
        ::lstrcat(className, L" Fullscreen");

        HWND hwnd = NULL;
        while ((hwnd = ::FindWindowEx(NULL, hwnd, className, NULL)) != NULL) {
            DWORD pid;
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid == ::GetCurrentProcessId()) return hwnd;
        }
    }
    return NULL;
}


// 録画終了/キャンセル後の動作を行う
bool CTTRec::OnStopped(BYTE mode)
{
    if (mode < ON_STOPPED_CLOSE || ON_STOPPED_S_HIBERNATE < mode) return false;

    // 確認のダイアログを表示
    HWND hwndParent = GetFullscreenWindow();
    if (!hwndParent) hwndParent = m_pApp->GetAppWindow();
    if ((mode < ON_STOPPED_S_NONE || m_fShowDlgOnAppSuspend) &&
        ::DialogBoxParam(g_hinstDLL, MAKEINTRESOURCE(IDD_ONSTOP), hwndParent,
        OnStoppedDlgProc, MAKELONG(ON_STOPPED_DLG_TIMEOUT, mode - ON_STOPPED_CLOSE)) != IDOK)
    {
        m_fOnStoppedPostponed = false;
        return false;
    }

    if (mode >= ON_STOPPED_S_NONE) {
        // 一旦最小化させることでトレイアイコンを出す
        //::SendMessage(m_pApp->GetAppWindow(), WM_SYSCOMMAND, SC_MINIMIZE, 0);
        m_epgCapSpace = -1;
        m_epgCapTimeout = 30;
        ::SetTimer(m_hwndRecording, WATCH_EPGCAP_TIMER_ID, 1000, NULL);
        if (m_pApp->SetStandby(true)) {
            TCHAR text[64];
            ::wsprintf(text, TEXT("待機状態にしました(%d分以内に復帰)。"), m_appSuspendTimeout);
            ShowBalloonTip(text, 2);
        }
    }
    else if (mode == ON_STOPPED_SUSPEND || mode == ON_STOPPED_HIBERNATE) {
        TCHAR pluginShortPath[MAX_PATH];
        if (!GetShortModuleFileName(g_hinstDLL, pluginShortPath, ARRAY_SIZE(pluginShortPath))) return false;

        TCHAR rundllPath[MAX_PATH];
        if (!GetRundll32Path(rundllPath)) return false;

        TCHAR cmdOption[MAX_PATH + 64];
        ::wsprintf(cmdOption, TEXT(" %s,DelayedSuspend %d %s%s"), pluginShortPath, m_suspendWait,
                   mode == ON_STOPPED_SUSPEND ? TEXT("S") : TEXT("H"),
                   m_fForceSuspend ? TEXT("F") : TEXT(""));

        // スリープ用のプロセスを起動
        STARTUPINFO si = { sizeof(si) };
        PROCESS_INFORMATION ps;
        if (::CreateProcess(rundllPath, cmdOption, NULL, NULL, FALSE, 0, NULL, NULL, &si, &ps)) {
            ::CloseHandle(ps.hThread);
            ::CloseHandle(ps.hProcess);
        }
        m_pApp->Close(TVTest::CLOSE_EXIT);
    }
    else if (mode == ON_STOPPED_CLOSE) {
        m_pApp->Close(TVTest::CLOSE_EXIT);
    }
    return true;
}


// 終了確認ダイアログプロシージャ
INT_PTR CALLBACK CTTRec::OnStoppedDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static const int TIMER_ID = 1;
    LPCTSTR message = TEXT("%2d秒後にTVTestを%sします");
    LPCTSTR mode[] = { TEXT("終了"), TEXT("終了してシステムをサスペンド"), TEXT("終了してシステムを休止状態に"),
                       TEXT("待機状態に"), TEXT("待機→終了"), TEXT("待機→終了してサスペンド"), TEXT("待機→終了して休止状態に") };

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
                LONG_PTR data = ::GetWindowLongPtr(hDlg, GWLP_USERDATA);
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
    // WM_CREATEのとき不定
    CTTRec *pThis = reinterpret_cast<CTTRec*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_CREATE:
        {
            LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            pThis = static_cast<CTTRec*>(pcs->lpCreateParams);
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));

            ::SetTimer(hwnd, FOLLOW_UP_TIMER_ID, CHECK_PROGRAMLIST_INTERVAL, NULL);
            ::SetTimer(hwnd, CHECK_QUERY_LIST_TIMER_ID,
                       max(CHECK_PROGRAMLIST_INTERVAL * 2 / (pThis->m_queryList.Length() + 1), 2000), NULL);
            ::SetTimer(hwnd, CHECK_RECORDING_TIMER_ID, CHECK_RECORDING_INTERVAL, NULL);
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMQUERYSUSPEND) {
            // Vista以降は呼ばれない
            if (pThis->m_prevExecState) {
                pThis->ShowBalloonTip(TEXT("サスペンドへの移行を拒否します。"), 3);
                return BROADCAST_QUERY_DENY;
            }
        }
        break;
    case WM_RUN_SAVE_TASK_DONE:
        if (!wParam) {
            TCHAR text[128];
            ::wsprintf(text, TEXT("%s\nHRESULT=0x%08x"), TEXT("タスクスケジューラ登録に失敗しました。"), static_cast<DWORD>(lParam));
            pThis->ShowBalloonTip(text, 1);
        }
        return 0;
    case WM_NOTIFY_ICON:
        if (LOWORD(lParam) == WM_LBUTTONUP) {
            ::PostMessage(hwnd, WM_TIMER, DONE_APP_SUSPEND_TIMER_ID, 0);
        }
        break;
    case WM_TIMER:
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
            case HIDE_BALLOON_TIP_TIMER_ID:
                pThis->m_balloonTip.Hide();
                ::KillTimer(hwnd, HIDE_BALLOON_TIP_TIMER_ID);
                break;
            case GET_START_STATUS_INFO_TIMER_ID:
                if (!pThis->m_pApp->GetStatus(&pThis->m_recordingInfo.startStatusInfo)) {
                    pThis->m_recordingInfo.startStatusInfo.Size = 0;
                }
                ::KillTimer(hwnd, GET_START_STATUS_INFO_TIMER_ID);
                break;
            case DONE_APP_SUSPEND_TIMER_ID:
                ::KillTimer(hwnd, DONE_APP_SUSPEND_TIMER_ID);
                ::KillTimer(hwnd, WATCH_EPGCAP_TIMER_ID);
                if (pThis->m_fOnStoppedPostponed) {
                    pThis->m_fOnStoppedPostponed = false;
                    // メッセージループに入るので注意
                    if (!pThis->OnStopped(pThis->m_onStopped)) {
                        // 録画後動作なしorキャンセルのときだけ復帰
                        pThis->m_pApp->SetStandby(false);
                    }
                }
                break;
            case WATCH_EPGCAP_TIMER_ID:
                // EPG取得の終了を監視する
                if (!pThis->m_fOnStoppedPostponed || --pThis->m_epgCapTimeout <= 0) {
                    ::SendMessage(hwnd, WM_TIMER, DONE_APP_SUSPEND_TIMER_ID, 0);
                }
                else {
                    TVTest::ChannelInfo ci;
                    if (!pThis->m_pApp->GetCurrentChannelInfo(&ci)) {
                        if (pThis->m_epgCapSpace >= 0) {
                            pThis->m_epgCapSpace = -1;
                            pThis->m_epgCapTimeout = 30;
                            TCHAR text[128];
                            ::wsprintf(text, TEXT("TTRec%s: EPG取得中 / %s"), pThis->m_szCaptionSuffix, TEXT(""));
                            pThis->m_notifyIcon.SetText(text);
                        }
                    }
                    else {
                        if (ci.Space != pThis->m_epgCapSpace || ci.Channel != pThis->m_epgCapChannel) {
                            pThis->m_epgCapSpace = ci.Space;
                            pThis->m_epgCapChannel = ci.Channel;
                            pThis->m_epgCapTimeout = pThis->m_pApp->GetVersion() < TVTest::MakeVersion(0,8,0) ? EPGCAP_TIMEOUT_OLD : EPGCAP_TIMEOUT;
                            TCHAR text[128 + ARRAY_SIZE(ci.szChannelName)];
                            ::wsprintf(text, TEXT("TTRec%s: EPG取得中 / %s"), pThis->m_szCaptionSuffix, ci.szChannelName);
                            pThis->m_notifyIcon.SetText(text);
                        }
                    }
                }
                break;
        }
        return 0;
    case WM_DESTROY:
        ::KillTimer(hwnd, FOLLOW_UP_TIMER_ID);
        ::KillTimer(hwnd, CHECK_QUERY_LIST_TIMER_ID);
        ::KillTimer(hwnd, CHECK_RECORDING_TIMER_ID);
        return 0;
    default:
        {
            static UINT msgTaskbarCreated = 0;
            if (!msgTaskbarCreated) {
                msgTaskbarCreated = ::RegisterWindowMessage(TEXT("TaskbarCreated"));
            }
            // シェルが再起動したとき
            if (uMsg == msgTaskbarCreated && pThis->m_notifyIcon.IsShowing()) {
                pThis->m_notifyIcon.Show();
            }
        }
        break;
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
    bool fDriver = m_szDriverName[0] && !::lstrcmpi(::PathFindFileName(driverName), ::PathFindFileName(m_szDriverName));

    LONGLONG adjustDiff;
    {
        CBlockLock lock(&m_totLock);
        DWORD diff = tick - m_totGrabbedTick;
        // 有効なTOT時刻がタイムアウト以内に取得できているか
        m_totIsValid = m_totIsValid && fDriver && diff < TOT_GRAB_TIMEOUT;
        adjustDiff = !m_totIsValid ? localNow - m_totAdjustedNow/*ローカル方向に補正*/ :
                     m_totGrabbedTime - m_totAdjustedNow + diff * FILETIME_MILLISECOND/*TOT方向に補正*/;
    }
    // メソッド呼び出しのたびに、進める方向に最大4秒、遅らせる方向に最大1秒、それぞれ補正する
    // 進める方向にはより速く補正する(PC内部時計は遅れる場合が多いのと、遅れは録画失敗につながる場合が多いため)
    m_totAdjustedNow += min(max(adjustDiff, -FILETIME_SECOND), 4 * FILETIME_SECOND);

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
        // 大きく補正されている間は出力
        TCHAR text[256];
        ::wsprintf(text, TEXT("CTTRec::UpdateTotAdjust(): d_target=%dmsec,d_local=%dmsec\n"),
                   (int)(adjustDiff / FILETIME_MILLISECOND),
                   (int)((m_totAdjustedNow - localNow) / FILETIME_MILLISECOND));
        DEBUG_OUT(text);
    }
#endif
}


// TOT時刻を取得するストリームコールバック(別スレッド)
BOOL CALLBACK CTTRec::StreamCallback(BYTE *pData, void *pClientData)
{
    int pid = ((pData[1]&0x1f)<<8) | pData[2];
    if (pid != 0x14) return TRUE;

    int unitStartIndicator = (pData[1]>>6)&0x01;
    int adaptationControl  = (pData[3]>>4)&0x03;
    if (!unitStartIndicator ||
        adaptationControl == 0 || adaptationControl == 2) return TRUE;

    BYTE *pPayload = pData + 4;
    if (adaptationControl == 3) {
        // アダプテーションフィールドをスキップする
        int adaptationLength = pData[4];
        if (adaptationLength > 182) return TRUE;
        pPayload += 1 + adaptationLength;
    }

    int pointerField = pPayload[0];
    BYTE *pTable = pPayload + 1 + pointerField;
    if (pTable + 7 >= pData + 188) return TRUE;

    int tableID = pTable[0];
    // TOT or TDT (ARIB STD-B10)
    if (tableID != 0x73 && tableID != 0x70) return TRUE;

    // TOTパケットは地上波の実測で6秒に1個程度
    // ARIB規格では最低30秒に1個

    CTTRec *pThis = static_cast<CTTRec*>(pClientData);

    // TOT時刻とTickカウントを記録する
    CBlockLock lock(&pThis->m_totLock);
    SYSTEMTIME totSysTime;
    if (AribToSystemTime(&pTable[3], &totSysTime) &&
        ::SystemTimeToFileTime(&totSysTime, &pThis->m_totGrabbedTime))
    {
        // バッファがあるので少し時刻を戻す(TVTest_0.7.19r2_Src/TVTest.cpp参考)
        pThis->m_totGrabbedTime += -2000 * FILETIME_MILLISECOND;
        pThis->m_totGrabbedTick = ::GetTickCount();
        pThis->m_totIsValid = true;
    }
    return TRUE;
}


TVTest::CTVTestPlugin *CreatePluginClass()
{
    return new CTTRec;
}
