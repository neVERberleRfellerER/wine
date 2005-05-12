/*
 *                 Shell basics
 *
 * Copyright 1998 Marcus Meissner
 * Copyright 1998 Juergen Schmied (jsch)  *  <juergen.schmied@metronet.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winreg.h"
#include "dlgs.h"
#include "shellapi.h"
#include "winuser.h"
#include "wingdi.h"
#include "shlobj.h"
#include "shlguid.h"
#include "shlwapi.h"

#include "undocshell.h"
#include "pidl.h"
#include "shell32_main.h"
#include "version.h"
#include "shresdef.h"

#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

extern const char * const SHELL_Authors[];

#define MORE_DEBUG 1
/*************************************************************************
 * CommandLineToArgvW            [SHELL32.@]
 *
 * We must interpret the quotes in the command line to rebuild the argv
 * array correctly:
 * - arguments are separated by spaces or tabs
 * - quotes serve as optional argument delimiters
 *   '"a b"'   -> 'a b'
 * - escaped quotes must be converted back to '"'
 *   '\"'      -> '"'
 * - an odd number of '\'s followed by '"' correspond to half that number
 *   of '\' followed by a '"' (extension of the above)
 *   '\\\"'    -> '\"'
 *   '\\\\\"'  -> '\\"'
 * - an even number of '\'s followed by a '"' correspond to half that number
 *   of '\', plus a regular quote serving as an argument delimiter (which
 *   means it does not appear in the result)
 *   'a\\"b c"'   -> 'a\b c'
 *   'a\\\\"b c"' -> 'a\\b c'
 * - '\' that are not followed by a '"' are copied literally
 *   'a\b'     -> 'a\b'
 *   'a\\b'    -> 'a\\b'
 *
 * Note:
 * '\t' == 0x0009
 * ' '  == 0x0020
 * '"'  == 0x0022
 * '\\' == 0x005c
 */
LPWSTR* WINAPI CommandLineToArgvW(LPCWSTR lpCmdline, int* numargs)
{
    DWORD argc;
    HGLOBAL hargv;
    LPWSTR  *argv;
    LPCWSTR cs;
    LPWSTR arg,s,d;
    LPWSTR cmdline;
    int in_quotes,bcount;

    if (*lpCmdline==0)
    {
        /* Return the path to the executable */
        DWORD len, size=16;

        hargv=GlobalAlloc(size, 0);
        argv=GlobalLock(hargv);
        for (;;)
        {
            len = GetModuleFileNameW(0, (LPWSTR)(argv+1), size-sizeof(LPWSTR));
            if (!len)
            {
                GlobalFree(hargv);
                return NULL;
            }
            if (len < size) break;
            size*=2;
            hargv=GlobalReAlloc(hargv, size, 0);
            argv=GlobalLock(hargv);
        }
        argv[0]=(LPWSTR)(argv+1);
        if (numargs)
            *numargs=2;

        return argv;
    }

    /* to get a writeable copy */
    argc=0;
    bcount=0;
    in_quotes=0;
    cs=lpCmdline;
    while (1)
    {
        if (*cs==0 || ((*cs==0x0009 || *cs==0x0020) && !in_quotes))
        {
            /* space */
            argc++;
            /* skip the remaining spaces */
            while (*cs==0x0009 || *cs==0x0020) {
                cs++;
            }
            if (*cs==0)
                break;
            bcount=0;
            continue;
        }
        else if (*cs==0x005c)
        {
            /* '\', count them */
            bcount++;
        }
        else if ((*cs==0x0022) && ((bcount & 1)==0))
        {
            /* unescaped '"' */
            in_quotes=!in_quotes;
            bcount=0;
        }
        else
        {
            /* a regular character */
            bcount=0;
        }
        cs++;
    }
    /* Allocate in a single lump, the string array, and the strings that go with it.
     * This way the caller can make a single GlobalFree call to free both, as per MSDN.
     */
    hargv=GlobalAlloc(0, argc*sizeof(LPWSTR)+(strlenW(lpCmdline)+1)*sizeof(WCHAR));
    argv=GlobalLock(hargv);
    if (!argv)
        return NULL;
    cmdline=(LPWSTR)(argv+argc);
    strcpyW(cmdline, lpCmdline);

    argc=0;
    bcount=0;
    in_quotes=0;
    arg=d=s=cmdline;
    while (*s)
    {
        if ((*s==0x0009 || *s==0x0020) && !in_quotes)
        {
            /* Close the argument and copy it */
            *d=0;
            argv[argc++]=arg;

            /* skip the remaining spaces */
            do {
                s++;
            } while (*s==0x0009 || *s==0x0020);

            /* Start with a new argument */
            arg=d=s;
            bcount=0;
        }
        else if (*s==0x005c)
        {
            /* '\\' */
            *d++=*s++;
            bcount++;
        }
        else if (*s==0x0022)
        {
            /* '"' */
            if ((bcount & 1)==0)
            {
                /* Preceded by an even number of '\', this is half that
                 * number of '\', plus a quote which we erase.
                 */
                d-=bcount/2;
                in_quotes=!in_quotes;
                s++;
            }
            else
            {
                /* Preceded by an odd number of '\', this is half that
                 * number of '\' followed by a '"'
                 */
                d=d-bcount/2-1;
                *d++='"';
                s++;
            }
            bcount=0;
        }
        else
        {
            /* a regular character */
            *d++=*s++;
            bcount=0;
        }
    }
    if (*arg)
    {
        *d='\0';
        argv[argc++]=arg;
    }
    if (numargs)
        *numargs=argc;

    return argv;
}

static DWORD shgfi_get_exe_type(LPCWSTR szFullPath)
{
    BOOL status = FALSE;
    HANDLE hfile;
    DWORD BinaryType;
    IMAGE_DOS_HEADER mz_header;
    IMAGE_NT_HEADERS nt;
    DWORD len;
    char magic[4];

    status = GetBinaryTypeW (szFullPath, &BinaryType);
    if (!status)
        return 0;
    if (BinaryType == SCS_DOS_BINARY || BinaryType == SCS_PIF_BINARY)
        return 0x4d5a;

    hfile = CreateFileW( szFullPath, GENERIC_READ, FILE_SHARE_READ,
                         NULL, OPEN_EXISTING, 0, 0 );
    if ( hfile == INVALID_HANDLE_VALUE )
        return 0;

    /*
     * The next section is adapted from MODULE_GetBinaryType, as we need
     * to examine the image header to get OS and version information. We
     * know from calling GetBinaryTypeA that the image is valid and either
     * an NE or PE, so much error handling can be omitted.
     * Seek to the start of the file and read the header information.
     */

    SetFilePointer( hfile, 0, NULL, SEEK_SET );
    ReadFile( hfile, &mz_header, sizeof(mz_header), &len, NULL );

    SetFilePointer( hfile, mz_header.e_lfanew, NULL, SEEK_SET );
    ReadFile( hfile, magic, sizeof(magic), &len, NULL );
    if ( *(DWORD*)magic == IMAGE_NT_SIGNATURE )
    {
        SetFilePointer( hfile, mz_header.e_lfanew, NULL, SEEK_SET );
        ReadFile( hfile, &nt, sizeof(nt), &len, NULL );
        CloseHandle( hfile );
        if (nt.OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI)
        {
             return IMAGE_NT_SIGNATURE | 
                   (nt.OptionalHeader.MajorSubsystemVersion << 24) |
                   (nt.OptionalHeader.MinorSubsystemVersion << 16);
        }
        return IMAGE_NT_SIGNATURE;
    }
    else if ( *(WORD*)magic == IMAGE_OS2_SIGNATURE )
    {
        IMAGE_OS2_HEADER ne;
        SetFilePointer( hfile, mz_header.e_lfanew, NULL, SEEK_SET );
        ReadFile( hfile, &ne, sizeof(ne), &len, NULL );
        CloseHandle( hfile );
        if (ne.ne_exetyp == 2)
            return IMAGE_OS2_SIGNATURE | (ne.ne_expver << 16);
        return 0;
    }
    CloseHandle( hfile );
    return 0;
}

#define SHGFI_KNOWN_FLAGS \
    (SHGFI_SMALLICON | SHGFI_OPENICON | SHGFI_SHELLICONSIZE | SHGFI_PIDL | \
     SHGFI_USEFILEATTRIBUTES | SHGFI_ADDOVERLAYS | SHGFI_OVERLAYINDEX | \
     SHGFI_ICON | SHGFI_DISPLAYNAME | SHGFI_TYPENAME | SHGFI_ATTRIBUTES | \
     SHGFI_ICONLOCATION | SHGFI_EXETYPE | SHGFI_SYSICONINDEX | \
     SHGFI_LINKOVERLAY | SHGFI_SELECTED | SHGFI_ATTR_SPECIFIED)

/*************************************************************************
 * SHGetFileInfoW            [SHELL32.@]
 *
 */
DWORD WINAPI SHGetFileInfoW(LPCWSTR path,DWORD dwFileAttributes,
                            SHFILEINFOW *psfi, UINT sizeofpsfi, UINT flags )
{
    WCHAR szLocation[MAX_PATH], szFullPath[MAX_PATH];
    int iIndex;
    DWORD ret = TRUE, dwAttributes = 0;
    IShellFolder * psfParent = NULL;
    IExtractIconW * pei = NULL;
    LPITEMIDLIST    pidlLast = NULL, pidl = NULL;
    HRESULT hr = S_OK;
    BOOL IconNotYetLoaded=TRUE;

    TRACE("%s fattr=0x%lx sfi=%p(attr=0x%08lx) size=0x%x flags=0x%x\n",
          (flags & SHGFI_PIDL)? "pidl" : debugstr_w(path), dwFileAttributes,
          psfi, psfi->dwAttributes, sizeofpsfi, flags);

    if ( (flags & SHGFI_USEFILEATTRIBUTES) && 
         (flags & (SHGFI_ATTRIBUTES|SHGFI_EXETYPE|SHGFI_PIDL)))
        return FALSE;

    /* windows initializes this values regardless of the flags */
    if (psfi != NULL)
    {
        psfi->szDisplayName[0] = '\0';
        psfi->szTypeName[0] = '\0';
        psfi->iIcon = 0;
    }

    if (!(flags & SHGFI_PIDL))
    {
        /* SHGetFileInfo should work with absolute and relative paths */
        if (PathIsRelativeW(path))
        {
            GetCurrentDirectoryW(MAX_PATH, szLocation);
            PathCombineW(szFullPath, szLocation, path);
        }
        else
        {
            lstrcpynW(szFullPath, path, MAX_PATH);
        }
    }

    if (flags & SHGFI_EXETYPE)
    {
        if (flags != SHGFI_EXETYPE)
            return 0;
        return shgfi_get_exe_type(szFullPath);
    }

    /*
     * psfi is NULL normally to query EXE type. If it is NULL, none of the
     * below makes sense anyway. Windows allows this and just returns FALSE
     */
    if (psfi == NULL)
        return FALSE;

    /*
     * translate the path into a pidl only when SHGFI_USEFILEATTRIBUTES
     * is not specified.
     * The pidl functions fail on not existing file names
     */

    if (flags & SHGFI_PIDL)
    {
        pidl = ILClone((LPCITEMIDLIST)path);
    }
    else if (!(flags & SHGFI_USEFILEATTRIBUTES))
    {
        hr = SHILCreateFromPathW(szFullPath, &pidl, &dwAttributes);
    }

    if ((flags & SHGFI_PIDL) || !(flags & SHGFI_USEFILEATTRIBUTES))
    {
        /* get the parent shellfolder */
        if (pidl)
        {
            hr = SHBindToParent( pidl, &IID_IShellFolder, (LPVOID*)&psfParent,
                                (LPCITEMIDLIST*)&pidlLast );
            ILFree(pidl);
        }
        else
        {
            ERR("pidl is null!\n");
            return FALSE;
        }
    }

    /* get the attributes of the child */
    if (SUCCEEDED(hr) && (flags & SHGFI_ATTRIBUTES))
    {
        if (!(flags & SHGFI_ATTR_SPECIFIED))
        {
            psfi->dwAttributes = 0xffffffff;
        }
        IShellFolder_GetAttributesOf( psfParent, 1, (LPCITEMIDLIST*)&pidlLast,
                                      &(psfi->dwAttributes) );
    }

    /* get the displayname */
    if (SUCCEEDED(hr) && (flags & SHGFI_DISPLAYNAME))
    {
        if (flags & SHGFI_USEFILEATTRIBUTES)
        {
            lstrcpyW (psfi->szDisplayName, PathFindFileNameW(szFullPath));
        }
        else
        {
            STRRET str;
            hr = IShellFolder_GetDisplayNameOf( psfParent, pidlLast,
                                                SHGDN_INFOLDER, &str);
            StrRetToStrNW (psfi->szDisplayName, MAX_PATH, &str, pidlLast);
        }
    }

    /* get the type name */
    if (SUCCEEDED(hr) && (flags & SHGFI_TYPENAME))
    {
        static const WCHAR szFile[] = { 'F','i','l','e',0 };
        static const WCHAR szDashFile[] = { '-','f','i','l','e',0 };

        if (!(flags & SHGFI_USEFILEATTRIBUTES))
        {
            char ftype[80];

            _ILGetFileType(pidlLast, ftype, 80);
            MultiByteToWideChar(CP_ACP, 0, ftype, -1, psfi->szTypeName, 80 );
        }
        else
        {
            if (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                strcatW (psfi->szTypeName, szFile);
            else 
            {
                WCHAR sTemp[64];

                lstrcpyW(sTemp,PathFindExtensionW(szFullPath));
                if (!( HCR_MapTypeToValueW(sTemp, sTemp, 64, TRUE) &&
                    HCR_MapTypeToValueW(sTemp, psfi->szTypeName, 80, FALSE )))
                {
                    lstrcpynW (psfi->szTypeName, sTemp, 64);
                    strcatW (psfi->szTypeName, szDashFile);
                }
            }
        }
    }

    /* ### icons ###*/
    if (flags & SHGFI_ADDOVERLAYS)
        FIXME("SHGFI_ADDOVERLAYS unhandled\n");

    if (flags & SHGFI_OVERLAYINDEX)
        FIXME("SHGFI_OVERLAYINDEX unhandled\n");

    if (flags & SHGFI_LINKOVERLAY)
        FIXME("set icon to link, stub\n");

    if (flags & SHGFI_SELECTED)
        FIXME("set icon to selected, stub\n");

    if (flags & SHGFI_SHELLICONSIZE)
        FIXME("set icon to shell size, stub\n");

    /* get the iconlocation */
    if (SUCCEEDED(hr) && (flags & SHGFI_ICONLOCATION ))
    {
        UINT uDummy,uFlags;

        hr = IShellFolder_GetUIObjectOf(psfParent, 0, 1,
               (LPCITEMIDLIST*)&pidlLast, &IID_IExtractIconA,
               &uDummy, (LPVOID*)&pei);
        if (SUCCEEDED(hr))
        {
            hr = IExtractIconW_GetIconLocation(pei, 
                    (flags & SHGFI_OPENICON)? GIL_OPENICON : 0,
                    szLocation, MAX_PATH, &iIndex, &uFlags);
            psfi->iIcon = iIndex;

            if (uFlags != GIL_NOTFILENAME)
                lstrcpyW (psfi->szDisplayName, szLocation);
            else
                ret = FALSE;

            IExtractIconA_Release(pei);
        }
    }

    /* get icon index (or load icon)*/
    if (SUCCEEDED(hr) && (flags & (SHGFI_ICON | SHGFI_SYSICONINDEX)))
    {
        if (flags & SHGFI_USEFILEATTRIBUTES)
        {
            WCHAR sTemp [MAX_PATH];
            WCHAR * szExt;
            DWORD dwNr=0;

            lstrcpynW(sTemp, szFullPath, MAX_PATH);

            if (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                psfi->iIcon = SIC_GetIconIndex(swShell32Name, -IDI_SHELL_FOLDER);
            else
            {
                static const WCHAR p1W[] = {'%','1',0};

                psfi->iIcon = 0;
                szExt = (LPWSTR) PathFindExtensionW(sTemp);
                if ( szExt &&
                     HCR_MapTypeToValueW(szExt, sTemp, MAX_PATH, TRUE) &&
                     HCR_GetDefaultIconW(sTemp, sTemp, MAX_PATH, &dwNr))
                {
                    if (!lstrcmpW(p1W,sTemp))            /* icon is in the file */
                        strcpyW(sTemp, szFullPath);

                    if (flags & SHGFI_SYSICONINDEX) 
                    {
                        psfi->iIcon = SIC_GetIconIndex(sTemp,dwNr);
                        if (psfi->iIcon == -1)
                            psfi->iIcon = 0;
                    }
                    else 
                    {
                        IconNotYetLoaded=FALSE;
                        if (flags & SHGFI_SMALLICON)
                            PrivateExtractIconsW( sTemp,dwNr,
                                GetSystemMetrics( SM_CXSMICON ),
                                GetSystemMetrics( SM_CYSMICON ),
                                &psfi->hIcon, 0, 1, 0);
                        else
                            PrivateExtractIconsW( sTemp, dwNr,
                                GetSystemMetrics( SM_CXICON),
                                GetSystemMetrics( SM_CYICON),
                                &psfi->hIcon, 0, 1, 0);
                        psfi->iIcon = dwNr;
                    }
                }
            }
        }
        else
        {
            if (!(PidlToSicIndex(psfParent, pidlLast, !(flags & SHGFI_SMALLICON),
                (flags & SHGFI_OPENICON)? GIL_OPENICON : 0, &(psfi->iIcon))))
            {
                ret = FALSE;
            }
        }
        if (ret)
        {
            if (flags & SHGFI_SMALLICON)
                ret = (DWORD) ShellSmallIconList;
            else
                ret = (DWORD) ShellBigIconList;
        }
    }

    /* icon handle */
    if (SUCCEEDED(hr) && (flags & SHGFI_ICON) && IconNotYetLoaded)
    {
        if (flags & SHGFI_SMALLICON)
            psfi->hIcon = ImageList_GetIcon( ShellSmallIconList, psfi->iIcon, ILD_NORMAL);
        else
            psfi->hIcon = ImageList_GetIcon( ShellBigIconList, psfi->iIcon, ILD_NORMAL);
    }

    if (flags & ~SHGFI_KNOWN_FLAGS)
        FIXME("unknown flags %08x\n", flags & ~SHGFI_KNOWN_FLAGS);

    if (psfParent)
        IShellFolder_Release(psfParent);

    if (hr != S_OK)
        ret = FALSE;

    if (pidlLast)
        SHFree(pidlLast);

#ifdef MORE_DEBUG
    TRACE ("icon=%p index=0x%08x attr=0x%08lx name=%s type=%s ret=0x%08lx\n",
           psfi->hIcon, psfi->iIcon, psfi->dwAttributes,
           debugstr_w(psfi->szDisplayName), debugstr_w(psfi->szTypeName), ret);
#endif

    return ret;
}

/*************************************************************************
 * SHGetFileInfoA            [SHELL32.@]
 */
DWORD WINAPI SHGetFileInfoA(LPCSTR path,DWORD dwFileAttributes,
                              SHFILEINFOA *psfi, UINT sizeofpsfi,
                              UINT flags )
{
    INT len;
    LPWSTR temppath;
    DWORD ret;
    SHFILEINFOW temppsfi;

    if (flags & SHGFI_PIDL)
    {
        /* path contains a pidl */
        temppath = (LPWSTR) path;
    }
    else
    {
        len = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
        temppath = HeapAlloc(GetProcessHeap(), 0, len*sizeof(WCHAR));
        MultiByteToWideChar(CP_ACP, 0, path, -1, temppath, len);
    }

    if (psfi && (flags & SHGFI_ATTR_SPECIFIED))
        temppsfi.dwAttributes=psfi->dwAttributes;

    if (psfi == NULL)
        ret = SHGetFileInfoW(temppath, dwFileAttributes, NULL, sizeof(temppsfi), flags);
    else
        ret = SHGetFileInfoW(temppath, dwFileAttributes, &temppsfi, sizeof(temppsfi), flags);

    if (psfi)
    {
        if(flags & SHGFI_ICON)
            psfi->hIcon=temppsfi.hIcon;
        if(flags & (SHGFI_SYSICONINDEX|SHGFI_ICON|SHGFI_ICONLOCATION))
            psfi->iIcon=temppsfi.iIcon;
        if(flags & SHGFI_ATTRIBUTES)
            psfi->dwAttributes=temppsfi.dwAttributes;
        if(flags & (SHGFI_DISPLAYNAME|SHGFI_ICONLOCATION))
        {
            WideCharToMultiByte(CP_ACP, 0, temppsfi.szDisplayName, -1,
                  psfi->szDisplayName, sizeof(psfi->szDisplayName), NULL, NULL);
        }
        if(flags & SHGFI_TYPENAME)
        {
            WideCharToMultiByte(CP_ACP, 0, temppsfi.szTypeName, -1,
                  psfi->szTypeName, sizeof(psfi->szTypeName), NULL, NULL);
        }
    }

    if (!(flags & SHGFI_PIDL))
        HeapFree(GetProcessHeap(), 0, temppath);

    return ret;
}

/*************************************************************************
 * DuplicateIcon            [SHELL32.@]
 */
HICON WINAPI DuplicateIcon( HINSTANCE hInstance, HICON hIcon)
{
    ICONINFO IconInfo;
    HICON hDupIcon = 0;

    TRACE("%p %p\n", hInstance, hIcon);

    if (GetIconInfo(hIcon, &IconInfo))
    {
        hDupIcon = CreateIconIndirect(&IconInfo);

        /* clean up hbmMask and hbmColor */
        DeleteObject(IconInfo.hbmMask);
        DeleteObject(IconInfo.hbmColor);
    }

    return hDupIcon;
}

/*************************************************************************
 * ExtractIconA                [SHELL32.@]
 */
HICON WINAPI ExtractIconA(HINSTANCE hInstance, LPCSTR lpszFile, UINT nIconIndex)
{   
    HICON ret;
    INT len = MultiByteToWideChar(CP_ACP, 0, lpszFile, -1, NULL, 0);
    LPWSTR lpwstrFile = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));

    TRACE("%p %s %d\n", hInstance, lpszFile, nIconIndex);

    MultiByteToWideChar(CP_ACP, 0, lpszFile, -1, lpwstrFile, len);
    ret = ExtractIconW(hInstance, lpwstrFile, nIconIndex);
    HeapFree(GetProcessHeap(), 0, lpwstrFile);

    return ret;
}

/*************************************************************************
 * ExtractIconW                [SHELL32.@]
 */
HICON WINAPI ExtractIconW(HINSTANCE hInstance, LPCWSTR lpszFile, UINT nIconIndex)
{
    HICON  hIcon = NULL;
    UINT ret;
    UINT cx = GetSystemMetrics(SM_CXICON), cy = GetSystemMetrics(SM_CYICON);

    TRACE("%p %s %d\n", hInstance, debugstr_w(lpszFile), nIconIndex);

    if (nIconIndex == 0xFFFFFFFF)
    {
        ret = PrivateExtractIconsW(lpszFile, 0, cx, cy, NULL, NULL, 0, LR_DEFAULTCOLOR);
        if (ret != 0xFFFFFFFF && ret)
            return (HICON)ret;
        return NULL;
    }
    else
        ret = PrivateExtractIconsW(lpszFile, nIconIndex, cx, cy, &hIcon, NULL, 1, LR_DEFAULTCOLOR);

    if (ret == 0xFFFFFFFF)
        return (HICON)1;
    else if (ret > 0 && hIcon)
        return hIcon;

    return NULL;
}

typedef struct
{
    LPCWSTR  szApp;
    LPCWSTR  szOtherStuff;
    HICON hIcon;
    HFONT hFont;
} ABOUT_INFO;

#define IDC_STATIC_TEXT1   100
#define IDC_STATIC_TEXT2   101
#define IDC_LISTBOX        99
#define IDC_WINE_TEXT      98

#define DROP_FIELD_TOP    (-15)
#define DROP_FIELD_HEIGHT  15

static BOOL __get_dropline( HWND hWnd, LPRECT lprect )
{
    HWND hWndCtl = GetDlgItem(hWnd, IDC_WINE_TEXT);

    if( hWndCtl )
    {
        GetWindowRect( hWndCtl, lprect );
        MapWindowPoints( 0, hWnd, (LPPOINT)lprect, 2 );
        lprect->bottom = (lprect->top += DROP_FIELD_TOP);
        return TRUE;
    }
    return FALSE;
}

/*************************************************************************
 * SHAppBarMessage            [SHELL32.@]
 */
UINT WINAPI SHAppBarMessage(DWORD msg, PAPPBARDATA data)
{
    int width=data->rc.right - data->rc.left;
    int height=data->rc.bottom - data->rc.top;
    RECT rec=data->rc;

    switch (msg)
    {
    case ABM_GETSTATE:
        return ABS_ALWAYSONTOP | ABS_AUTOHIDE;
    case ABM_GETTASKBARPOS:
        GetWindowRect(data->hWnd, &rec);
        data->rc=rec;
        return TRUE;
    case ABM_ACTIVATE:
        SetActiveWindow(data->hWnd);
        return TRUE;
    case ABM_GETAUTOHIDEBAR:
        data->hWnd=GetActiveWindow();
        return TRUE;
    case ABM_NEW:
        SetWindowPos(data->hWnd,HWND_TOP,rec.left,rec.top,
                          width,height,SWP_SHOWWINDOW);
        return TRUE;
    case ABM_QUERYPOS:
        GetWindowRect(data->hWnd, &(data->rc));
        return TRUE;
    case ABM_REMOVE:
        FIXME("ABM_REMOVE broken\n");
        /* FIXME: this is wrong; should it be DestroyWindow instead? */
        /*CloseHandle(data->hWnd);*/
        return TRUE;
    case ABM_SETAUTOHIDEBAR:
        SetWindowPos(data->hWnd,HWND_TOP,rec.left+1000,rec.top,
                         width,height,SWP_SHOWWINDOW);
        return TRUE;
    case ABM_SETPOS:
        data->uEdge=(ABE_RIGHT | ABE_LEFT);
        SetWindowPos(data->hWnd,HWND_TOP,data->rc.left,data->rc.top,
                     width,height,SWP_SHOWWINDOW);
        return TRUE;
    case ABM_WINDOWPOSCHANGED:
        return TRUE;
    }
    return FALSE;
}

/*************************************************************************
 * SHHelpShortcuts_RunDLLA        [SHELL32.@]
 *
 */
DWORD WINAPI SHHelpShortcuts_RunDLLA(DWORD dwArg1, DWORD dwArg2, DWORD dwArg3, DWORD dwArg4)
{
    FIXME("(%lx, %lx, %lx, %lx) stub!\n", dwArg1, dwArg2, dwArg3, dwArg4);
    return 0;
}

/*************************************************************************
 * SHHelpShortcuts_RunDLLA        [SHELL32.@]
 *
 */
DWORD WINAPI SHHelpShortcuts_RunDLLW(DWORD dwArg1, DWORD dwArg2, DWORD dwArg3, DWORD dwArg4)
{
    FIXME("(%lx, %lx, %lx, %lx) stub!\n", dwArg1, dwArg2, dwArg3, dwArg4);
    return 0;
}

/*************************************************************************
 * SHLoadInProc                [SHELL32.@]
 * Create an instance of specified object class from within
 * the shell process and release it immediately
 */
HRESULT WINAPI SHLoadInProc (REFCLSID rclsid)
{
    void *ptr = NULL;

    TRACE("%s\n", debugstr_guid(rclsid));

    CoCreateInstance(rclsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown,&ptr);
    if(ptr)
    {
        IUnknown * pUnk = ptr;
        IUnknown_Release(pUnk);
        return NOERROR;
    }
    return DISP_E_MEMBERNOTFOUND;
}

/*************************************************************************
 * AboutDlgProc            (internal)
 */
INT_PTR CALLBACK AboutDlgProc( HWND hWnd, UINT msg, WPARAM wParam,
                              LPARAM lParam )
{
    HWND hWndCtl;

    TRACE("\n");

    switch(msg)
    {
    case WM_INITDIALOG:
        {
            ABOUT_INFO *info = (ABOUT_INFO *)lParam;
            WCHAR Template[512], AppTitle[512];

            if (info)
            {
                const char* const *pstr = SHELL_Authors;
                SendDlgItemMessageW(hWnd, stc1, STM_SETICON,(WPARAM)info->hIcon, 0);
                GetWindowTextW( hWnd, Template, sizeof(Template)/sizeof(WCHAR) );
                sprintfW( AppTitle, Template, info->szApp );
                SetWindowTextW( hWnd, AppTitle );
                SetWindowTextW( GetDlgItem(hWnd, IDC_STATIC_TEXT1), info->szApp );
                SetWindowTextW( GetDlgItem(hWnd, IDC_STATIC_TEXT2), info->szOtherStuff );
                hWndCtl = GetDlgItem(hWnd, IDC_LISTBOX);
                SendMessageW( hWndCtl, WM_SETREDRAW, 0, 0 );
                SendMessageW( hWndCtl, WM_SETFONT, (WPARAM)info->hFont, 0 );
                while (*pstr)
                {
                    WCHAR name[64];
                    /* authors list is in iso-8859-1 format */
                    MultiByteToWideChar( 28591, 0, *pstr, -1, name, sizeof(name)/sizeof(WCHAR) );
                    SendMessageW( hWndCtl, LB_ADDSTRING, (WPARAM)-1, (LPARAM)name );
                    pstr++;
                }
                SendMessageW( hWndCtl, WM_SETREDRAW, 1, 0 );
            }
        }
        return 1;

    case WM_PAINT:
        {
            RECT rect;
            PAINTSTRUCT ps;
            HDC hDC = BeginPaint( hWnd, &ps );

            if (__get_dropline( hWnd, &rect ))
            {
                SelectObject( hDC, GetStockObject( BLACK_PEN ) );
                MoveToEx( hDC, rect.left, rect.top, NULL );
                LineTo( hDC, rect.right, rect.bottom );
            }
            EndPaint( hWnd, &ps );
        }
    break;

    case WM_COMMAND:
        if (wParam == IDOK || wParam == IDCANCEL)
        {
            EndDialog(hWnd, TRUE);
            return TRUE;
        }
        break;
    case WM_CLOSE:
      EndDialog(hWnd, TRUE);
      break;
    }

    return 0;
}


/*************************************************************************
 * ShellAboutA                [SHELL32.288]
 */
BOOL WINAPI ShellAboutA( HWND hWnd, LPCSTR szApp, LPCSTR szOtherStuff, HICON hIcon )
{
    BOOL ret;
    LPWSTR appW = NULL, otherW = NULL;
    int len;

    if (szApp)
    {
        len = MultiByteToWideChar(CP_ACP, 0, szApp, -1, NULL, 0);
        appW = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
        MultiByteToWideChar(CP_ACP, 0, szApp, -1, appW, len);
    }
    if (szOtherStuff)
    {
        len = MultiByteToWideChar(CP_ACP, 0, szOtherStuff, -1, NULL, 0);
        otherW = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
        MultiByteToWideChar(CP_ACP, 0, szOtherStuff, -1, otherW, len);
    }

    ret = ShellAboutW(hWnd, appW, otherW, hIcon);

    HeapFree(GetProcessHeap(), 0, otherW);
    HeapFree(GetProcessHeap(), 0, appW);
    return ret;
}


/*************************************************************************
 * ShellAboutW                [SHELL32.289]
 */
BOOL WINAPI ShellAboutW( HWND hWnd, LPCWSTR szApp, LPCWSTR szOtherStuff,
                             HICON hIcon )
{
    ABOUT_INFO info;
    LOGFONTW logFont;
    HRSRC hRes;
    LPVOID template;
    BOOL bRet;
    static const WCHAR wszSHELL_ABOUT_MSGBOX[] =
        {'S','H','E','L','L','_','A','B','O','U','T','_','M','S','G','B','O','X',0};

    TRACE("\n");

    if(!(hRes = FindResourceW(shell32_hInstance, wszSHELL_ABOUT_MSGBOX, (LPWSTR)RT_DIALOG)))
        return FALSE;
    if(!(template = (LPVOID)LoadResource(shell32_hInstance, hRes)))
        return FALSE;
    info.szApp        = szApp;
    info.szOtherStuff = szOtherStuff;
    info.hIcon        = hIcon ? hIcon : LoadIconW( 0, (LPWSTR)IDI_WINLOGO );

    SystemParametersInfoW( SPI_GETICONTITLELOGFONT, 0, &logFont, 0 );
    info.hFont = CreateFontIndirectW( &logFont );

    bRet = DialogBoxIndirectParamW((HINSTANCE)GetWindowLongPtrW( hWnd, GWLP_HINSTANCE ),
                                   template, hWnd, AboutDlgProc, (LPARAM)&info );
    DeleteObject(info.hFont);
    return bRet;
}

/*************************************************************************
 * FreeIconList (SHELL32.@)
 */
void WINAPI FreeIconList( DWORD dw )
{
    FIXME("%lx: stub\n",dw);
}


/*************************************************************************
 * ShellDDEInit (SHELL32.@)
 */
void WINAPI ShellDDEInit(BOOL start)
{
    FIXME("stub: %d\n", start);
}

/***********************************************************************
 * DllGetVersion [SHELL32.@]
 *
 * Retrieves version information of the 'SHELL32.DLL'
 *
 * PARAMS
 *     pdvi [O] pointer to version information structure.
 *
 * RETURNS
 *     Success: S_OK
 *     Failure: E_INVALIDARG
 *
 * NOTES
 *     Returns version of a shell32.dll from IE4.01 SP1.
 */

HRESULT WINAPI SHELL32_DllGetVersion (DLLVERSIONINFO *pdvi)
{
    /* FIXME: shouldn't these values come from the version resource? */
    if (pdvi->cbSize == sizeof(DLLVERSIONINFO) ||
        pdvi->cbSize == sizeof(DLLVERSIONINFO2))
    {
        pdvi->dwMajorVersion = WINE_FILEVERSION_MAJOR;
        pdvi->dwMinorVersion = WINE_FILEVERSION_MINOR;
        pdvi->dwBuildNumber = WINE_FILEVERSION_BUILD;
        pdvi->dwPlatformID = WINE_FILEVERSION_PLATFORMID;
        if (pdvi->cbSize == sizeof(DLLVERSIONINFO2))
        {
            DLLVERSIONINFO2 *pdvi2 = (DLLVERSIONINFO2 *)pdvi;

            pdvi2->dwFlags = 0;
            pdvi2->ullVersion = MAKEDLLVERULL(WINE_FILEVERSION_MAJOR,
                                              WINE_FILEVERSION_MINOR,
                                              WINE_FILEVERSION_BUILD,
                                              WINE_FILEVERSION_PLATFORMID);
        }
        TRACE("%lu.%lu.%lu.%lu\n",
              pdvi->dwMajorVersion, pdvi->dwMinorVersion,
              pdvi->dwBuildNumber, pdvi->dwPlatformID);
        return S_OK;
    }
    else
    {
        WARN("wrong DLLVERSIONINFO size from app\n");
        return E_INVALIDARG;
    }
}

/*************************************************************************
 * global variables of the shell32.dll
 * all are once per process
 *
 */
HINSTANCE    shell32_hInstance = 0;
HIMAGELIST   ShellSmallIconList = 0;
HIMAGELIST   ShellBigIconList = 0;


/*************************************************************************
 * SHELL32 DllMain
 *
 * NOTES
 *  calling oleinitialize here breaks sone apps.
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID fImpLoad)
{
    TRACE("%p 0x%lx %p\n", hinstDLL, fdwReason, fImpLoad);

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        shell32_hInstance = hinstDLL;
        DisableThreadLibraryCalls(shell32_hInstance);

        /* get full path to this DLL for IExtractIconW_fnGetIconLocation() */
        GetModuleFileNameW(hinstDLL, swShell32Name, MAX_PATH);
        swShell32Name[MAX_PATH - 1] = '\0';

        InitCommonControlsEx(NULL);

        SIC_Initialize();
        SYSTRAY_Init();
        InitChangeNotifications();
        break;

    case DLL_PROCESS_DETACH:
        shell32_hInstance = 0;
        SIC_Destroy();
        FreeChangeNotifications();
        break;
    }
    return TRUE;
}

/*************************************************************************
 * DllInstall         [SHELL32.@]
 *
 * PARAMETERS
 *
 *    BOOL bInstall - TRUE for install, FALSE for uninstall
 *    LPCWSTR pszCmdLine - command line (unused by shell32?)
 */

HRESULT WINAPI SHELL32_DllInstall(BOOL bInstall, LPCWSTR cmdline)
{
    FIXME("%s %s: stub\n", bInstall ? "TRUE":"FALSE", debugstr_w(cmdline));
    return S_OK;        /* indicate success */
}

/***********************************************************************
 *              DllCanUnloadNow (SHELL32.@)
 */
HRESULT WINAPI SHELL32_DllCanUnloadNow(void)
{
    FIXME("stub\n");
    return S_FALSE;
}
