#include "ChildProcessGuard.h"

#include <QLoggingCategory>
#include <QProcess>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#endif

Q_LOGGING_CATEGORY(lcChildGuard, "eagle.process")

#if defined(Q_OS_WIN)
namespace {

HANDLE killOnCloseJob()
{
    // Never closed on purpose: Windows closes it when Eagle exits (or crashes),
    // which terminates every process in the job.
    static HANDLE job = []() -> HANDLE {
        HANDLE handle = CreateJobObjectW(nullptr, nullptr);
        if (!handle) {
            return nullptr;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
        ZeroMemory(&info, sizeof(info));
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &info,
                                     sizeof(info))) {
            CloseHandle(handle);
            return nullptr;
        }
        return handle;
    }();
    return job;
}

} // namespace
#endif

void ChildProcessGuard::prepare(QProcess *process, bool lowerPriority)
{
    if (!process) {
        return;
    }
#if defined(Q_OS_LINUX)
    process->setChildProcessModifier([lowerPriority]() {
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (lowerPriority) {
            const int ignored = ::nice(5);
            (void)ignored;
        }
    });
#else
    Q_UNUSED(lowerPriority);
#endif
}

void ChildProcessGuard::adopt(QProcess *process)
{
    if (!process) {
        return;
    }
#if defined(Q_OS_WIN)
    HANDLE job = killOnCloseJob();
    const qint64 pid = process->processId();
    if (!job || pid <= 0) {
        return;
    }
    HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid));
    if (!child) {
        qCWarning(lcChildGuard) << "OpenProcess failed for pid" << pid << GetLastError();
        return;
    }
    if (!AssignProcessToJobObject(job, child)) {
        qCWarning(lcChildGuard) << "AssignProcessToJobObject failed for pid" << pid << GetLastError();
    }
    CloseHandle(child);
#endif
}
