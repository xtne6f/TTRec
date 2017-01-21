#ifndef INCLUDE_RECORDING_OPTION_H
#define INCLUDE_RECORDING_OPTION_H

// マージンは0～MARGIN_MAX秒まで設定可能
// (終了マージンは-MARGIN_MAX～MARGIN_MAX秒)
#define MARGIN_MAX 600
#define MARGIN_DEFAULT INT_MIN

// 録画の優先度
enum {
    PRIORITY_DEFAULT,
    PRIORITY_LOWEST,
    PRIORITY_LOW,
    PRIORITY_NORMAL,
    PRIORITY_HIGH,
    PRIORITY_HIGHEST,
    PRIORITY_MOD
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

struct RECORDING_OPTION {
    int startMargin;            // 録画開始マージン[秒]
    int endMargin;              // 録画終了マージン[秒]
    BYTE priority;              // 録画の優先度(<PRIORITY_MOD:見るだけレベル)
    BYTE onStopped;             // 録画停止後の動作
    TCHAR saveDir[MAX_PATH];    // 保存ディレクトリ名
    TCHAR saveName[MAX_PATH];   // 保存ファイル名
    bool IsViewOnly() const { return priority < PRIORITY_MOD; }
    bool FromString(LPCTSTR str);
    void LoadDefaultSetting(LPCTSTR fileName);
    void ToString(LPTSTR str) const;
    void SaveDefaultSetting(LPCTSTR fileName) const;
    void SetDefault(bool fViewOnly);
    void ApplyDefault(const RECORDING_OPTION &defaultOption);
    INT_PTR DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, bool hasDefault, const RECORDING_OPTION *pDefaultOption = NULL);
};

#endif // INCLUDE_RECORDING_OPTION_H
