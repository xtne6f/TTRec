#include <Windows.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <tchar.h>
#include "Util.h"

// CRT非依存のためにはさらに /NODEFAULTLIB /GR- /GS- /EHsc削除 が必要
#ifdef NO_CRT
// new/deleteの置き換え
void *operator new(size_t size)
{
    return ::HeapAlloc(::GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, size);
}

void operator delete(void *ptr)
{
    if (ptr) ::HeapFree(::GetProcessHeap(), 0, ptr);
}
#endif

// WritePrivateProfileString()の引用符付加版
BOOL WritePrivateProfileStringQuote(LPCTSTR lpAppName, LPCTSTR lpKeyName, LPCTSTR lpString, LPCTSTR lpFileName)
{
    TCHAR szVal[1024];
    int len = ::lstrlen(lpString);
    if (lpString[0] != TEXT('"') || lpString[0] != lpString[len-1] || len == 1) {
        return ::WritePrivateProfileString(lpAppName, lpKeyName, lpString, lpFileName);
    }
    if (len >= _countof(szVal) - 2) return FALSE;
    szVal[0] = TEXT('"');
    ::lstrcpy(&szVal[1], lpString);
    szVal[len+1] = TEXT('"');
    szVal[len+2] = TEXT('\0');
    return ::WritePrivateProfileString(lpAppName, lpKeyName, szVal, lpFileName);
}

DWORD GetLongModuleFileName(HMODULE hModule, LPTSTR lpFileName, DWORD nSize)
{
    TCHAR longOrShortName[MAX_PATH];
    DWORD nRet = ::GetModuleFileName(hModule, longOrShortName, MAX_PATH);
    if (nRet && nRet < MAX_PATH) {
        nRet = ::GetLongPathName(longOrShortName, lpFileName, nSize);
        if (nRet < nSize) return nRet;
    }
    return 0;
}

DWORD GetShortModuleFileName(HMODULE hModule, LPTSTR lpFileName, DWORD nSize)
{
    TCHAR longOrShortName[MAX_PATH];
    DWORD nRet = ::GetModuleFileName(hModule, longOrShortName, MAX_PATH);
    if (nRet && nRet < MAX_PATH) {
        nRet = ::GetShortPathName(longOrShortName, lpFileName, nSize);
        if (nRet < nSize) return nRet;
    }
    return 0;
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

bool GetRundll32Path(LPTSTR path)
{
    TCHAR systemDir[MAX_PATH];
    if (!::GetSystemDirectory(systemDir, MAX_PATH)) return false;
    ::PathCombine(path, systemDir, TEXT("rundll32.exe"));
    if (!::PathFileExists(path)) return false;
    return true;
}

// トークンを取得
void GetToken(LPCTSTR str, LPTSTR token, int max)
{
    int size = ::StrCSpn(str, TEXT("\t\r\n")) + 1;
    ::lstrcpyn(token, str, size < max ? size : max);
}

// 次のトークン(タブ区切り)までポインタを進める
bool NextToken(LPCTSTR *pStr)
{
    *pStr += ::StrCSpn(*pStr, TEXT("\t\r\n"));
    if (**pStr == TEXT('\t')) {
        ++*pStr;
        return true;
    }
    *pStr = NULL;
    return false;
}

// トークン区切りになる文字を空白文字に置換する
void ReplaceTokenDelimiters(LPTSTR str)
{
    TranslateText(str, TEXT("/\t\r\n/   /"));
}

// patternに従ってstrの文字を置換する
void TranslateText(LPTSTR str, LPCTSTR pattern)
{
    TCHAR delim = pattern[0];
    int len = ::lstrlen(pattern);
    if (len >= 3 && len % 2 != 0 && pattern[len - 1] == delim && pattern[len / 2] == delim) {
        for (int i = 0; str[i]; ++i) {
            for (int j = 1; pattern[j] != delim; ++j) {
                if (str[i] == pattern[j]) {
                    str[i] = pattern[len / 2 + j];
                    break;
                }
            }
        }
    }
}

// patternに従ってstrから文字列を削除する
void RemoveTextPattern(LPTSTR str, LPCTSTR pattern)
{
    TCHAR delim = pattern[0];
    int len = ::lstrlen(pattern);
    if (len >= 2 && pattern[len - 1] == delim) {
        LPTSTR q = str;
        for (LPCTSTR p = str; *p;) {
            int matchLen = 0;
            for (int i = 1; pattern[i] && matchLen <= 0; ++i) {
                matchLen = 0;
                for (; pattern[i] != delim; ++i) {
                    if (matchLen < 0 || p[matchLen++] != pattern[i]) {
                        matchLen = -1;
                    }
                }
            }
            if (matchLen > 0) {
                p += matchLen;
            }
            else {
                *q++ = *p++;
            }
        }
        *q = 0;
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

bool StrToTimeSpan(LPCTSTR str, int *pSpan, LPCTSTR *endptr)
{
    // フォーマットは"hh:mm:ss"のみ
    if (endptr) *endptr = str;
    if (lstrlen(str) < 8 || str[2] != TEXT(':') || str[5] != TEXT(':')) return false;
    if (endptr) *endptr += 8;

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


#if 1 // From: TVTest_0.8.2_Src/ProgramSearch.cpp (一部改変)

static bool FindKeyword(LPCTSTR pszText,LPCTSTR pKeyword,int KeywordLength,bool fIgnoreCase)
{
	if (pszText[0]=='\0')
		return false;

	UINT Flags=NORM_IGNOREWIDTH;
	if (fIgnoreCase)
		Flags|=NORM_IGNORECASE;

	FARPROC pFindNLSString=::GetProcAddress(::GetModuleHandle(TEXT("kernel32.dll")),"FindNLSString");
	if (pFindNLSString!=NULL) {
		// Vista以降
		if (reinterpret_cast<int(WINAPI*)(LCID,DWORD,LPCWSTR,int,LPCWSTR,int,LPINT)>(pFindNLSString)(
				LOCALE_USER_DEFAULT,FIND_FROMSTART|Flags,pszText,-1,pKeyword,KeywordLength,NULL)>=0) {
			return true;
		}
	} else {
		const int StringLength=::lstrlen(pszText);

		if (StringLength<KeywordLength)
			return false;

		for (int i=0;i<=StringLength-KeywordLength;i++) {
			if (::CompareString(LOCALE_USER_DEFAULT,Flags,
					pszText+i,KeywordLength,pKeyword,KeywordLength)==CSTR_EQUAL) {
				return true;
			}
		}
	}

	return false;
}

bool MatchKeyword(LPCTSTR pszText,LPCTSTR pszKeyword)
{
	bool fIgnoreCase=pszKeyword[0]==PREFIX_IGNORECASE;
	bool fMatch=false,fMinusOnly=true;
	bool fOr=false,fPrevOr=false,fOrMatch;
	int WordCount=0;
	LPCTSTR p=pszKeyword;
	if (fIgnoreCase) p++;

	while (*p!='\0') {
		TCHAR szWord[MAX_KEYWORD_LENGTH],Delimiter;
		bool fMinus=false;

		while (*p==' ')
			p++;
		if (*p=='-') {
			fMinus=true;
			p++;
		}
		if (*p=='"') {
			p++;
			Delimiter='"';
		} else {
			Delimiter=' ';
		}
		int i;
		for (i=0;*p!=Delimiter && *p!='|' && *p!='\0';i++) {
			szWord[i]=*p++;
		}
		if (*p==Delimiter)
			p++;
		while (*p==' ')
			p++;
		if (*p=='|') {
			if (!fOr) {
				fOr=true;
				fOrMatch=false;
			}
			p++;
		} else {
			fOr=false;
		}
		if (i>0) {
			if (FindKeyword(pszText,szWord,i,fIgnoreCase)) {
				if (fMinus)
					return false;
				fMatch=true;
				if (fOr)
					fOrMatch=true;
			} else {
				if (!fMinus && !fOr && (!fPrevOr || !fOrMatch))
					return false;
			}
			if (!fMinus)
				fMinusOnly=false;
			WordCount++;
		}
		fPrevOr=fOr;
	}
	if (fMinusOnly && WordCount>0)
		return true;
	return fMatch;
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


#if 1 // From: TVTest_0.8.2_Src/Record.cpp (一部改変)

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
	for (i=0;i<MaxFileName-1 && *p!=_T('\0');) {
		if (*p==_T('%')) {
			p++;
			if (*p==_T('%')) {
				pszFileName[i++]=_T('%');
				p++;
			} else {
				TCHAR szKeyword[32];
				size_t j;

				for (j=0;p[j]!=_T('%') && p[j]!=_T('\0');j++) {
					if (j<ARRAY_SIZE(szKeyword)-1)
						szKeyword[j]=p[j];
				}
				if (j<=ARRAY_SIZE(szKeyword)-1 && p[j]==_T('%')) {
					const int Remain=MaxFileName-i;

					p+=j+1;
					szKeyword[j]=_T('\0');
					if (::lstrcmpi(szKeyword,TEXT("date"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d%02d%02d"),
											stStart.wYear,stStart.wMonth,stStart.wDay);
					} else if (::lstrcmpi(szKeyword,TEXT("year"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),stStart.wYear);
					} else if (::lstrcmpi(szKeyword,TEXT("year2"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),stStart.wYear%100);
					} else if (::lstrcmpi(szKeyword,TEXT("month"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),stStart.wMonth);
					} else if (::lstrcmpi(szKeyword,TEXT("month2"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),stStart.wMonth);
					} else if (::lstrcmpi(szKeyword,TEXT("day"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),stStart.wDay);
					} else if (::lstrcmpi(szKeyword,TEXT("day2"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),stStart.wDay);
					} else if (::lstrcmpi(szKeyword,TEXT("time"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d%02d%02d"),
											stStart.wHour,stStart.wMinute,stStart.wSecond);
					} else if (::lstrcmpi(szKeyword,TEXT("hour"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),stStart.wHour);
					} else if (::lstrcmpi(szKeyword,TEXT("hour2"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),stStart.wHour);
					} else if (::lstrcmpi(szKeyword,TEXT("minute"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),stStart.wMinute);
					} else if (::lstrcmpi(szKeyword,TEXT("minute2"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),stStart.wMinute);
					} else if (::lstrcmpi(szKeyword,TEXT("second"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%d"),stStart.wSecond);
					} else if (::lstrcmpi(szKeyword,TEXT("second2"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%02d"),stStart.wSecond);
					} else if (::lstrcmpi(szKeyword,TEXT("day-of-week"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%s"),
											GetDayOfWeekText(stStart.wDayOfWeek));
					} else if (::lstrcmpi(szKeyword,TEXT("event-name"))==0) {
						if (pszEventName!=NULL)
							i+=MapFileNameCopy(&pszFileName[i],Remain,pszEventName);
					} else if (::lstrcmpi(szKeyword,TEXT("event-id"))==0) {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%04X"),EventID);
					} else {
						i+=StdUtil_snprintf(&pszFileName[i],Remain,TEXT("%%%s%%"),szKeyword);
					}
				} else {
					pszFileName[i++]=_T('%');
				}
			}
		} else {
			pszFileName[i++]=*p++;
		}
	}
	pszFileName[i]=_T('\0');
	return i;
}

#endif

// イベント名に"%num%"or"%num2"(連番)フォーマット指示子があれば置換する
int FormatEventName(LPTSTR pszEventName, int MaxEventName, int num, LPCTSTR pszFormat)
{
	LPCTSTR p;
	int i;

	p=pszFormat;
	for (i=0;i<MaxEventName-1 && *p!=_T('\0');) {
		if (*p==_T('%')) {
			p++;
			if (*p==_T('%')) {
				pszEventName[i++]=_T('%');
				p++;
			} else {
				TCHAR szKeyword[32];
				size_t j;

				for (j=0;p[j]!=_T('%') && p[j]!=_T('\0');j++) {
					if (j<ARRAY_SIZE(szKeyword)-1)
						szKeyword[j]=p[j];
				}
				if (j<=ARRAY_SIZE(szKeyword)-1 && p[j]==_T('%')) {
					const int Remain=MaxEventName-i;

					p+=j+1;
					szKeyword[j]=_T('\0');
					if (::lstrcmpi(szKeyword,TEXT("num"))==0) {
						if (num > 0) i+=StdUtil_snprintf(&pszEventName[i],Remain,TEXT("%d"),num);
					} else if (::lstrcmpi(szKeyword,TEXT("num2"))==0) {
						if (num > 0) i+=StdUtil_snprintf(&pszEventName[i],Remain,TEXT("%02d"),num);
					} else {
						i+=StdUtil_snprintf(&pszEventName[i],Remain,TEXT("%%%s%%"),szKeyword);
					}
				} else {
					pszEventName[i++]=_T('%');
				}
			}
		} else {
			pszEventName[i++]=*p++;
		}
	}
	pszEventName[i]=_T('\0');
	return i;
}


void CNotifyIcon::Initialize(HWND hwnd, UINT uid, UINT msg)
{
    Finalize();
    m_hwnd = hwnd;
    m_uid = uid;
    m_msg = msg;
}

void CNotifyIcon::Finalize()
{
    Hide();
    m_hwnd = NULL;
}

bool CNotifyIcon::Show()
{
    Hide();
    if (m_hwnd) {
        NOTIFYICONDATA nid = {0};
        nid.cbSize = NOTIFYICONDATA_V2_SIZE;
        nid.hWnd = m_hwnd;
        nid.uID = m_uid;
        nid.uFlags = NIF_MESSAGE | NIF_ICON;
        nid.uCallbackMessage = m_msg;
        nid.hIcon = static_cast<HICON>(::LoadImage(::GetModuleHandle(NULL), MAKEINTRESOURCE(1), IMAGE_ICON,
                                       ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        if (!nid.hIcon) {
            // 本体アイコンがとれないときの保険
            nid.hIcon = static_cast<HICON>(::LoadImage(NULL, IDI_INFORMATION, IMAGE_ICON,
                                           ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        }
        m_fShow = ::Shell_NotifyIcon(NIM_ADD, &nid) != FALSE;
        return m_fShow;
    }
    return false;
}

bool CNotifyIcon::Hide()
{
    if (m_hwnd) {
        if (m_fShow) {
            NOTIFYICONDATA nid = {0};
            nid.cbSize = NOTIFYICONDATA_V2_SIZE;
            nid.hWnd = m_hwnd;
            nid.uID = m_uid;
            ::Shell_NotifyIcon(NIM_DELETE, &nid);
            m_fShow = false;
        }
        return true;
    }
    return false;
}

bool CNotifyIcon::SetText(LPCTSTR pszText)
{
    if (m_hwnd && m_fShow) {
        NOTIFYICONDATA nid = {0};
        nid.cbSize = NOTIFYICONDATA_V2_SIZE;
        nid.hWnd = m_hwnd;
        nid.uID = m_uid;
        nid.uFlags = NIF_TIP;
        ::lstrcpyn(nid.szTip, pszText, ARRAY_SIZE(nid.szTip));
        return ::Shell_NotifyIcon(NIM_MODIFY, &nid) != FALSE;
    }
    return false;
}


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
