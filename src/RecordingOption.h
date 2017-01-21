#ifndef INCLUDE_RECORDING_OPTION_H
#define INCLUDE_RECORDING_OPTION_H

#include <Windows.h>

// マージンは0～MARGIN_MAX秒まで設定可能
#define MARGIN_MAX 600

// 録画の優先度
enum {
    PRIORITY_DEFAULT,
    PRIORITY_LOWEST,
    PRIORITY_LOW,
    PRIORITY_NORMAL,
    PRIORITY_HIGH,
    PRIORITY_HIGHEST,
    PRIORITY_MAX
};

// 録画停止後の動作
enum {
    ON_STOPPED_DEFAULT,     // デフォルト設定の動作を行う
    ON_STOPPED_NONE,        // 何もしない
    ON_STOPPED_CLOSE,       // TVTestを終了
    ON_STOPPED_SUSPEND,     // システムサスペンド
    ON_STOPPED_HIBERNATE,   // システム休止
    ON_STOPPED_MAX
};

typedef struct {
    int startMargin;            // 録画開始マージン[秒]
    int endMargin;              // 録画終了マージン[秒]
    BYTE priority;              // 録画の優先度
    BYTE onStopped;             // 録画停止後の動作
    TCHAR saveDir[MAX_PATH];    // 保存ディレクトリ名
    TCHAR saveName[MAX_PATH];   // 保存ファイル名
} RECORDING_OPTION;

namespace RecordingOption {
    extern const RECORDING_OPTION DEFAULT;
    bool FromString(LPCTSTR str, RECORDING_OPTION *pOption);
    void LoadSetting(LPCTSTR fileName, RECORDING_OPTION *pOption);
    void ToString(const RECORDING_OPTION &option, LPTSTR str);
    void SaveSetting(LPCTSTR fileName, const RECORDING_OPTION &option);
    void ApplyDefault(RECORDING_OPTION *pOption, const RECORDING_OPTION &defaultOption);
    INT_PTR DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, RECORDING_OPTION *pOption, bool hasDefault, const RECORDING_OPTION *pDefaultOption = NULL);
}

#endif // INCLUDE_RECORDING_OPTION_H
