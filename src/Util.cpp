#include <Windows.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include "Util.h"

// CRT非依存のためにはさらに /NODEFAULTLIB /GR- /GS- /GL削除 /EHsc削除 が必要
#ifdef NO_CRT
#pragma function(memset)
#pragma function(memcpy)

void * __cdecl memset(void *Dest, int Pattern, size_t Length)
{
    FillMemory(Dest, Length, (BYTE)Pattern);
    return Dest;
}

void * __cdecl memcpy(void *Dest, const void *Src, size_t Length)
{
    CopyMemory(Dest, Src, Length);
    return Dest;
}

// http://support.microsoft.com/kb/401983/
int _purecall(void) { return 0; }

// new/deleteの置き換え
void *operator new(size_t size)
{
    return ::HeapAlloc(::GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, size);
}

void *operator new[](size_t size)
{
    return operator new(size);
}

void operator delete(void *ptr)
{
    if (ptr) ::HeapFree(::GetProcessHeap(), 0, ptr);
}

void operator delete[](void *ptr)
{
    operator delete(ptr);
}
#endif

DWORD GetLongModuleFileName(HMODULE hModule, LPTSTR lpFileName, DWORD nSize)
{
    TCHAR longOrShortName[MAX_PATH];
    // ロングパスとは限らない
    if (!::GetModuleFileName(hModule, longOrShortName, MAX_PATH)) return 0;
    DWORD rv = ::GetLongPathName(longOrShortName, lpFileName, nSize);
    return rv >= nSize ? 0 : rv;
}

bool GetIdentifierFromModule(HMODULE hModule, LPTSTR name, DWORD max)
{
    if (!GetLongModuleFileName(hModule, name, max)) return false;
    ::CharUpperBuff(name, ::lstrlen(name));
    for (TCHAR *p = name; *p; p++) if (*p == TEXT('\\')) *p = TEXT('/');
    return true;
}

HANDLE CreateFullAccessMutex(BOOL bInitialOwner, LPCTSTR name)
{
    SECURITY_DESCRIPTOR sd = {0};
    ::InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    ::SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;
    return ::CreateMutex(&sa, bInitialOwner, name);
}

// スピンアップのために適当なファイルを作成して削除する
// 同名のファイルが既に存在すれば何もしない
void WriteFileForSpinUp(LPCTSTR dirName)
{
    TCHAR path[MAX_PATH];
    if (!::PathCombine(path, dirName, TEXT("_SPINUP_"))) return;

    HANDLE hFile = ::CreateFile(path, GENERIC_WRITE, 0, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    BYTE buf[512] = {0};
    DWORD written;
    ::WriteFile(hFile, buf, sizeof(buf), &written, NULL);
    ::CloseHandle(hFile);

    ::Sleep(1);
    ::DeleteFile(path);
}

// BOM付きUTF-16テキストファイルを文字列として全て読む
// 成功するとnewされた配列のポインタが返るので、必ずdeleteすること
WCHAR *NewReadTextFileToEnd(LPCTSTR fileName, DWORD dwShareMode)
{
    HANDLE hFile = ::CreateFile(fileName, GENERIC_READ, dwShareMode, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    WCHAR bom;
    DWORD readBytes;
    DWORD fileBytes = ::GetFileSize(hFile, NULL);
    if (fileBytes < sizeof(WCHAR) || READ_FILE_MAX_SIZE <= fileBytes ||
        !::ReadFile(hFile, &bom, sizeof(WCHAR), &readBytes, NULL) ||
        readBytes != sizeof(WCHAR) || bom != L'\xFEFF')
    {
        ::CloseHandle(hFile);
        return NULL;
    }

    WCHAR *pRet = new WCHAR[fileBytes / sizeof(WCHAR) - 1 + 1];
    if (!::ReadFile(hFile, pRet, fileBytes - sizeof(WCHAR), &readBytes, NULL)) {
        delete [] pRet;
        ::CloseHandle(hFile);
        return NULL;
    }
    pRet[readBytes / sizeof(WCHAR)] = 0;

    ::CloseHandle(hFile);
    return pRet;
}

// 空白文字区切りのパターンに文字列がマッチするか判定する
bool IsMatch(LPCTSTR str, LPCTSTR patterns)
{
    if (!str || !patterns) return false;
    while (*patterns) {
        int len = ::StrCSpn(patterns, TEXT(" 　"));
        // 空白文字の連続や MATCH_PATTERN_MAX 以上の語は無視
        if (1 <= len && len < MATCH_PATTERN_MAX) {
            TCHAR pattern[MATCH_PATTERN_MAX];
            ::lstrcpyn(pattern, patterns, len + 1);
            if (pattern[0] == TEXT('-')) {
                // 除外検索
                if (::StrStrI(str, pattern + 1)) return false;
            }
            else {
                if (!::StrStrI(str, pattern)) return false;
            }
        }
        patterns += patterns[len] ? len + 1 : len;
    }
    return true;
}

bool GetRundll32Path(LPTSTR path)
{
    TCHAR systemDir[MAX_PATH];
    if (!::GetSystemDirectory(systemDir, MAX_PATH)) return false;
    ::PathCombine(path, systemDir, TEXT("rundll32.exe"));
    if (!::PathFileExists(path)) return false;
    return true;
}

// トークン(タブ区切り)を取得
void GetToken(LPCTSTR str, LPTSTR token, int max)
{
    int size = ::StrCSpn(str, TEXT("\t\r\n")) + 1;
    ::lstrcpyn(token, str, size < max ? size : max);
}

// 次のトークンまでポインタを進める
TCHAR NextToken(LPCTSTR *pStr)
{
    *pStr += ::StrCSpn(*pStr, TEXT("\t\r\n"));
    return !**pStr ? 0 : *(*pStr)++;
}

// トークン区切りになる文字を空白文字に置換する
void ReplaceTokenDelimiters(LPTSTR str)
{
    while (*str) {
        str += ::StrCSpn(str, TEXT("\t\r\n"));
        if (*str) *str++ = TEXT(' ');
    }
}

// フラグ文字列"T.TT..."をbool配列に変換
bool FlagStrToArray(LPCTSTR str, bool *flags, int len)
{
    int i = 0;
    for (; i < len && (*str == TEXT('T') || *str == TEXT('.')); i++) {
        flags[i] = str[i] == TEXT('T');
    }
    return i == len;
}

// bool配列をフラグ文字列に変換
void FlagArrayToStr(const bool *flags, LPTSTR str, int len)
{
    for (int i = 0; i < len; i++) {
        str[i] = flags[i] ? TEXT('T') : TEXT('.');
    }
    str[len] = 0;
}

bool StrToTimeSpan(LPCTSTR str, int *pSpan)
{
    // フォーマットは"hh:mm:ss"のみ
    if (lstrlen(str) < 8 || str[2] != TEXT(':') || str[5] != TEXT(':')) return false;

    *pSpan = ::StrToInt(&str[0]) * 60 * 60 + ::StrToInt(&str[3]) * 60 + ::StrToInt(&str[6]);
    return true;
}

void TimeSpanToStr(int span, LPTSTR str)
{
    if (span < 0) span = 0;
    ::wsprintf(str, TEXT("%02d:%02d:%02d"), span / 60 / 60 % 100, span / 60 % 60, span % 60);
}

bool StrToFileTime(LPCTSTR str, FILETIME *pTime)
{
    // フォーマットは"YYYY-MM-DDThh:mm:ss"のみ
    if (lstrlen(str) < 19 || str[4] != TEXT('-') || str[7] != TEXT('-') ||
        str[10] != TEXT('T') || str[13] != TEXT(':') || str[16] != TEXT(':')) return false;

    SYSTEMTIME sysTime;
    sysTime.wYear = (WORD)::StrToInt(&str[0]);
    sysTime.wMonth = (WORD)::StrToInt(&str[5]);
    sysTime.wDay = (WORD)::StrToInt(&str[8]);
    sysTime.wHour = (WORD)::StrToInt(&str[11]);
    sysTime.wMinute = (WORD)::StrToInt(&str[14]);
    sysTime.wSecond = (WORD)::StrToInt(&str[17]);
    sysTime.wMilliseconds = 0;

    if (!::SystemTimeToFileTime(&sysTime, pTime)) return false;
    return true;
}

void FileTimeToStr(const FILETIME *pTime, LPTSTR str)
{
    SYSTEMTIME sysTime;
    if (!::FileTimeToSystemTime(pTime, &sysTime)) {
        ::lstrcpy(str, TEXT("0000-00-00T00:00:00"));
    }
    else {
        ::wsprintf(str, TEXT("%04hu-%02hu-%02huT%02hu:%02hu:%02hu"),
                   sysTime.wYear, sysTime.wMonth, sysTime.wDay,
                   sysTime.wHour, sysTime.wMinute, sysTime.wSecond);
    }
}


#if 1 // From: TVTest_0.7.19r2_Src/Util.cpp

FILETIME &operator+=(FILETIME &ft,LONGLONG Offset)
{
	ULARGE_INTEGER Result;

	Result.LowPart=ft.dwLowDateTime;
	Result.HighPart=ft.dwHighDateTime;
	Result.QuadPart+=Offset;
	ft.dwLowDateTime=Result.LowPart;
	ft.dwHighDateTime=Result.HighPart;
	return ft;
}

LONGLONG operator-(const FILETIME &ft1,const FILETIME &ft2)
{
	LARGE_INTEGER Time1,Time2;

	Time1.LowPart=ft1.dwLowDateTime;
	Time1.HighPart=ft1.dwHighDateTime;
	Time2.LowPart=ft2.dwLowDateTime;
	Time2.HighPart=ft2.dwHighDateTime;
	return Time1.QuadPart-Time2.QuadPart;
}

void GetLocalTimeAsFileTime(FILETIME *pTime)
{
	SYSTEMTIME st;

	GetLocalTime(&st);
	SystemTimeToFileTime(&st,pTime);
}

int CalcDayOfWeek(int Year,int Month,int Day)
{
	if (Month<=2) {
		Year--;
		Month+=12;
	}
	return (Year*365+Year/4-Year/100+Year/400+306*(Month+1)/10+Day-428)%7;
}

LPCTSTR GetDayOfWeekText(int DayOfWeek)
{
	if (DayOfWeek<0 || DayOfWeek>6)
		return TEXT("？");
	return TEXT("日\0月\0火\0水\0木\0金\0土")+DayOfWeek*((3-sizeof(TCHAR))+1);
}

int CALLBACK BrowseFolderCallback(HWND hwnd,UINT uMsg,LPARAM lpData, LPARAM lParam)
{
	switch (uMsg) {
	case BFFM_INITIALIZED:
		if (((LPTSTR)lParam)[0]!=TEXT('\0')) {
			TCHAR szDirectory[MAX_PATH];

			lstrcpy(szDirectory,(LPTSTR)lParam);
			PathRemoveBackslash(szDirectory);
			SendMessage(hwnd,BFFM_SETSELECTION,TRUE,(LPARAM)szDirectory);
		}
		break;
	}
	return 0;
}

bool BrowseFolderDialog(HWND hwndOwner,LPTSTR pszDirectory,LPCTSTR pszTitle)
{
	BROWSEINFO bi;
	PIDLIST_ABSOLUTE pidl;
	BOOL fRet;

	bi.hwndOwner=hwndOwner;
	bi.pidlRoot=NULL;
	bi.pszDisplayName=pszDirectory;
	bi.lpszTitle=pszTitle;
	bi.ulFlags=BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	bi.lpfn=BrowseFolderCallback;
	bi.lParam=(LPARAM)pszDirectory;
	pidl=SHBrowseForFolder(&bi);
	if (pidl==NULL)
		return false;
	fRet=SHGetPathFromIDList(pidl,pszDirectory);
	CoTaskMemFree(pidl);
	return fRet==TRUE;
}

#endif


#if 1 // From: EDCBSupport_0.2.0_Src/EDCBSupport.cpp

void SetComboBoxList(HWND hDlg,int ID,LPCTSTR const *pList,int Length)
{
	for (int i=0;i<Length;i++)
		::SendDlgItemMessage(hDlg,ID,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(pList[i]));
}

BOOL WritePrivateProfileInt(LPCTSTR pszSection,LPCTSTR pszKey,int Value,LPCTSTR pszFileName)
{
	TCHAR szValue[32];

	::wsprintf(szValue,TEXT("%d"),Value);
	return ::WritePrivateProfileString(pszSection,pszKey,szValue,pszFileName);
}

#endif


#if 1 // From: TVTest_0.7.19r2_Src/BonTsEngine\TsEncode.cpp (CRT回避のため一部改変)

const bool AribToSystemTime(const BYTE *pHexData, SYSTEMTIME *pSysTime)
{
	// 全ビットが1のときは未定義
	if((*((DWORD *)pHexData) == 0xFFFFFFFFUL) && (pHexData[4] == 0xFFU))return false;

	// MJD形式の日付を解析
	SplitAribMjd(((WORD)pHexData[0] << 8) | (WORD)pHexData[1], &pSysTime->wYear, &pSysTime->wMonth, &pSysTime->wDay, &pSysTime->wDayOfWeek);

	// BCD形式の時刻を解析
	SplitAribBcd(&pHexData[2], &pSysTime->wHour, &pSysTime->wMinute, &pSysTime->wSecond);

	// ミリ秒は常に0
	pSysTime->wMilliseconds = 0U;

	return true;
}

/* オリジナル
void SplitAribMjd(const WORD wAribMjd, WORD *pwYear, WORD *pwMonth, WORD *pwDay, WORD *pwDayOfWeek)
{
	// MJD形式の日付を解析する
	const DWORD dwYd = (DWORD)(((double)wAribMjd - 15078.2) / 365.25);
	const DWORD dwMd = (DWORD)(((double)wAribMjd - 14956.1 - (double)((int)((double)dwYd * 365.25))) / 30.6001);
	const DWORD dwK = ((dwMd == 14UL) || (dwMd == 15UL))? 1U : 0U;

	if(pwDay)*pwDay = wAribMjd - 14956U - (WORD)((double)dwYd * 365.25) - (WORD)((double)dwMd * 30.6001);
	if(pwYear)*pwYear = (WORD)(dwYd + dwK) + 1900U;
	if(pwMonth)*pwMonth	= (WORD)(dwMd - 1UL - dwK * 12UL);
	if(pwDayOfWeek)*pwDayOfWeek = (wAribMjd + 3U) % 7U;
}*/
// 1900-03-01(MJD15079)～2038-04-22(MJD65535)においてオリジナルと出力一致
// 参考: ARIB STD-B10,TR-B13
void SplitAribMjd(const WORD wAribMjd, WORD *pwYear, WORD *pwMonth, WORD *pwDay, WORD *pwDayOfWeek)
{
	// MJD形式の日付を解析する
	const DWORD dwYd = ((DWORD)wAribMjd * 20 - 301564) / 7305;
	const DWORD dwMd = ((DWORD)wAribMjd * 10000 - 149561000 - (DWORD)dwYd * 1461 / 4 * 10000) / 306001;
	const DWORD dwK = ((dwMd == 14UL) || (dwMd == 15UL))? 1U : 0U;

	if(pwDay)*pwDay = wAribMjd - 14956U - (WORD)((DWORD)dwYd * 1461 / 4) - (WORD)((DWORD)dwMd * 306001 / 10000);
	if(pwYear)*pwYear = (WORD)(dwYd + dwK) + 1900U;
	if(pwMonth)*pwMonth	= (WORD)(dwMd - 1UL - dwK * 12UL);
	if(pwDayOfWeek)*pwDayOfWeek = (wAribMjd + 3U) % 7U;
}

void SplitAribBcd(const BYTE *pAribBcd, WORD *pwHour, WORD *pwMinute, WORD *pwSecond)
{
	// BCD形式の時刻を解析する
	if(pwHour)*pwHour		= (WORD)(pAribBcd[0] >> 4) * 10U + (WORD)(pAribBcd[0] & 0x0FU);
	if(pwMinute)*pwMinute	= (WORD)(pAribBcd[1] >> 4) * 10U + (WORD)(pAribBcd[1] & 0x0FU);
	if(pwSecond)*pwSecond	= (WORD)(pAribBcd[2] >> 4) * 10U + (WORD)(pAribBcd[2] & 0x0FU);
}

#endif


# if 1 // From: TVTest_0.7.19r2_Src/HelperClass/StdUtil.cpp (vswprintf_s -> wvsprintf改変)
int StdUtil_snprintf(wchar_t *s,size_t n,const wchar_t *format, ...)
{
	va_list args;
	int Length;

	va_start(args,format);
	if (n>0) {
        // wvsprintfのバッファ制限は1KB(KB77255)
        // 実測ではWCHARだと2KB、超えると1024番目にNULL文字がつく
        TCHAR buf[1025];
		Length=::wvsprintf(buf,format,args);
        ::lstrcpyn(s, buf, n);
	} else {
		Length=0;
	}
	va_end(args);
	return Length;
}
#endif


#if 1 // From: TVTest_0.7.19r2_Src/Record.cpp (一部改変)

int MapFileNameCopy(LPWSTR pszFileName,int MaxFileName,LPCWSTR pszText)
{
	int i;
	LPCWSTR p=pszText;

	for (i=0;i<MaxFileName-1 && *p!='\0';i++) {
		static const struct {
			WCHAR From;
			WCHAR To;
		} CharMap[] = {
			{L'\\',	L'￥'},
			{L'/',	L'／'},
			{L':',	L'：'},
			{L'*',	L'＊'},
			{L'?',	L'？'},
			{L'"',	L'”'},
			{L'<',	L'＜'},
			{L'>',	L'＞'},
			{L'|',	L'｜'},
		};

		for (int j=0;j<ARRAY_SIZE(CharMap);j++) {
			if (CharMap[j].From==*p) {
				pszFileName[i]=CharMap[j].To;
				goto Next;
			}
		}
		pszFileName[i]=*p;
	Next:
		p++;
	}
	pszFileName[i]='\0';
	return i;
}

int FormatFileName(LPTSTR pszFileName, int MaxFileName, WORD EventID, FILETIME StartTimeSpec, LPCTSTR pszEventName, LPCTSTR pszFormat)
{
	SYSTEMTIME stStart;
	LPCTSTR p;
	int i;

	::FileTimeToSystemTime(&StartTimeSpec,&stStart);
	p=pszFormat;
	for (i=0;i<MaxFileName-1 && *p!='\0';) {
		if (*p=='%') {
			p++;
			if (*p=='%') {
				pszFileName[i++]='%';
				p++;
			} else {
				TCHAR szKeyword[32];
				int j;

				for (j=0;*p!='%' && *p!='\0';) {
					if (j<ARRAY_SIZE(szKeyword)-1)
						szKeyword[j++]=*p;
					p++;
				}
				if (*p=='%')
					p++;
				szKeyword[j]='\0';
				int Remain=MaxFileName-i;
				if (::lstrcmpi(szKeyword,TEXT("date"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d%02d%02d"),
										(int)stStart.wYear,(int)stStart.wMonth,(int)stStart.wDay);
				} else if (::lstrcmpi(szKeyword,TEXT("year"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),(int)stStart.wYear);
				} else if (::lstrcmpi(szKeyword,TEXT("year2"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),(int)stStart.wYear%100);
				} else if (::lstrcmpi(szKeyword,TEXT("month"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),(int)stStart.wMonth);
				} else if (::lstrcmpi(szKeyword,TEXT("month2"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),(int)stStart.wMonth);
				} else if (::lstrcmpi(szKeyword,TEXT("day"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),(int)stStart.wDay);
				} else if (::lstrcmpi(szKeyword,TEXT("day2"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),(int)stStart.wDay);
				} else if (::lstrcmpi(szKeyword,TEXT("time"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d%02d%02d"),
										 (int)stStart.wHour,(int)stStart.wMinute,(int)stStart.wSecond);
				} else if (::lstrcmpi(szKeyword,TEXT("hour"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),(int)stStart.wHour);
				} else if (::lstrcmpi(szKeyword,TEXT("hour2"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),(int)stStart.wHour);
				} else if (::lstrcmpi(szKeyword,TEXT("minute"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),(int)stStart.wMinute);
				} else if (::lstrcmpi(szKeyword,TEXT("minute2"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),(int)stStart.wMinute);
				} else if (::lstrcmpi(szKeyword,TEXT("second"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),(int)stStart.wSecond);
				} else if (::lstrcmpi(szKeyword,TEXT("second2"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),(int)stStart.wSecond);
				} else if (::lstrcmpi(szKeyword,TEXT("day-of-week"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%s"),
										 GetDayOfWeekText(stStart.wDayOfWeek));
				} else if (::lstrcmpi(szKeyword,TEXT("event-name"))==0) {
					if (pszEventName!=NULL)
						i+=MapFileNameCopy(&pszFileName[i],Remain,pszEventName);
				} else if (::lstrcmpi(szKeyword,TEXT("event-id"))==0) {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%04X"),(int)EventID);
				} else {
					i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%%%s%%"),szKeyword);
				}
			}
		} else {
#ifndef UNICODE
			if (::IsDBCSLeadByteEx(CP_ACP,*p)) {
				if (i+1==MaxFileName)
					break;
				pszFileName[i++]=*p++;
			}
#endif
			pszFileName[i++]=*p++;
		}
	}
	pszFileName[i]='\0';
	return i;
}

#endif

// イベント名に"%num%"or"%num2"(連番)フォーマット指示子があれば置換する
int FormatEventName(LPTSTR pszEventName, int MaxEventName, int num, LPCTSTR pszFormat)
{
	LPCTSTR p;
	int i;

	p=pszFormat;
	for (i=0;i<MaxEventName-1 && *p!='\0';) {
		if (*p=='%') {
			p++;
			if (*p=='%') {
				pszEventName[i++]='%';
				p++;
			} else {
				TCHAR szKeyword[32];
				int j;

				for (j=0;*p!='%' && *p!='\0';) {
					if (j<ARRAY_SIZE(szKeyword)-1)
						szKeyword[j++]=*p;
					p++;
				}
				if (*p=='%')
					p++;
				szKeyword[j]='\0';
				int Remain=MaxEventName-i;
				if (::lstrcmpi(szKeyword,TEXT("num"))==0) {
					if (num > 0) i+=StdUtil_snprintf(&pszEventName[i],Remain,TEXT("%d"),num);
				} else if (::lstrcmpi(szKeyword,TEXT("num2"))==0) {
                    if (num > 0) i+=StdUtil_snprintf(&pszEventName[i],Remain,TEXT("%02d"),num);
                } else {
					i+=StdUtil_snprintf(&pszEventName[i],Remain,TEXT("%%%s%%"),szKeyword);
				}
			}
		} else {
#ifndef UNICODE
			if (::IsDBCSLeadByteEx(CP_ACP,*p)) {
				if (i+1==MaxEventName)
					break;
				pszEventName[i++]=*p++;
			}
#endif
			pszEventName[i++]=*p++;
		}
	}
	pszEventName[i]='\0';
	return i;
}


# if 1 // From: TVTest_0.7.21r2_Src/BonTsEngine/TsUtilClass.cpp

CCriticalLock::CCriticalLock()
{
	// クリティカルセクション初期化
	::InitializeCriticalSection(&m_CriticalSection);
}

CCriticalLock::~CCriticalLock()
{
	// クリティカルセクション削除
	::DeleteCriticalSection(&m_CriticalSection);
}

void CCriticalLock::Lock(void)
{
	// クリティカルセクション取得
	::EnterCriticalSection(&m_CriticalSection);
}

void CCriticalLock::Unlock(void)
{
	// クリティカルセクション開放
	::LeaveCriticalSection(&m_CriticalSection);
}

CBlockLock::CBlockLock(CCriticalLock *pCriticalLock)
	: m_pCriticalLock(pCriticalLock)
{
	// ロック取得
	m_pCriticalLock->Lock();
}

CBlockLock::~CBlockLock()
{
	// ロック開放
	m_pCriticalLock->Unlock();
}

#endif


#if 1 // From: TVTest_0.7.23_Src/Tooltip.cpp (一部改変)

CBalloonTip::CBalloonTip()
	: m_hwndToolTips(NULL)
{
}


CBalloonTip::~CBalloonTip()
{
	Finalize();
}


bool CBalloonTip::Initialize(HWND hwnd, HMODULE hModule)
{
	if (m_hwndToolTips!=NULL)
		return false;

	m_hwndToolTips=::CreateWindowEx(WS_EX_TOPMOST,TOOLTIPS_CLASS,NULL,
									WS_POPUP | TTS_NOPREFIX | TTS_BALLOON/* | TTS_CLOSE*/,
									0,0,0,0,
									NULL,NULL,hModule,NULL);
	if (m_hwndToolTips==NULL)
		return false;

	::SendMessage(m_hwndToolTips,TTM_SETMAXTIPWIDTH,0,320);

	TOOLINFO ti;

	::ZeroMemory(&ti,sizeof(ti));
	ti.cbSize=TTTOOLINFO_V1_SIZE;
	ti.uFlags=TTF_SUBCLASS | TTF_TRACK;
	ti.hwnd=hwnd;
	ti.uId=0;
	ti.hinst=NULL;
	ti.lpszText=TEXT("");
	::SendMessage(m_hwndToolTips,TTM_ADDTOOL,0,(LPARAM)&ti);

	m_hwndOwner=hwnd;

	return true;
}


void CBalloonTip::Finalize()
{
	if (m_hwndToolTips!=NULL) {
		::DestroyWindow(m_hwndToolTips);
		m_hwndToolTips=NULL;
	}
}


bool CBalloonTip::Show(LPCTSTR pszText,LPCTSTR pszTitle,const POINT *pPos,int Icon)
{
	if (m_hwndToolTips==NULL || pszText==NULL)
		return false;
	TOOLINFO ti;
	ti.cbSize=TTTOOLINFO_V1_SIZE;
	ti.hwnd=m_hwndOwner;
	ti.uId=0;
	ti.lpszText=const_cast<LPTSTR>(pszText);
	::SendMessage(m_hwndToolTips,TTM_UPDATETIPTEXT,0,(LPARAM)&ti);
	::SendMessage(m_hwndToolTips,TTM_SETTITLE,Icon,(LPARAM)(pszTitle!=NULL?pszTitle:TEXT("")));
	POINT pt;
	if (pPos!=NULL) {
		pt=*pPos;
	} else {
		RECT rc;
		::SystemParametersInfo(SPI_GETWORKAREA,0,&rc,0);
		pt.x=rc.right-32;
		pt.y=rc.bottom;
	}
	::SendMessage(m_hwndToolTips,TTM_TRACKPOSITION,0,MAKELPARAM(pt.x,pt.y));
	::SendMessage(m_hwndToolTips,TTM_TRACKACTIVATE,TRUE,(LPARAM)&ti);
	return true;
}


bool CBalloonTip::Hide()
{
	TOOLINFO ti;

	ti.cbSize=TTTOOLINFO_V1_SIZE;
	ti.hwnd=m_hwndOwner;
	ti.uId=0;
	::SendMessage(m_hwndToolTips,TTM_TRACKACTIVATE,FALSE,(LPARAM)&ti);
	return true;
}

#endif
