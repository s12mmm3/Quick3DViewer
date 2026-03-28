#include "dumpcatcher.h"
#include "logger.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <Dbghelp.h>
#include <tchar.h>
#endif

namespace UINamespace {
QString g_appName;
#ifdef Q_OS_WIN
// 生成dump文件
int GenerateMiniDump(HANDLE hFile, PEXCEPTION_POINTERS pExceptionPointers)
{
    BOOL bOwnDumpFile = FALSE;
    HANDLE hDumpFile = hFile;
    MINIDUMP_EXCEPTION_INFORMATION ExpParam;

    typedef BOOL(WINAPI * MiniDumpWriteDumpT)(
        HANDLE,
        DWORD,
        HANDLE,
        MINIDUMP_TYPE,
        PMINIDUMP_EXCEPTION_INFORMATION,
        PMINIDUMP_USER_STREAM_INFORMATION,
        PMINIDUMP_CALLBACK_INFORMATION
        );

    MiniDumpWriteDumpT pfnMiniDumpWriteDump = NULL;
    HMODULE hDbgHelp = LoadLibrary(_T("DbgHelp.dll"));
    if (hDbgHelp)
        pfnMiniDumpWriteDump = (MiniDumpWriteDumpT)GetProcAddress(hDbgHelp, "MiniDumpWriteDump");

    if (pfnMiniDumpWriteDump)
    {
        if (hDumpFile == NULL || hDumpFile == INVALID_HANDLE_VALUE)
        {
            //TCHAR szPath[MAX_PATH] = { 0 };
            TCHAR szFileName[MAX_PATH] = { 0 };
            SYSTEMTIME stLocalTime;

            GetLocalTime(&stLocalTime);
            CreateDirectory(szFileName, NULL);

            wsprintf(szFileName, _T("%s-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp"),
                     g_appName.toStdWString().c_str(),
                     stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
                     stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond,
                     GetCurrentProcessId(), GetCurrentThreadId());
            hDumpFile = CreateFile(szFileName, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

            bOwnDumpFile = TRUE;
            OutputDebugString(szFileName);
        }

        if (hDumpFile != INVALID_HANDLE_VALUE)
        {
            ExpParam.ThreadId = GetCurrentThreadId();
            ExpParam.ExceptionPointers = pExceptionPointers;
            ExpParam.ClientPointers = FALSE;

            pfnMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                 hDumpFile, MiniDumpWithDataSegs, (pExceptionPointers ? &ExpParam : NULL), NULL, NULL);

            if (bOwnDumpFile)
                CloseHandle(hDumpFile);
        }
    }

    if (hDbgHelp != NULL)
        FreeLibrary(hDbgHelp);
    return EXCEPTION_EXECUTE_HANDLER;
}


LONG WINAPI ExceptionFilter(LPEXCEPTION_POINTERS lpExceptionInfo)
{
    if (IsDebuggerPresent())
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    return GenerateMiniDump(NULL, lpExceptionInfo);
}
#else
#include <sys/resource.h>

static bool enableCoreDump() {
    // set core file size to max.
    struct rlimit limit;
    int ret = getrlimit(RLIMIT_CORE, &limit);
    limit.rlim_cur = limit.rlim_max;
    return setrlimit(RLIMIT_CORE, &limit) == 0;
}
#endif

namespace DumpCatcher
{
void initDumpCatcher(QString appName)
{
    g_appName = appName;
    if (g_appName.isEmpty()) {
        g_appName = "Dump";
    }
    DEBUG << "Enable core dump: " << appName <<
#ifdef Q_OS_WIN
        reinterpret_cast<void*>(SetUnhandledExceptionFilter(ExceptionFilter));
#else
        enableCoreDump();
#endif
}
}
}
