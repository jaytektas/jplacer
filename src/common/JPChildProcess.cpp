// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPChildProcess.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

inline namespace jf {

namespace {

// How much one read takes at most (a read of more comes back with what there is).
constexpr size_t kChunk = 65536;

int msLeft(std::chrono::steady_clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    return int(std::max<long long>(0, left));
}

} // namespace

#if defined(_WIN32)

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// One argument quoted as Windows' command lines are parsed (CommandLineToArgvW's rules).
std::wstring quoted(const std::string& arg) {
    const std::wstring a = wide(arg);
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (size_t i = 0;; ++i) {
        size_t slashes = 0;
        while (i < a.size() && a[i] == L'\\') {
            ++i;
            ++slashes;
        }
        if (i == a.size()) {
            out.append(slashes * 2, L'\\');
            break;
        }
        if (a[i] == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        out += a[i];
    }
    return out + L"\"";
}

std::string lastError() {
    char text[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), 0, text, sizeof text, nullptr);
    std::string s = text;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '.')) s.pop_back();
    return s;
}

std::string pipeName() {
    static std::atomic<unsigned> next { 0 };
    return "\\\\.\\pipe\\jplacer-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++next);
}

} // namespace

// Each channel a named pipe, read and written overlapped, so several can be waited on together.
struct JPChildProcess::Impl {
    struct Pipe {
        HANDLE     handle = INVALID_HANDLE_VALUE;
        bool       fromChild = true;
        bool       connected = false;   // the child has opened it (channel 0: from the start)
        bool       pending = false;     // an overlapped operation (connecting, else reading) under way
        bool       closed = false;      // the child closed its end
        OVERLAPPED ov {};
        std::string have;               // read, not yet taken
        char        buf[kChunk];
    };
    PROCESS_INFORMATION process {};
    std::vector<std::unique_ptr<Pipe>> pipes;

    ~Impl() {
        for (auto& p : pipes) shut(*p);
        if (process.hProcess) CloseHandle(process.hProcess);
        if (process.hThread) CloseHandle(process.hThread);
    }
    static void shut(Pipe& p) {
        if (p.handle != INVALID_HANDLE_VALUE) {
            if (p.pending) {
                CancelIoEx(p.handle, &p.ov);
                DWORD n = 0;
                GetOverlappedResult(p.handle, &p.ov, &n, TRUE);
            }
            CloseHandle(p.handle);
        }
        if (p.ov.hEvent) CloseHandle(p.ov.hEvent);
        p.handle = INVALID_HANDLE_VALUE;
        p.ov.hEvent = nullptr;
        p.pending = false;
        p.closed = true;
    }
    // What an operation under way has done, once it has; then the next asked for (a read, once connected).
    void pump(Pipe& p, bool waitForIt) {
        if (p.closed || p.handle == INVALID_HANDLE_VALUE) return;
        if (p.pending) {
            DWORD n = 0;
            if (!GetOverlappedResult(p.handle, &p.ov, &n, waitForIt ? TRUE : FALSE)) {
                const DWORD e = GetLastError();
                if (e == ERROR_IO_INCOMPLETE) return;   // still under way
                p.pending = false;
                if (e == ERROR_PIPE_CONNECTED) p.connected = true;
                else {
                    p.closed = true;   // the child closed it, or never opened it
                    return;
                }
            } else {
                p.pending = false;
                if (!p.connected) p.connected = true;
                else p.have.append(p.buf, n);
            }
        }
        if (!p.connected) {
            // Waiting for the child to open it.
            ResetEvent(p.ov.hEvent);
            if (ConnectNamedPipe(p.handle, &p.ov)) p.connected = true;
            else if (GetLastError() == ERROR_PIPE_CONNECTED) p.connected = true;
            else if (GetLastError() == ERROR_IO_PENDING) {
                p.pending = true;
                return;
            } else {
                p.closed = true;
                return;
            }
        }
        if (p.fromChild && p.have.empty() && !p.pending) {
            ResetEvent(p.ov.hEvent);
            DWORD n = 0;
            if (ReadFile(p.handle, p.buf, DWORD(sizeof p.buf), &n, &p.ov)) p.have.append(p.buf, n);
            else if (GetLastError() == ERROR_IO_PENDING) p.pending = true;
            else p.closed = true;
        }
    }
};

JPChildProcess::JPChildProcess() : m_impl(std::make_unique<Impl>()) {}

std::unique_ptr<JPChildProcess> JPChildProcess::start(const Arguments& args, const Environment& env, const std::string& cwd,
                                                      const std::vector<Channel>& channels, std::string& why) {
    std::unique_ptr<JPChildProcess> c(new JPChildProcess());
    Impl& m = *c->m_impl;
    // Its output (channel 0), then each channel: a pipe this end of which is ours.
    std::vector<std::string> names;
    std::vector<Way> ways { Way::FromChild };
    std::vector<size_t> sizes { 0 };
    for (const Channel& ch : channels) {
        ways.push_back(ch.way);
        sizes.push_back(ch.bytes);
    }
    for (size_t i = 0; i < ways.size(); ++i) {
        auto p = std::make_unique<Impl::Pipe>();
        p->fromChild = ways[i] == Way::FromChild;
        const std::string name = pipeName();
        const DWORD room = DWORD(std::max(kChunk, sizes[i]));
        p->handle = CreateNamedPipeW(wide(name).c_str(),
                                     (p->fromChild ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND) | FILE_FLAG_OVERLAPPED
                                         | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, room, room, 0, nullptr);
        p->ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (p->handle == INVALID_HANDLE_VALUE || !p->ov.hEvent) {
            why = "a pipe for it could not be made: " + lastError();
            return nullptr;
        }
        names.push_back(name);
        m.pipes.push_back(std::move(p));
    }
    c->m_names.assign(names.begin() + 1, names.end());
    const std::vector<std::string> argv = args(c->m_names);
    if (argv.empty()) {
        why = "nothing to run";
        return nullptr;
    }
    // Its output: the other end of channel 0, its standard output and error, handed to it; nothing to read.
    SECURITY_ATTRIBUTES inherit { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE out = CreateFileW(wide(names[0]).c_str(), GENERIC_WRITE, 0, &inherit, OPEN_EXISTING, 0, nullptr);
    HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    if (out == INVALID_HANDLE_VALUE || in == INVALID_HANDLE_VALUE) {
        why = "its output could not be taken: " + lastError();
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
        return nullptr;
    }
    m.pipes[0]->connected = true;
    // Its command line and environment (a block of NUL-ended strings, ending in another).
    std::wstring line;
    for (const std::string& a : argv) line += (line.empty() ? L"" : L" ") + quoted(a);
    std::wstring block;
    std::vector<std::string> environment = env ? env(c->m_names) : std::vector<std::string>();
    for (const std::string& e : environment) block += wide(e) + L'\0';
    block += L'\0';
    STARTUPINFOW si {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = out;
    const std::wstring dir = wide(cwd);
    const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env ? block.data() : nullptr,
                                        dir.empty() ? nullptr : dir.c_str(), &si, &m.process);
    const std::string failed = started ? std::string() : lastError();
    CloseHandle(out);
    CloseHandle(in);
    if (!started) {
        why = argv[0] + " could not be started (" + failed + ")";
        return nullptr;
    }
    for (auto& p : m.pipes) m.pump(*p, false);   // the channels' connections, and its output's first read, under way
    return c;
}

JPChildProcess::~JPChildProcess() {
    if (!exited()) {
        kill();
        wait();
    }
}

int JPChildProcess::waitAny(const std::vector<size_t>& channels, int timeoutMs) {
    Impl& m = *m_impl;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        std::vector<HANDLE> events;
        for (const size_t c : channels) {
            if (c >= m.pipes.size()) continue;
            Impl::Pipe& p = *m.pipes[c];
            m.pump(p, false);
            if (!p.have.empty() || p.closed) return int(c);
            if (p.pending) events.push_back(p.ov.hEvent);
        }
        if (events.empty()) return -1;
        const int left = msLeft(deadline);
        if (WaitForMultipleObjects(DWORD(events.size()), events.data(), FALSE, DWORD(left)) == WAIT_TIMEOUT && left == 0) return -1;
        if (msLeft(deadline) == 0) {
            for (const size_t c : channels)   // one last look: what came as the time ran out
                if (c < m.pipes.size()) {
                    m.pump(*m.pipes[c], false);
                    if (!m.pipes[c]->have.empty() || m.pipes[c]->closed) return int(c);
                }
            return -1;
        }
    }
}

int JPChildProcess::read(size_t c, char* into, size_t n, int timeoutMs) {
    Impl& m = *m_impl;
    if (c >= m.pipes.size()) return -1;
    Impl::Pipe& p = *m.pipes[c];
    if (p.have.empty() && !p.closed && waitAny({ c }, timeoutMs) < 0) return 0;
    if (p.have.empty()) return p.closed ? -1 : 0;
    const size_t k = std::min(n, p.have.size());
    std::memcpy(into, p.have.data(), k);
    p.have.erase(0, k);
    m.pump(p, false);   // the next read under way
    return int(k);
}

size_t JPChildProcess::available(size_t c) {
    Impl& m = *m_impl;
    if (c >= m.pipes.size()) return 0;
    Impl::Pipe& p = *m.pipes[c];
    m.pump(p, false);
    DWORD more = 0;
    if (p.connected && !p.closed) PeekNamedPipe(p.handle, nullptr, 0, nullptr, &more, nullptr);
    return p.have.size() + more;
}

bool JPChildProcess::write(size_t c, const std::string& bytes) {
    Impl& m = *m_impl;
    if (c >= m.pipes.size() || m.pipes[c]->fromChild) return false;
    Impl::Pipe& p = *m.pipes[c];
    while (!p.connected && !p.closed) m.pump(p, true);   // the child opening it first
    if (p.closed) return false;
    size_t sent = 0;
    while (sent < bytes.size()) {
        OVERLAPPED ov {};
        ov.hEvent = p.ov.hEvent;
        ResetEvent(ov.hEvent);
        DWORD n = 0;
        if (!WriteFile(p.handle, bytes.data() + sent, DWORD(bytes.size() - sent), &n, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult(p.handle, &ov, &n, TRUE)) {
                Impl::shut(p);
                return false;
            }
        }
        sent += n;
    }
    return true;
}

void JPChildProcess::close(size_t c) {
    if (c < m_impl->pipes.size()) Impl::shut(*m_impl->pipes[c]);
}

std::optional<int> JPChildProcess::exited() {
    const HANDLE h = m_impl->process.hProcess;
    if (!h) return -1;
    if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) return std::nullopt;
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    return int(code);
}

int JPChildProcess::wait() {
    const HANDLE h = m_impl->process.hProcess;
    if (!h) return -1;
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    return int(code);
}

void JPChildProcess::stop() { kill(); }

void JPChildProcess::kill() {
    if (m_impl->process.hProcess) TerminateProcess(m_impl->process.hProcess, 1);
}

#else

namespace {

void closeFd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

} // namespace

// Each channel a pipe: our end a file descriptor, the child's handed to it as the number asked for.
struct JPChildProcess::Impl {
    pid_t            pid = -1;
    std::vector<int> fds;   // our ends, by channel
    std::optional<int> code;

    ~Impl() {
        for (int& fd : fds) closeFd(fd);
    }
};

JPChildProcess::JPChildProcess() : m_impl(std::make_unique<Impl>()) {}

std::unique_ptr<JPChildProcess> JPChildProcess::start(const Arguments& makeArgs, const Environment& env, const std::string& cwd,
                                                      const std::vector<Channel>& channels, std::string& why) {
    std::unique_ptr<JPChildProcess> c(new JPChildProcess());
    Impl& m = *c->m_impl;
    // Its output (its standard output and error), then each channel: ours one end, its the other.
    std::vector<int> theirs;
    std::vector<int> theirFd { -1 };
    std::vector<Way> ways { Way::FromChild };
    std::vector<size_t> sizes { 0 };
    for (const Channel& ch : channels) {
        ways.push_back(ch.way);
        theirFd.push_back(ch.fd);
        sizes.push_back(ch.bytes);
        c->m_names.push_back(std::to_string(ch.fd));
    }
    auto failed = [&](const std::string& what) {
        for (int& fd : theirs) closeFd(fd);
        why = what;
        return nullptr;
    };
    for (size_t i = 0; i < ways.size(); ++i) {
        int p[2];
        if (::pipe2(p, O_CLOEXEC) != 0) return failed("a pipe for it could not be made: " + std::string(std::strerror(errno)));
        if (sizes[i] > 0) ::fcntl(p[0], F_SETPIPE_SZ, int(sizes[i]));
        const bool fromChild = ways[i] == Way::FromChild;
        m.fds.push_back(fromChild ? p[0] : p[1]);
        theirs.push_back(fromChild ? p[1] : p[0]);
    }
    std::vector<std::string> args = makeArgs(c->m_names);
    if (args.empty()) return failed("nothing to run");
    std::vector<char*> argp;
    for (std::string& a : args) argp.push_back(a.data());
    argp.push_back(nullptr);
    std::vector<std::string> envs = env ? env(c->m_names) : std::vector<std::string>();
    std::vector<char*> envp;
    for (std::string& e : envs) envp.push_back(e.data());
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, theirs[0], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, theirs[0], STDERR_FILENO);
    for (size_t i = 1; i < theirs.size(); ++i) posix_spawn_file_actions_adddup2(&actions, theirs[i], theirFd[i]);
    if (!cwd.empty()) posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    const int failedSpawn = posix_spawnp(&m.pid, args[0].c_str(), &actions, nullptr, argp.data(), env ? envp.data() : environ);
    posix_spawn_file_actions_destroy(&actions);
    for (int& fd : theirs) closeFd(fd);   // the child's own now
    if (failedSpawn) {
        m.pid = -1;
        why = args[0] + " could not be started (" + std::strerror(failedSpawn) + ")";
        return nullptr;
    }
    return c;
}

JPChildProcess::~JPChildProcess() {
    for (int& fd : m_impl->fds) closeFd(fd);
    if (!exited()) {
        kill();
        wait();
    }
}

int JPChildProcess::waitAny(const std::vector<size_t>& channels, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<pollfd> ps;
    for (const size_t c : channels)
        ps.push_back({ c < m_impl->fds.size() ? m_impl->fds[c] : -1, POLLIN, 0 });
    for (;;) {
        const int r = ::poll(ps.data(), nfds_t(ps.size()), msLeft(deadline));
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        for (size_t i = 0; i < ps.size(); ++i)
            if (ps[i].revents & (POLLIN | POLLHUP | POLLERR)) return int(channels[i]);
        return -1;
    }
}

int JPChildProcess::read(size_t c, char* into, size_t n, int timeoutMs) {
    if (c >= m_impl->fds.size() || m_impl->fds[c] < 0) return -1;
    if (waitAny({ c }, timeoutMs) < 0) return 0;
    for (;;) {
        const ssize_t k = ::read(m_impl->fds[c], into, std::min(n, kChunk));
        if (k < 0 && errno == EINTR) continue;
        return k > 0 ? int(k) : -1;
    }
}

size_t JPChildProcess::available(size_t c) {
    int waiting = 0;
    if (c >= m_impl->fds.size() || m_impl->fds[c] < 0 || ::ioctl(m_impl->fds[c], FIONREAD, &waiting) != 0) return 0;
    return size_t(waiting);
}

bool JPChildProcess::write(size_t c, const std::string& bytes) {
    if (c >= m_impl->fds.size() || m_impl->fds[c] < 0) return false;
    size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t k = ::write(m_impl->fds[c], bytes.data() + sent, bytes.size() - sent);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        sent += size_t(k);
    }
    return true;
}

void JPChildProcess::close(size_t c) {
    if (c < m_impl->fds.size()) closeFd(m_impl->fds[c]);
}

std::optional<int> JPChildProcess::exited() {
    Impl& m = *m_impl;
    if (m.code || m.pid <= 0) return m.code ? m.code : std::optional<int>(-1);
    int status = 0;
    if (::waitpid(m.pid, &status, WNOHANG) != m.pid) return std::nullopt;
    m.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return m.code;
}

int JPChildProcess::wait() {
    Impl& m = *m_impl;
    if (m.code || m.pid <= 0) return m.code.value_or(-1);
    int status = 0;
    while (::waitpid(m.pid, &status, 0) < 0 && errno == EINTR) {}
    m.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return *m.code;
}

void JPChildProcess::stop() {
    if (m_impl->pid > 0 && !m_impl->code) ::kill(m_impl->pid, SIGTERM);
}

void JPChildProcess::kill() {
    if (m_impl->pid > 0 && !m_impl->code) ::kill(m_impl->pid, SIGKILL);
}

#endif

} // inline namespace jf
