//////////////////////////////////////////////////////////////////////////
//
//  UltraDefrag - a powerful defragmentation tool for Windows NT.
//  Copyright (c) 2007-2013 Dmitri Arkhangelski (dmitriar@gmail.com).
//  Copyright (c) 2010-2013 Stefan Pendl (stefanpe@users.sourceforge.net).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
//////////////////////////////////////////////////////////////////////////

/**
 * @file utils.cpp
 * @brief Auxiliary utilities.
 * @addtogroup Utils
 * @{
 */

// Ideas by Stefan Pendl <stefanpe@users.sourceforge.net>
// and Dmitri Arkhangelski <dmitriar@gmail.com>.

// =======================================================================
//                            Declarations
// =======================================================================

#include "main.h"

#define WIN_TMPF_TRUETYPE 0x04
typedef struct WIN_CONSOLE_FONT_INFO_EX {
  ULONG cbSize;
  DWORD nFont;
  COORD dwFontSize;
  UINT  FontFamily;
  UINT  FontWeight;
  WCHAR FaceName[LF_FACESIZE];
} WIN_CONSOLE_FONT_INFO_EX, *PWIN_CONSOLE_FONT_INFO_EX;
typedef BOOL (WINAPI *GET_CURRENT_CONSOLE_FONT_EX_PROC)(
    HANDLE hConsoleOutput,BOOL bMaximumWindow,
    PWIN_CONSOLE_FONT_INFO_EX lpConsoleCurrentFontEx
);

typedef HRESULT (__stdcall *URLMON_PROCEDURE)(
    /* LPUNKNOWN */ void *lpUnkcaller,
    LPCWSTR szURL,
    LPWSTR szFileName,
    DWORD cchFileName,
    DWORD dwReserved,
    /*IBindStatusCallback*/ void *pBSC
);

// =======================================================================
//                         Auxiliary utilities
// =======================================================================

/**
 * @brief Defines whether the user
 * has administrative rights or not.
 * @details Based on the UserInfo
 * NSIS plug-in.
 */
bool check_admin_rights(void)
{
    HANDLE hToken;
    if(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,FALSE,&hToken)){
        letrace("cannot open access token of the thread");
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&hToken)){
            letrace("cannot open access token of the process");
            return false;
        }
    }

    PSID psid = NULL;
    SID_IDENTIFIER_AUTHORITY SystemSidAuthority = {SECURITY_NT_AUTHORITY};
    if(!AllocateAndInitializeSid(&SystemSidAuthority,2,
      SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,
      0,0,0,0,0,0,&psid)){
        letrace("cannot create the security identifier");
        CloseHandle(hToken);
        return false;
    }

    BOOL is_member = false;
    if(!CheckTokenMembership(NULL,psid,&is_member)){
        letrace("cannot check token membership");
        if(psid) FreeSid(psid); CloseHandle(hToken);
        return false;
    }

    if(!is_member) itrace("the user is not a member of administrators group");
    if(psid) FreeSid(psid); CloseHandle(hToken);
    return is_member;
}

/**
 * @brief Fills the current line with spaces.
 */
void clear_line(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if(!GetConsoleScreenBufferInfo(g_out,&csbi))
        return; /* impossible to determine the screen width */
    int n = (int)csbi.dwSize.X;

    char *line = new char[n + 1];
    memset(line,0x20,n);
    line[n] = 0;
    fprintf(stdout,"\r%s",line);
    delete line;

    /* move cursor back to the previous line */
    if(GetConsoleScreenBufferInfo(g_out,&csbi)){
        COORD pos; pos.X = 0;
        pos.Y = csbi.dwCursorPosition.Y - 1;
        (void)SetConsoleCursorPosition(g_out,pos);
    }
}

/**
 * @brief Displays Unicode string on the screen.
 * @details Based on two articles:
 * http://blogs.msdn.com/b/michkap/archive/2010/05/07/10008232.aspx
 * http://www.codeproject.com/Articles/34068/Unicode-Output-to-the-Windows-Console
 * @param[in] string the string to be displayed.
 * @note
 * - UTF-8 encoding is always used when the output
 * is redirected to a file.
 * - Since Windows Vista UTF-8 encoding is used
 * when a True Type font is selected for the console
 * window.
 * - In all other cases the string becomes converted
 * to the OEM encoding; thus all the characters which
 * cannot be converted become replaced by question
 * marks.
 */
void print_unicode(wchar_t *string)
{
    int old_cp = GetConsoleOutputCP();

    /* output redirected to a file can be converted to UTF-8 */
    DWORD file_type = GetFileType(g_out);
    if(file_type == FILE_TYPE_UNKNOWN && GetLastError() != NO_ERROR){
        letrace("GetFileType failed");
    } else {
        file_type &= ~(FILE_TYPE_REMOTE);
        if(file_type == FILE_TYPE_CHAR){
            DWORD mode;
            if(!GetConsoleMode(g_out,&mode)){
                if(GetLastError() == ERROR_INVALID_HANDLE){
                    wxString s(string);
                    printf("%hs",s.utf8_str().data());
                    return;
                } else {
                    letrace("GetConsoleMode failed");
                }
            }
        } else {
            wxString s(string);
            printf("%hs",s.utf8_str().data());
            return;
        }
    }

    /* if the console uses a TrueType font, UTF-8 can be used too */
    int version; wxGetOsVersion(&version);
    if(version > 5){ // Windows Vista etc.
        wxDynamicLibrary lib(wxT("kernel32"));
        wxDYNLIB_FUNCTION(GET_CURRENT_CONSOLE_FONT_EX_PROC,
            GetCurrentConsoleFontEx, lib);
        if(pfnGetCurrentConsoleFontEx){
            WIN_CONSOLE_FONT_INFO_EX cfie;
            memset(&cfie,0,sizeof(cfie));
            cfie.cbSize = sizeof(cfie);
            if(!pfnGetCurrentConsoleFontEx(g_out,false,&cfie)){
                letrace("GetCurrentConsoleFontEx failed");
            } else {
                if((cfie.FontFamily & WIN_TMPF_TRUETYPE) == WIN_TMPF_TRUETYPE){
                    if(!SetConsoleOutputCP(CP_UTF8)){
                        letrace("SetConsoleOutputCP failed");
                    } else {
                        wxString s(string);
                        printf("%hs",s.utf8_str().data());
                        SetConsoleOutputCP(old_cp);
                        return;
                    }
                }
            }
        }
    }

    /* if the console runs inside of the PowerShell ISE, UTF-8 can be used too */
    /* however, this approach failed being tested on a Win7 driven machine */

    DWORD n; WriteConsole(g_out,string,wcslen(string),&n,NULL);
}

/**
 * @brief Downloads a file from the web.
 * @return Path to the downloaded file.
 * @note If the program terminates before
 * the file download completion it crashes.
 */
wxString download(const wxString& url)
{
    wxLogMessage(wxT("downloading %ls"),url.wc_str());

    HMODULE h = ::LoadLibrary(wxT("urlmon.dll"));
    if(!h){
        wxLogSysError(wxT("cannot load urlmon.dll library"));
        return wxEmptyString;
    }

    URLMON_PROCEDURE pDownload = (URLMON_PROCEDURE) \
        ::GetProcAddress(h,"URLDownloadToCacheFileW");
    if(!pDownload){
        wxLogSysError(wxT("URLDownloadToCacheFileW procedure not found"));
        return wxEmptyString;
    }

    wchar_t buffer[MAX_PATH + 1];
    HRESULT result = pDownload(NULL,url.wc_str(),buffer,MAX_PATH,0,NULL);
    if(result != S_OK){
        wxLogError(wxT("URLDownloadToCacheFile failed with code 0x%x"),(UINT)result);
        return wxEmptyString;
    }

    buffer[MAX_PATH] = 0;
    wxString path(buffer);
    return path;
}

/**
 * @brief Sends a request to Google Analytics
 * service gathering statistics of the use
 * of the program.
 * @details Based on http://code.google.com/apis/analytics/docs/
 * and http://www.vdgraaf.info/google-analytics-without-javascript.html
 */
void ga_request(const wxString& path)
{
    srand((unsigned int)time(NULL));
    int utmn = (rand() << 16) + rand();
    int utmhid = (rand() << 16) + rand();
    int cookie = (rand() << 16) + rand();
    int random = (rand() << 16) + rand();
    __int64 today = (__int64)time(NULL);

    wxString request;

    request << wxT("http://www.google-analytics.com/__utm.gif?utmwv=4.6.5");
    request << wxString::Format(wxT("&utmn=%u"),utmn);
    request << wxT("&utmhn=ultradefrag.sourceforge.net");
    request << wxString::Format(wxT("&utmhid=%u&utmr=-"),utmhid);
    request << wxT("&utmp=") << path;
    request << wxT("&utmac=");
    request << wxT("UA-15890458-1");
    request << wxString::Format(wxT("&utmcc=__utma%%3D%u.%u.%I64u.%I64u.%I64u.") \
        wxT("50%%3B%%2B__utmz%%3D%u.%I64u.27.2.utmcsr%%3Dgoogle.com%%7Cutmccn%%3D") \
        wxT("(referral)%%7Cutmcmd%%3Dreferral%%7Cutmcct%%3D%%2F%3B"),
        cookie,random,today,today,today,cookie,today);

    wxString url(request);
    wxString file = download(url);
    if(!file.IsEmpty())
        wxRemoveFile(file);
}

/** @} */
