#ifndef COMPAT_TYPES_H
#define COMPAT_TYPES_H

/* Cross-platform compat types (Phase 1 of cross_platform_plan.md).
 * Replaces the legacy Win32 system/audio headers for the native 64-bit
 * SDL2/OpenGL target. Game logic includes this instead of the OS header.
 *
 * Original addresses are unaffected; this header only renames the
 * Win32 vocabulary to portable C99+SDL types. HWND/HINSTANCE/HMODULE
 * are opaque void* handles (GxMode.hInstance/hwnd are ignored fields).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* --- integer / string vocabulary --- */
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;
typedef int      BOOL;
typedef unsigned int UINT;
typedef intptr_t LRESULT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef int      HRESULT;
typedef void    *HANDLE;
typedef void    *HWND;
typedef void    *HINSTANCE;
typedef void    *HMODULE;
typedef void    *HCURSOR;
typedef void    *HBRUSH;
typedef void    *HMENU;
typedef void    *HICON;
#ifndef _WIN32
typedef uint8_t byte; /* MinGW rpcndr.h provided this via windows.h */
#endif
typedef const char *LPCSTR;
typedef char       *LPSTR;
typedef const void *LPCVOID;
typedef void       *LPVOID;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((void *)(intptr_t)-1)
#endif

/* Calling-convention residue: original binary used __stdcall/__cdecl for
 * WinMain/polls; the native target uses the default C convention. */
#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef CDECL
#define CDECL
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif

/* ZeroMemory -> memset (Phase 1 trivial swap). */
#ifndef ZeroMemory
#define ZeroMemory(dst, n) memset((dst), 0, (n))
#endif

/* --- trivial string swaps (Phase 1.2) ---
 * Kept as macros so any remaining wsprintfA/lstrcpyA call sites compile
 * during the migration; new code uses snprintf/strcpy directly. */
#ifndef wsprintfA
#define wsprintfA snprintf
#endif
#ifndef wsprintf
#define wsprintf snprintf
#endif
#ifndef lstrcpyA
#define lstrcpyA strcpy
#endif
#ifndef lstrcpy
#define lstrcpy strcpy
#endif
/* lstrcpyn(dst,src,n): copy at most n-1 chars + NUL (Win32 semantics). */
#ifndef lstrcpynA
#define lstrcpynA(dst, src, n) snprintf((dst), (size_t)(n), "%s", (src))
#endif
#ifndef lstrcpyn
#define lstrcpyn(dst, src, n) snprintf((dst), (size_t)(n), "%s", (src))
#endif

/* MessageBox style bits kept for address-comment history only. */
#ifndef MB_ICONERROR
#define MB_ICONERROR 0x10
#endif
#ifndef IDC_ARROW
#define IDC_ARROW 32512
#endif

/* 64-bit port: runtime structs holding native pointers change size with
 * the pointer width, so their original 32-bit sizes are asserted only on
 * 32-bit builds. Serialized on-disk layout assertions stay active on
 * every host (see cross_platform_plan.md Phase 1.3). */
#if UINTPTR_MAX == UINT32_MAX
#define MANIAC_32BIT_RUNTIME_LAYOUT 1
#else
#define MANIAC_32BIT_RUNTIME_LAYOUT 0
#endif

/* Win32 error shim: GetLastError() -> errno on the native target. */
#ifndef GetLastError
#define GetLastError() ((unsigned long)errno)
#endif

#endif /* COMPAT_TYPES_H */
