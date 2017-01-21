#include <Windows.h>
#include <Shlwapi.h>
#include "resource.h"
#include "Util.h"
#include "RecordingOption.h"

namespace RecordingOption {

const RECORDING_OPTION DEFAULT = { -1, MARGIN_DEFAULT, PRIORITY_MOD + PRIORITY_DEFAULT, ON_STOPPED_DEFAULT, TEXT(""), TEXT("") };

inline bool ViewsOnly(const RECORDING_OPTION &option)
{
    return option.priority < PRIORITY_MOD;
}

bool FromString(LPCTSTR str, RECORDING_OPTION *pOption)
{
    pOption->startMargin = ::StrToInt(str);
    if (pOption->startMargin < 0 || pOption->startMargin > MARGIN_MAX) pOption->startMargin = -1;
    if (!NextToken(&str)) return false;

    int exMargin = ::StrToInt(str);
    pOption->endMargin = exMargin < 0 || exMargin > MARGIN_MAX * 2 ? MARGIN_DEFAULT :
                         exMargin > MARGIN_MAX ? MARGIN_MAX - exMargin : exMargin;
    if (!NextToken(&str)) return false;

    pOption->priority = static_cast<BYTE>(::StrToInt(str) + PRIORITY_MOD);
    if (pOption->priority >= PRIORITY_MOD * 2) pOption->priority = PRIORITY_MOD + PRIORITY_DEFAULT;
    if (!NextToken(&str)) return false;

    pOption->onStopped = static_cast<BYTE>(::StrToInt(str));
    if (pOption->onStopped >= ON_STOPPED_MAX) pOption->onStopped = ON_STOPPED_DEFAULT;
    if (!NextToken(&str)) return false;

    GetToken(str, pOption->saveDir, ARRAY_SIZE(pOption->saveDir));
    if (!::lstrcmp(pOption->saveDir, TEXT("*"))) pOption->saveDir[0] = 0;
    if (!NextToken(&str)) return false;

    GetToken(str, pOption->saveName, ARRAY_SIZE(pOption->saveName));
    if (!::lstrcmp(pOption->saveName, TEXT("*"))) pOption->saveName[0] = 0; 
    return true;
}

void LoadSetting(LPCTSTR fileName, RECORDING_OPTION *pOption)
{
    if (!pOption) return;
    RECORDING_OPTION &opt = *pOption;

    opt.startMargin = ::GetPrivateProfileInt(TEXT("DefaultRec"), TEXT("StartMargin"), 30, fileName);
    if (opt.startMargin < 0 || opt.startMargin > MARGIN_MAX) opt.startMargin = 30;

    int exMargin = ::GetPrivateProfileInt(TEXT("DefaultRec"), TEXT("EndMargin"), 30, fileName);
    opt.endMargin = exMargin < 0 || exMargin > MARGIN_MAX * 2 ? 30 :
                    exMargin > MARGIN_MAX ? MARGIN_MAX - exMargin : exMargin;

    opt.priority = static_cast<BYTE>(::GetPrivateProfileInt(TEXT("DefaultRec"), TEXT("Priority"), PRIORITY_NORMAL, fileName));
    if (opt.priority >= PRIORITY_MOD) opt.priority = PRIORITY_NORMAL;

    opt.onStopped = static_cast<BYTE>(::GetPrivateProfileInt(TEXT("DefaultRec"), TEXT("OnStopped"), ON_STOPPED_NONE, fileName));
    if (opt.onStopped >= ON_STOPPED_MAX) opt.onStopped = ON_STOPPED_NONE;

    ::GetPrivateProfileString(TEXT("DefaultRec"), TEXT("SaveName"), TEXT("%date%%hour2%%minute2%_%event-name%.ts"),
                              opt.saveName, ARRAY_SIZE(opt.saveName), fileName);
}

void ToString(const RECORDING_OPTION &option, LPTSTR str)
{
    ::wsprintf(str, TEXT("%d\t%d\t%d\t%d\t%s\t%s"),
        option.startMargin,
        option.endMargin == MARGIN_DEFAULT ? -1 : option.endMargin < 0 ? MARGIN_MAX - option.endMargin : option.endMargin,
        static_cast<int>(option.priority) - PRIORITY_MOD,
        static_cast<int>(option.onStopped),
        option.saveDir[0] ? option.saveDir : TEXT("*"),
        option.saveName[0] ? option.saveName : TEXT("*"));
}

void SaveSetting(LPCTSTR fileName, const RECORDING_OPTION &option)
{
    WritePrivateProfileInt(TEXT("DefaultRec"), TEXT("StartMargin"), option.startMargin, fileName);
    WritePrivateProfileInt(TEXT("DefaultRec"), TEXT("EndMargin"),
                           option.endMargin == MARGIN_DEFAULT ? -1 : option.endMargin < 0 ? MARGIN_MAX - option.endMargin : option.endMargin,
                           fileName);
    WritePrivateProfileInt(TEXT("DefaultRec"), TEXT("Priority"), option.priority, fileName);
    WritePrivateProfileInt(TEXT("DefaultRec"), TEXT("OnStopped"), option.onStopped, fileName);
    ::WritePrivateProfileString(TEXT("DefaultRec"), TEXT("SaveName"), option.saveName, fileName);
}

void ApplyDefault(RECORDING_OPTION *pOption, const RECORDING_OPTION &defaultOption)
{
    if (pOption->startMargin < 0) pOption->startMargin = defaultOption.startMargin;
    if (pOption->endMargin == MARGIN_DEFAULT) pOption->endMargin = defaultOption.endMargin;

    if (pOption->priority % PRIORITY_MOD == PRIORITY_DEFAULT) pOption->priority += defaultOption.priority;
    if (pOption->onStopped == ON_STOPPED_DEFAULT) pOption->onStopped = defaultOption.onStopped;

    if (!pOption->saveDir[0]) ::lstrcpy(pOption->saveDir, defaultOption.saveDir);
    if (!pOption->saveName[0]) ::lstrcpy(pOption->saveName, defaultOption.saveName);
}

// ダイアログの"録画設定"部分のプロシージャ
// hasDefaultの場合、各コントロールの選択肢に"デフォルト"が追加される
// pDefaultOptionはhasDefaultかつuMsg==WM_INITDIALOGの時だけ必要
INT_PTR DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, RECORDING_OPTION *pOption, bool hasDefault, const RECORDING_OPTION *pDefaultOption)
{
    if (!pOption) return FALSE;

    switch (uMsg) {
    case WM_INITDIALOG:
        {
            if (hasDefault) {
                if (pOption->startMargin >= 0) {
                    ::CheckDlgButton(hDlg, IDC_CHECK_STA_M, BST_CHECKED);
                    ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_STA_M), TRUE);
                }
                if (pOption->endMargin != MARGIN_DEFAULT) {
                    ::CheckDlgButton(hDlg, IDC_CHECK_END_M, BST_CHECKED);
                    ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_END_M), TRUE);
                }
                if (pOption->saveDir[0]) {
                    ::CheckDlgButton(hDlg, IDC_CHECK_SAVE_DIR, BST_CHECKED);
                    ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_SAVE_DIR), TRUE);
                }
                if (pOption->saveName[0]) {
                    ::CheckDlgButton(hDlg, IDC_CHECK_SAVE_NAME, BST_CHECKED);
                    ::EnableWindow(GetDlgItem(hDlg, IDC_EDIT_SAVE_NAME), TRUE);
                }
            }

            ::SetDlgItemInt(hDlg, IDC_EDIT_STA_M,
                hasDefault && pOption->startMargin < 0 ? pDefaultOption->startMargin : pOption->startMargin, FALSE);

            ::SetDlgItemInt(hDlg, IDC_EDIT_END_M,
                hasDefault && pOption->endMargin == MARGIN_DEFAULT ? pDefaultOption->endMargin : pOption->endMargin, TRUE);

            if (hasDefault) {
                ::CheckDlgButton(hDlg, IDC_CHECK_VIEW_ONLY, ViewsOnly(*pOption) ? BST_CHECKED : BST_UNCHECKED);
            }
            LPCTSTR pPriList[] = { TEXT("デフォルト"), TEXT("1--"), TEXT("2-"), TEXT("3"), TEXT("4+"), TEXT("5++") };
            SetComboBoxList(hDlg, IDC_COMBO_PRI, pPriList + (hasDefault ? 0 : 1), ARRAY_SIZE(pPriList) - (hasDefault ? 0 : 1));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_PRI, CB_SETCURSEL, pOption->priority % PRIORITY_MOD - (hasDefault ? 0 : 1), 0);

            LPCTSTR pOnStopList[] = { TEXT("デフォルト"), TEXT("何もしない"), TEXT("TVTestを終了"), TEXT("サスペンド"), TEXT("休止状態") };
            SetComboBoxList(hDlg, IDC_COMBO_ONSTOP, pOnStopList + (hasDefault ? 0 : 1), ARRAY_SIZE(pOnStopList) - (hasDefault ? 0 : 1));
            ::SendDlgItemMessage(hDlg, IDC_COMBO_ONSTOP, CB_SETCURSEL, pOption->onStopped - (hasDefault ? 0 : 1), 0);

            // フォルダ選択コントロールはhasDefaultのときしか存在しない
            if (hasDefault) {
                ::SetDlgItemText(hDlg, IDC_EDIT_SAVE_DIR, !pOption->saveDir[0] ? pDefaultOption->saveDir : pOption->saveDir);
                ::SendDlgItemMessage(hDlg, IDC_EDIT_SAVE_DIR, EM_LIMITTEXT, ARRAY_SIZE(pOption->saveDir) - 1, 0);
            }
            ::SetDlgItemText(hDlg, IDC_EDIT_SAVE_NAME, hasDefault && !pOption->saveName[0] ? pDefaultOption->saveName : pOption->saveName);
            ::SendDlgItemMessage(hDlg, IDC_EDIT_SAVE_NAME, EM_LIMITTEXT, ARRAY_SIZE(pOption->saveName) - 1, 0);
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
            pOption->startMargin = !hasDefault || ::IsDlgButtonChecked(hDlg, IDC_CHECK_STA_M) == BST_CHECKED ?
                                   ::GetDlgItemInt(hDlg, IDC_EDIT_STA_M, NULL, FALSE) : -1;
            if (pOption->startMargin > MARGIN_MAX) pOption->startMargin = MARGIN_MAX;

            pOption->endMargin = !hasDefault || ::IsDlgButtonChecked(hDlg, IDC_CHECK_END_M) == BST_CHECKED ?
                                 ::GetDlgItemInt(hDlg, IDC_EDIT_END_M, NULL, TRUE) : MARGIN_DEFAULT;
            if (pOption->endMargin > MARGIN_MAX) pOption->endMargin = MARGIN_MAX;
            else if (pOption->endMargin != MARGIN_DEFAULT && pOption->endMargin < -MARGIN_MAX) pOption->endMargin = -MARGIN_MAX;

            pOption->priority = static_cast<BYTE>(::SendDlgItemMessage(hDlg, IDC_COMBO_PRI, CB_GETCURSEL, 0, 0) + (hasDefault ? 0 : 1));
            if (hasDefault && ::IsDlgButtonChecked(hDlg, IDC_CHECK_VIEW_ONLY) != BST_CHECKED) pOption->priority += PRIORITY_MOD;

            pOption->onStopped = static_cast<BYTE>(::SendDlgItemMessage(hDlg, IDC_COMBO_ONSTOP, CB_GETCURSEL, 0, 0) + (hasDefault ? 0 : 1));

            if (hasDefault) {
                pOption->saveDir[0] = 0;
                if (::IsDlgButtonChecked(hDlg, IDC_CHECK_SAVE_DIR) == BST_CHECKED)
                    if (!::GetDlgItemText(hDlg, IDC_EDIT_SAVE_DIR, pOption->saveDir, ARRAY_SIZE(pOption->saveDir)))
                        pOption->saveDir[0] = 0;
            }
            pOption->saveName[0] = 0;
            if (!hasDefault || ::IsDlgButtonChecked(hDlg, IDC_CHECK_SAVE_NAME) == BST_CHECKED)
                if (!::GetDlgItemText(hDlg, IDC_EDIT_SAVE_NAME, pOption->saveName, ARRAY_SIZE(pOption->saveName)))
                    pOption->saveName[0] = 0;

            return TRUE;
        }
    }
    return FALSE;
}

} // namespace RecordingOption
