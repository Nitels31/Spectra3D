#include "core/CrashLog.hpp"

#include "core/Log.hpp"
#include "core/Project.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <functional>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#if defined(SPECTRA_CRASH_SYMBOLS)
#include <dbghelp.h>
#endif
#else
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__has_include)
#if __has_include(<execinfo.h>)
#define SPECTRA_HAVE_EXECINFO 1
#include <execinfo.h>
#endif
#endif
#endif

namespace s3d {
namespace {

// How many frames of the stack the report holds.
constexpr int kMaxFrames = 32;

// The buffer the last words of the log are copied into. The ring buffer holds
// 256 messages of up to 192 bytes; this is enough for the newest of them.
constexpr size_t kLogExcerptSize = 16 * 1024;

// The size of the path of the report.
constexpr size_t kPathSize = 1024;

// The directory the report goes into, and the name of the program in it.
//
// Plain buffers and not std::string: the report is written from a handler
// that runs when the program is already broken, and the heap is one of the
// things that may be broken with it.
char g_directory[kPathSize] = {0};
char g_program[64] = {0};

// Set while a report is being written, and never cleared: a crash inside the
// crash handler must not start a second report.
//
// The machine can ask about a fault on any thread, so the flag is an atomic
// one: two faults at the same moment must still produce one report.
std::atomic<int> g_handling{0};

// The name of the program in the report: the engine's own, unless the program
// says that it is something else.
const char* programName() { return g_program[0] ? g_program : kEngineName; }

// Copies a string into a fixed buffer. Done by hand and not with snprintf,
// which the compiler would warn about when the buffer and the string are the
// same size - and the truncation it warns about cannot happen here.
void copyText(char* out, size_t size, const char* text) {
    if (!out || size == 0) return;
    size_t at = 0;
    while (text && text[at] && at + 1 < size) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

// The path of a file in the report's directory, put together by hand: the
// compiler cannot know how long the directory is, and a path that was cut in
// half is a path to a file that is not there.
bool joinPath(char* out, size_t size, const char* dir, const char* name) {
    size_t dirLength = 0;
    size_t nameLength = 0;
    while (dir && dir[dirLength]) ++dirLength;
    while (name && name[nameLength]) ++nameLength;
    if (dirLength + (dirLength ? 1 : 0) + nameLength + 1 > size) return false;
    size_t at = 0;
    for (size_t i = 0; i < dirLength; ++i) out[at++] = dir[i];
    if (dirLength) out[at++] = '/';
    for (size_t i = 0; i < nameLength; ++i) out[at++] = name[i];
    out[at] = '\0';
    return true;
}

// The report, written through a file handle and nothing else.
//
// A crash handler runs with the heap in an unknown state, so the report does
// not use it: every line is formatted into a stack buffer and written with a
// single call.
class Report {
public:
    // Opens the file, and only if it is not there: the report takes the first
    // free number, and two crashes at the same moment cannot take the same
    // name.
    bool open(const char* path) {
#if defined(_WIN32)
        handle_ = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                         _S_IREAD | _S_IWRITE);
#else
        handle_ = ::open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
#endif
        return handle_ >= 0;
    }

    void put(const char* text, size_t length) {
        if (handle_ < 0 || !text || length == 0) return;
        const char* cursor = text;
        size_t left = length;
        while (left > 0) {
#if defined(_WIN32)
            const int written = _write(handle_, cursor, (unsigned int)left);
            if (written <= 0) return;
#else
            const ssize_t written = ::write(handle_, cursor, left);
            if (written <= 0) return;
#endif
            cursor += written;
            left -= (size_t)written;
        }
    }

    void put(const char* text) { put(text, std::strlen(text)); }

    // One line of the report, formatted into a buffer of its own.
    void line(const char* fmt, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        const int length = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        if (length <= 0) return;
        size_t count = (size_t)length;
        if (count >= sizeof(buffer)) count = sizeof(buffer) - 1;
        put(buffer, count);
        put("\n", 1);
    }

    void close() {
        if (handle_ < 0) return;
#if defined(_WIN32)
        _close(handle_);
#else
        ::close(handle_);
#endif
        handle_ = -1;
    }

private:
    int handle_ = -1;
};

// The time of the crash, in the machine's own time zone.
const char* timeString() {
    static char buffer[32] = {0};
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (!local) return "";
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local);
    return buffer;
}

// The number of the process, so that a report can be told apart from the
// reports of the other programs on the machine.
unsigned long processNumber() {
#if defined(_WIN32)
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

// The number of the thread the crash happened on. The hash of the thread's
// id is enough to tell two threads apart in a report.
unsigned long threadNumber() {
    return (unsigned long)std::hash<std::thread::id>{}(std::this_thread::get_id());
}

#if defined(_WIN32) && defined(SPECTRA_CRASH_SYMBOLS)

// The symbols are read once, when the handlers are installed: reading them
// later means loading them while the program is dying, which is the worst
// moment to ask for more memory.
void initSymbols() {
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
}

// The name of the function at an address, or "" when there is none.
//
// The buffer is reused: the name is used at once and is never kept.
const char* symbolName(void* address) {
    static char buffer[512];
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = sizeof(buffer) - sizeof(SYMBOL_INFO);
    DWORD64 displacement = 0;
    if (!SymFromAddr(GetCurrentProcess(), (DWORD64)(uintptr_t)address, &displacement, symbol))
        return "";
    return symbol->Name;
}

#endif

// The stack the program was on when it died.
//
// The handler runs on the thread that died, so this is that thread's stack:
// the frames of the handler itself, and under them the frames of the code
// that broke.
void writeStack(Report& report) {
    report.line("");
    report.line("Stack");
    report.line("------");
    int count = 0;
#if defined(_WIN32)
    void* frames[kMaxFrames];
    count = (int)CaptureStackBackTrace(0, kMaxFrames, frames, nullptr);
    for (int i = 0; i < count; ++i) {
        const unsigned long long address = (unsigned long long)(uintptr_t)frames[i];
#if defined(SPECTRA_CRASH_SYMBOLS)
        const char* name = symbolName(frames[i]);
        if (name[0])
            report.line("  #%02d 0x%016llx %s", i, address, name);
        else
            report.line("  #%02d 0x%016llx", i, address);
#else
        report.line("  #%02d 0x%016llx", i, address);
#endif
    }
#elif defined(SPECTRA_HAVE_EXECINFO)
    void* frames[kMaxFrames];
    count = backtrace(frames, kMaxFrames);
    for (int i = 0; i < count; ++i) {
        const unsigned long long address = (unsigned long long)(uintptr_t)frames[i];
        Dl_info info;
        if (dladdr(frames[i], &info) && info.dli_sname) {
            const unsigned long long offset =
                (unsigned long long)((const char*)frames[i] - (const char*)info.dli_saddr);
            report.line("  #%02d 0x%016llx %s!%s+0x%llx", i, address,
                        info.dli_fname ? info.dli_fname : "?", info.dli_sname, offset);
        } else {
            report.line("  #%02d 0x%016llx", i, address);
        }
    }
#endif
    if (count == 0) report.line("  (no frames)");
}

// The last words of the log.
//
// The log lives in a buffer and the buffer dies with the process, so the
// report carries a copy of it: the ring buffer is still in memory when the
// handler runs, and it is the only account of what the program was doing.
void writeLogExcerpt(Report& report) {
    report.line("");
    report.line("The log before the crash");
    report.line("------------------------");
    static char excerpt[kLogExcerptSize];
    const size_t length = Log::copyEntries(excerpt, sizeof(excerpt));
    if (length == 0) {
        report.line("(nothing was logged, or the log was busy)");
        return;
    }
    report.put(excerpt, length);
}

// The report's file: crash-001.log, crash-002.log, and so on.
//
// The first free number is taken, so the reports of several crashes are all
// kept and they are in the order they happened.
bool openReport(Report& report, char* pathOut, size_t pathSize) {
    for (int number = 1; number < 1000; ++number) {
        char name[32];
        std::snprintf(name, sizeof(name), "crash-%03d.log", number);
        char path[kPathSize];
        if (!joinPath(path, sizeof(path), g_directory, name)) continue;
        if (!report.open(path)) continue;
        copyText(pathOut, pathSize, path);
        return true;
    }
    return false;
}

// The report itself: what happened, where, and what the program said last.
//
// `kind` says whether this is a crash or a failure the program survived, and
// `pathOut` receives the file the report went into.
void emitReport(const char* kind, const char* reason, const void* address, char* pathOut,
                size_t pathSize) {
    Report report;
    char path[kPathSize];
    if (!openReport(report, path, sizeof(path))) {
        // No file: at least say it on the console, where somebody will see it.
        std::fprintf(stderr, "%s: %s\n", kEngineName, reason ? reason : "the program stopped");
        copyText(pathOut, pathSize, "");
        return;
    }

    report.line("%s crash report", kEngineName);
    report.line("======================");
    report.line("kind:      %s", kind);
    report.line("program:   %s", programName());
    report.line("version:   %s", kEngineVersion);
    report.line("time:      %s", timeString());
    report.line("reason:    %s", reason ? reason : "unknown");
    if (address) report.line("address:   0x%016llx", (unsigned long long)(uintptr_t)address);
    report.line("process:   %lu", processNumber());
    report.line("thread:    %lu", threadNumber());
    writeStack(report);
    writeLogExcerpt(report);
    report.close();

    std::fprintf(stderr, "%s: %s\nReport: %s\n", kEngineName,
                 reason ? reason : "the program stopped", path);
    copyText(pathOut, pathSize, path);
}

// The name of a signal, and what it means.
const char* signalName(int sig) {
    switch (sig) {
        case SIGABRT: return "SIGABRT (the program stopped itself)";
        case SIGFPE: return "SIGFPE (a division that does not work)";
        case SIGILL: return "SIGILL (an instruction that does not exist)";
        case SIGSEGV: return "SIGSEGV (a memory was touched that was not there)";
#if defined(SIGBUS)
        case SIGBUS: return "SIGBUS (a memory that was not there)";
#endif
#if defined(SIGBREAK)
        case SIGBREAK: return "SIGBREAK (the program was interrupted)";
#endif
        default: return "a signal";
    }
}

#if defined(_WIN32)

// The name of a Windows exception.
const char* exceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
        default: return "an exception";
    }
}

// The exceptions that are worth a report.
//
// A C++ exception is not one of them: it is caught by the program that threw
// it, and a report for every caught exception would be a folder full of
// noise. Only the faults nobody catches get a file.
bool isFatalException(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_OVERFLOW:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            return true;
        default:
            return false;
    }
}

// The first thing the machine asks about a fault.
//
// A vectored handler is called before anything else, so it is the only place
// that sees the address of the fault and the code that came with it. It only
// writes the report: the fault goes on to whoever was going to handle it, and
// the program still dies the way it was going to.
LONG WINAPI onVectoredException(EXCEPTION_POINTERS* pointers) {
    const EXCEPTION_RECORD* record = pointers ? pointers->ExceptionRecord : nullptr;
    const DWORD code = record ? record->ExceptionCode : 0;
    if (!isFatalException(code)) return EXCEPTION_CONTINUE_SEARCH;
    if (!g_handling.exchange(1)) {
        char reason[160];
        std::snprintf(reason, sizeof(reason), "%s (0x%08lx)", exceptionName(code),
                      (unsigned long)code);
        emitReport("crash", reason, record ? record->ExceptionAddress : nullptr, nullptr, 0);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// A signal the C runtime caught for us. The runtime calls this on a thread of
// its own, which is the only way a handler survives a stack that has run out.
void onAbortSignal(int sig) {
    if (!g_handling.exchange(1)) {
        emitReport("crash", signalName(sig), nullptr, nullptr, 0);
    }
    // The runtime ends the process after the handler returns.
}

#else

// A fault the machine caught for us.
void onPosixSignal(int sig, siginfo_t* info, void*) {
    if (!g_handling.exchange(1)) {
        const void* address = nullptr;
        if (info && (sig == SIGSEGV || sig == SIGBUS)) address = info->si_addr;
        emitReport("crash", signalName(sig), address, nullptr, 0);
    }
    // The program dies the way it would have died: the default action for the
    // signal, so the exit code is the one the machine expects and a core dump
    // is made if the machine makes them.
    struct sigaction dfl;
    std::memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, nullptr);
    raise(sig);
}

#endif

// An exception that got out of the program.
//
// The engine does not throw, but the libraries it uses do, and an exception
// that reaches the end of the program is a crash that nobody was there to
// see.
void onTerminate() {
    if (!g_handling.exchange(1)) {
        // The exception is not thrown again to be read: a rethrow inside a
        // terminate handler starts a second unwinding, and the program is
        // already in the state where the first one found no handler. The
        // message of the exception is in the report of the program that
        // caught it - see main().
        const bool hadException = std::current_exception() != nullptr;
        emitReport("crash",
                   hadException ? "an exception got out of the program"
                                : "the program stopped itself",
                   nullptr, nullptr, 0);
    }
    std::abort();
}

} // namespace

void CrashLog::install() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    // The symbols are read now, while the program is healthy.
#if defined(_WIN32) && defined(SPECTRA_CRASH_SYMBOLS)
    initSymbols();
#endif

#if defined(_WIN32)
    // The vectored handler: it is called before anything else, so it is the
    // only place that sees the address of the fault and the code that came
    // with it. It only writes the report - the fault goes on to whoever was
    // going to handle it, and the program still dies the way it was going to.
    AddVectoredExceptionHandler(1, onVectoredException);
    signal(SIGABRT, onAbortSignal);
    signal(SIGFPE, onAbortSignal);
    signal(SIGILL, onAbortSignal);
    signal(SIGSEGV, onAbortSignal);
    signal(SIGBREAK, onAbortSignal);
#else
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = onPosixSignal;
    // SA_RESETHAND puts the default action back when the signal is delivered,
    // so a second fault is not caught a second time.
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
#endif

    // An exception that got out of the program.
    std::set_terminate(onTerminate);
}

void CrashLog::setProgramName(const char* name) {
    if (!name) return;
    copyText(g_program, sizeof(g_program), name);
}

void CrashLog::setDirectory(const char* dir) {
    if (!dir) return;
    copyText(g_directory, sizeof(g_directory), dir);
}

const char* CrashLog::directory() { return g_directory; }

std::string CrashLog::write(const char* what) {
    char path[kPathSize] = {0};
    emitReport("report", what ? what : "the program stopped", nullptr, path, sizeof(path));
    if (!path[0]) return std::string();
    Log::info("Crash report: '%s' written", path);
    return std::string(path);
}

} // namespace s3d
