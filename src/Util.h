#ifndef INCLUDE_UTIL_H
#define INCLUDE_UTIL_H

#define MODULE_ID   TEXT("TTREC-MOD-2D0BE1F1-C49D-428F-A286-51EA53F38B5F")
#define SUSPEND_ID  TEXT("TTREC-SUS-2D0BE1F1-C49D-428F-A286-51EA53F38B5F")

#define CMD_OPTION_MAX      512
#define EVENT_NAME_MAX      128
#define MATCH_PATTERN_MAX   128
// メニューリストの最大項目数(実用上現実的な数)
#define MENULIST_MAX        100
// タスクトリガ設定の最大個数
#define TASK_TRIGGER_MAX    20
// NewReadTextFileToEnd()の最大ファイルサイズ
#define READ_FILE_MAX_SIZE  (4096 * 1024)

#define FILETIME_MILLISECOND    10000LL
#define FILETIME_SECOND         (1000LL*FILETIME_MILLISECOND)
#define FILETIME_MINUTE         (60LL*FILETIME_SECOND)
#define FILETIME_HOUR           (60LL*FILETIME_MINUTE)

#ifdef _DEBUG
#define DEBUG_OUT(x) ::OutputDebugString(x)
#else
#define DEBUG_OUT(x)
#endif

#define ARRAY_SIZE(a) _countof(a)

#ifdef NO_CRT
#undef RtlFillMemory
#undef RtlZeroMemory
#undef RtlMoveMemory
#undef RtlCopyMemory
EXTERN_C NTSYSAPI VOID NTAPI RtlFillMemory(VOID UNALIGNED *Dest, SIZE_T Length, BYTE Pattern);
EXTERN_C NTSYSAPI VOID NTAPI RtlZeroMemory(VOID UNALIGNED *Dest, SIZE_T Length);
EXTERN_C NTSYSAPI VOID NTAPI RtlMoveMemory(VOID UNALIGNED *Dest, CONST VOID UNALIGNED *Src, SIZE_T Length);
#define RtlCopyMemory RtlMoveMemory

int _purecall(void);
void *operator new(size_t size);
void *operator new[](size_t size);
void operator delete(void *ptr);
void operator delete[](void *ptr);
#endif

DWORD GetLongModuleFileName(HMODULE hModule, LPTSTR lpFileName, DWORD nSize);
bool GetIdentifierFromModule(HMODULE hModule, LPTSTR name, DWORD max);
HANDLE CreateFullAccessMutex(BOOL bInitialOwner, LPCTSTR name);
void WriteFileForSpinUp(LPCTSTR dirName);
WCHAR *NewReadTextFileToEnd(LPCTSTR fileName, DWORD dwShareMode);
bool IsMatch(LPCTSTR str, LPCTSTR patterns);
bool GetRundll32Path(LPTSTR rundllPath);
void GetToken(LPCTSTR str, LPTSTR token, int max);
TCHAR NextToken(LPCTSTR *str);
void ReplaceTokenDelimiters(LPTSTR str);

bool FlagStrToArray(LPCTSTR str, bool *flags, int len);
void FlagArrayToStr(const bool *flags, LPTSTR str, int len);
bool StrToTimeSpan(LPCTSTR str, int *pSpan);
void TimeSpanToStr(int span, LPTSTR str);
bool StrToFileTime(LPCTSTR str, FILETIME *pTime);
void FileTimeToStr(const FILETIME *pTime, LPTSTR str);

FILETIME &operator+=(FILETIME &ft,LONGLONG Offset);
LONGLONG operator-(const FILETIME &ft1,const FILETIME &ft2);
void GetLocalTimeAsFileTime(FILETIME *pTime);
int CalcDayOfWeek(int Year,int Month,int Day);
LPCTSTR GetDayOfWeekText(int DayOfWeek);
bool BrowseFolderDialog(HWND hwndOwner,LPTSTR pszDirectory,LPCTSTR pszTitle);

void SetComboBoxList(HWND hDlg,int ID,LPCTSTR const *pList,int Length);
BOOL WritePrivateProfileInt(LPCTSTR pszSection,LPCTSTR pszKey,int Value,LPCTSTR pszFileName);

const bool AribToSystemTime(const BYTE *pHexData, SYSTEMTIME *pSysTime);
void SplitAribMjd(const WORD wAribMjd, WORD *pwYear, WORD *pwMonth, WORD *pwDay, WORD *pwDayOfWeek);
void SplitAribBcd(const BYTE *pAribBcd, WORD *pwHour, WORD *pwMinute, WORD *pwSecond);

int FormatFileName(LPTSTR pszFileName, int MaxFileName, WORD EventID, FILETIME StartTimeSpec, LPCTSTR pszEventName, LPCTSTR pszFormat);
int FormatEventName(LPTSTR pszEventName, int MaxEventName, int num, LPCTSTR pszFormat);

class CCriticalLock
{
public:
	CCriticalLock();
	virtual ~CCriticalLock();
	void Lock(void);
	void Unlock(void);
	//bool TryLock(DWORD TimeOut=0);
private:
	CRITICAL_SECTION m_CriticalSection;
};

class CBlockLock
{
public:
	CBlockLock(CCriticalLock *pCriticalLock);
	virtual ~CBlockLock();
private:
	CCriticalLock *m_pCriticalLock;
};

class CBalloonTip
{
	HWND m_hwndToolTips;
	HWND m_hwndOwner;

public:
	CBalloonTip();
	~CBalloonTip();
	bool Initialize(HWND hwnd, HMODULE hModule);
	void Finalize();
	enum {
		ICON_NONE,
		ICON_INFO,
		ICON_WARNING,
		ICON_ERROR
	};
	bool Show(LPCTSTR pszText,LPCTSTR pszTitle,const POINT *pPos,int Icon=ICON_NONE);
	bool Hide();
	HWND GetHandle() const { return m_hwndToolTips; }
};

#endif // INCLUDE_UTIL_H
