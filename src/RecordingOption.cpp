#include <Windows.h>
#include <Shlwapi.h>
#include "resource.h"
#include "Util.h"
#include "RecordingOption.h"

static LPCTSTR INITIAL_SAVE_NAME = TEXT("%date%%hour2%%minute2%_%event-name%.ts");

bool RECORDING_OPTION::FromString(LPCTSTR str)
{
    startMargin = ::StrToInt(str);
    if (startMargin < 0 || startMargin > MARGIN_MAX) startMargin = -1;
    if (!NextToken(&str)) return false;

    // 互換のための変換あり!
    endMargin = ::StrToInt(str);
    if (endMargin < 0 || endMargin > MARGIN_MAX * 2) endMargin = MARGIN_DEFAULT;
    else if (endMargin > MARGIN_MAX) endMargin = MARGIN_MAX - endMargin;
    if (!NextToken(&str)) return false;

    // 互換のための変換あり!
    priority = static_cast<BYTE>(::StrToInt(str) + PRIORITY_MOD);
    if (priority >= PRIORITY_MOD * 2) priority = PRIORITY_MOD + PRIORITY_DEFAULT;
    if (!NextToken(&str)) return false;

    onStopped = static_cast<BYTE>(::StrToInt(str));
    if (onStopped >= ON_STOPPED_MAX) onStopped = ON_STOPPED_DEFAULT;
    if (!NextToken(&str)) return false;

    GetToken(str, saveDir, ARRAY_SIZE(saveDir));
    if (!::lstrcmp(saveDir, TEXT("*"))) saveDir[0] = 0;
    if (!NextToken(&str)) return false;

    GetToken(str, saveName, ARRAY_SIZE(saveName));
    if (!::lstrcmp(saveName, TEXT("*"))) saveName[0] = 0;

    if (NextToken(&str)) {
        startTrim = ::StrToInt(str);
        if (startTrim < 0) startTrim = 0;
        if (!NextToken(&str)) return false;

        endTrim = ::StrToInt(str);
        if (endTrim < 0) endTrim = 0;
    }
    else {
        // ver.0.7以前との互換のため
        startTrim = endTrim = 0;
    }
    return true;
}

// saveDirはセットしないので注意
void RECORDING_OPTION::LoadDefaultSetting(LPCTSTR fileName)
{
    startMargin = ::GetPrivateProfileInt(TEXT("DefaultRec"), TEXT("StartMargin"), 30, fileName);
    if (startMargin < 0 || startMargin > MARGIN_MAX) startMargin = 30;

    // 互換のための変換あり!
    endMargin = ::GetPrivateProfileInt(TEXT("DefaultRec"), TEXT("EndMargin"), 30, fileName);
    if (endMargin < 0 || endMargin > MARGIN_MAX * 2) endMargin = 30;
    else if (endMargin > MARGIN_MAX) endMargin = MARGIN_MAX - endMargin;

    // 互換のための変換あり!
    priority = static_cast<BYTE>(GetPrivateProfileSignedInt(TEXT("DefaultRec"), TEXT("Priority"), PRIORITY_NORMAL, fileName) + PRIORITY_MOD);
    if (priority % PRIORITY_MOD == PRIORITY_DEFAULT || priority >= PRIORITY_MOD * 2) priority = PRIORITY_MOD + PRIORITY_NORMAL;

    onStopped = static_cast<BYTE>(::GetPrivateProfileInt(TEXT("DefaultRec"), TEXT("OnStopped"), ON_STOPPED_NONE, fileName));
    if (onStopped == ON_STOPPED_DEFAULT || onStopped >= ON_STOPPED_MAX) onStopped = ON_STOPPED_NONE;

    ::GetPrivateProfileString(TEXT("DefaultRec"), TEXT("SaveName"), TEXT(""), saveName, ARRAY_SIZE(saveName), fileName);
    if (!saveName[0]) ::lstrcpy(saveName, INITIAL_SAVE_NAME);

    startTrim = endTrim = 0;
}

// strには少なくとも600要素の確保が必要
void RECORDING_OPTION::ToString(LPTSTR str) const
{
    ::wsprintf(str, TEXT("%d\t%d\t%d\t%d\t%s\t%s\t%d\t%d"),
               startMargin,
               endMargin == MARGIN_DEFAULT ? -1 : endMargin < 0 ? MARGIN_MAX - endMargin : endMargin,
               priority - PRIORITY_MOD, onStopped,
               saveDir[0] ? saveDir : TEXT("*"),
               saveName[0] ? saveName : TEXT("*"),
               startTrim, endTrim);
}

void RECORDING_OPTION::SaveDefaultSetting(LPCTSTR fileName) const
{
    WritePrivateProfileInt(TEXT("DefaultRec"), TEXT("StartMargin"), startMargin, fileName);
    WritePrivateProfileInt(TEXT("DefaultRec"), TEXT("EndMargin"), endMargin < 0 ? MARGIN_MAX - endMargin : endMargin, fileName);
    WritePrivateProfileInt(TEXT("DefaultRec"), TEXT("Priority"), priority - PRIORITY_MOD, fileName);
    WritePrivateProfileInt(TEXT("DefaultRec"), TEXT("OnStopped"), onStopped, fileName);
    ::WritePrivateProfileString(TEXT("DefaultRec"), TEXT("SaveName"), saveName, fileName);
}

void RECORDING_OPTION::SetEmpty(bool fViewOnly)
{
    startMargin = 0;
    endMargin   = 0;
    priority    = (fViewOnly ? 0 : PRIORITY_MOD) + PRIORITY_NORMAL;
    onStopped   = ON_STOPPED_NONE;
    saveDir[0]  = 0;
    saveName[0] = 0;
    startTrim   = 0;
    endTrim     = 0;
}

void RECORDING_OPTION::SetDefault(bool fViewOnly)
{
    startMargin = -1;
    endMargin   = MARGIN_DEFAULT;
    priority    = (fViewOnly ? 0 : PRIORITY_MOD) + PRIORITY_DEFAULT;
    onStopped   = ON_STOPPED_DEFAULT;
    saveDir[0]  = 0;
    saveName[0] = 0;
    startTrim   = 0;
    endTrim     = 0;
}

void RECORDING_OPTION::ApplyDefault(const RECORDING_OPTION &defaultOption)
{
    if (startMargin < 0) startMargin = defaultOption.startMargin;
    if (endMargin == MARGIN_DEFAULT) endMargin = defaultOption.endMargin;
    if (priority % PRIORITY_MOD == PRIORITY_DEFAULT) priority += defaultOption.priority % PRIORITY_MOD;
    if (onStopped == ON_STOPPED_DEFAULT) onStopped = defaultOption.onStopped;
    if (!saveDir[0]) ::lstrcpy(saveDir, defaultOption.saveDir);
    if (!saveName[0]) ::lstrcpy(saveName, defaultOption.saveName);
}

// ダイアログの"録画設定"部分のプロシージャ
// hasDefaultの場合、各コントロールの選択肢に"デフォルト"が追加される
// pDefaultOptionはhasDefaultかつuMsg==WM_INITDIALOGの時だけ必要
INT_PTR RECORDING_OPTION::DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, bool hasDefault, const RECORDING_OPTION *pDefaultOption)
{
    switch (uMsg) {
    case WM_INITDIALOG:
        {
            if (hasDefault) {
                if (startMargin >= 0) {
                    ::CheckDlgButton(hDlg, IDC_CHECK_STA_M, BST_CHECKED);
                    ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_STA_M), TRUE);
                }
                if (endMargin != MARGIN_DEFAULT) {
                    ::CheckDlgButton(hDlg, IDC_CHECK_END_M, BST_CHECKED);
                    ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_END_M), TRUE);
                }
                if (saveDir[0]) {
                    ::CheckDlgButton(hDlg, IDC_CHECK_SAVE_DIR, BST_CHECKED);
                    ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_SAVE_DIR), TRUE);
                    ::EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_DIR_BROWSE), TRUE);
                }
                if (saveName[0]) {
                    ::CheckDlgButton(hDlg, IDC_CHECK_SAVE_NAME, BST_CHECKED);
                    ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_SAVE_NAME), TRUE);
                }
            }

            ::SetDlgItemInt(hDlg, IDC_EDIT_STA_M,
                hasDefault && startMargin < 0 ? pDefaultOption->startMargin : startMargin, FALSE);

            ::SetDlgItemInt(hDlg, IDC_EDIT_END_M,
                hasDefault && endMargin == MARGIN_DEFAULT ? pDefaultOption->endMargin : endMargin, TRUE);

            ::CheckDlgButton(hDlg, IDC_CHECK_VIEW_ONLY, IsViewOnly() ? BST_CHECKED : BST_UNCHECKED);

            LPCTSTR pPriList[] = { TEXT("デフォルト"), TEXT("1--"), TEXT("2-"), TEXT("3"), TEXT("4+"), TEXT("5++") };
            SetComboBoxList(hDlg, IDC_COMBO_PRI, pPriList + (hasDefault ? 0 : 1), ARRAY_SIZE(pPriList) - (hasDefault ? 0 : 1));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_PRI, CB_SETCURSEL, priority % PRIORITY_MOD - (hasDefault ? 0 : 1), 0);

            LPCTSTR pOnStopList[] = { TEXT("デフォルト"), TEXT("何もしない"), TEXT("TVTestを終了"), TEXT("サスペンド"), TEXT("休止状態"),
                                      TEXT("待機+何もしない"), TEXT("待機+TVTestを終了"), TEXT("待機+サスペンド"), TEXT("待機+休止状態") };
            SetComboBoxList(hDlg, IDC_COMBO_ONSTOP, pOnStopList + (hasDefault ? 0 : 1), ARRAY_SIZE(pOnStopList) - (hasDefault ? 0 : 1));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_ONSTOP, CB_SETCURSEL, onStopped - (hasDefault ? 0 : 1), 0);

            // フォルダ選択コントロールはhasDefaultのときしか存在しない
            if (hasDefault) {
                ::SetDlgItemText(hDlg, IDC_EDIT_SAVE_DIR, !saveDir[0] ? pDefaultOption->saveDir : saveDir);
                ::SendDlgItemMessage(hDlg, IDC_EDIT_SAVE_DIR, EM_LIMITTEXT, ARRAY_SIZE(saveDir) - 1, 0);
            }
            ::SetDlgItemText(hDlg, IDC_EDIT_SAVE_NAME, hasDefault && !saveName[0] ? pDefaultOption->saveName : saveName);
            ::SendDlgItemMessage(hDlg, IDC_EDIT_SAVE_NAME, EM_LIMITTEXT, ARRAY_SIZE(saveName) - 1, 0);

            if (hasDefault) {
                ::SetDlgItemInt(hDlg, IDC_EDIT_STA_TRIM, startTrim / 60, FALSE);
                ::SetDlgItemInt(hDlg, IDC_EDIT_END_TRIM, endTrim / 60, FALSE);
            }
        }
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHECK_STA_M:
            ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_STA_M), ::IsDlgButtonChecked(hDlg, IDC_CHECK_STA_M) == BST_CHECKED);
            return TRUE;
        case IDC_CHECK_END_M:
            ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_END_M), ::IsDlgButtonChecked(hDlg, IDC_CHECK_END_M) == BST_CHECKED);
            return TRUE;
        case IDC_CHECK_SAVE_DIR:
            ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_SAVE_DIR), ::IsDlgButtonChecked(hDlg, IDC_CHECK_SAVE_DIR) == BST_CHECKED);
            ::EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_DIR_BROWSE), ::IsDlgButtonChecked(hDlg, IDC_CHECK_SAVE_DIR) == BST_CHECKED);
            return TRUE;
        case IDC_CHECK_SAVE_NAME:
            ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_SAVE_NAME), ::IsDlgButtonChecked(hDlg, IDC_CHECK_SAVE_NAME) == BST_CHECKED);
            return TRUE;
        case IDC_BUTTON_SAVE_DIR_BROWSE:
            TCHAR dir[MAX_PATH];
            if (!::GetDlgItemText(hDlg,IDC_EDIT_SAVE_DIR, dir, ARRAY_SIZE(dir))) dir[0] = 0;
            if (BrowseFolderDialog(hDlg, dir, TEXT("録画ファイルの保存先フォルダ:")))
                ::SetDlgItemText(hDlg, IDC_EDIT_SAVE_DIR, dir);
            return TRUE;
        case IDOK:
            startMargin = !hasDefault || ::IsDlgButtonChecked(hDlg, IDC_CHECK_STA_M) == BST_CHECKED ?
                          ::GetDlgItemInt(hDlg, IDC_EDIT_STA_M, NULL, FALSE) : -1;
            if (startMargin > MARGIN_MAX) startMargin = MARGIN_MAX;

            endMargin = !hasDefault || ::IsDlgButtonChecked(hDlg, IDC_CHECK_END_M) == BST_CHECKED ?
                        ::GetDlgItemInt(hDlg, IDC_EDIT_END_M, NULL, TRUE) : MARGIN_DEFAULT;
            if (endMargin > MARGIN_MAX) endMargin = MARGIN_MAX;
            else if (endMargin != MARGIN_DEFAULT && endMargin < -MARGIN_MAX) endMargin = -MARGIN_MAX;

            {
                LRESULT res = ::SendDlgItemMessage(hDlg, IDC_COMBO_PRI, CB_GETCURSEL, 0, 0);
                priority = res < 0 ? PRIORITY_NORMAL : static_cast<BYTE>(res + (hasDefault ? 0 : 1));
                if (::IsDlgButtonChecked(hDlg, IDC_CHECK_VIEW_ONLY) != BST_CHECKED) priority += PRIORITY_MOD;
            }
            {
                LRESULT res = ::SendDlgItemMessage(hDlg, IDC_COMBO_ONSTOP, CB_GETCURSEL, 0, 0);
                onStopped = res < 0 ? ON_STOPPED_NONE : static_cast<BYTE>(res + (hasDefault ? 0 : 1));
            }
            if (hasDefault) {
                saveDir[0] = 0;
                if (::IsDlgButtonChecked(hDlg, IDC_CHECK_SAVE_DIR) == BST_CHECKED)
                    if (!::GetDlgItemText(hDlg, IDC_EDIT_SAVE_DIR, saveDir, ARRAY_SIZE(saveDir)))
                        saveDir[0] = 0;
            }
            saveName[0] = 0;
            if (!hasDefault || ::IsDlgButtonChecked(hDlg, IDC_CHECK_SAVE_NAME) == BST_CHECKED)
                if (!::GetDlgItemText(hDlg, IDC_EDIT_SAVE_NAME, saveName, ARRAY_SIZE(saveName)))
                    saveName[0] = 0;
            // !hasDefaultのとき空文字にはできない
            if (!hasDefault && !saveName[0]) ::lstrcpy(saveName, INITIAL_SAVE_NAME);

            if (hasDefault) {
                startTrim = ::GetDlgItemInt(hDlg, IDC_EDIT_STA_TRIM, NULL, FALSE) * 60;
                endTrim = ::GetDlgItemInt(hDlg, IDC_EDIT_END_TRIM, NULL, FALSE) * 60;
            }
            return TRUE;
        }
    }
    return FALSE;
}
