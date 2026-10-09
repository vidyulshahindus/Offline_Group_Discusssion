#pragma once

class QProcess;

// Makes helper processes (whisper-server, Piper workers) die with Eagle, even if
// Eagle crashes, so no orphan holds a port or RAM (spec §22.5 "no orphan helpers").
//   Windows: the child joins a job object with KILL_ON_JOB_CLOSE.
//   Linux:   the child gets PR_SET_PDEATHSIG(SIGKILL) and, optionally, nice +5.
namespace ChildProcessGuard {

// Call before QProcess::start().
void prepare(QProcess *process, bool lowerPriority = false);
// Call after the process has started (needs its PID).
void adopt(QProcess *process);

} // namespace ChildProcessGuard
