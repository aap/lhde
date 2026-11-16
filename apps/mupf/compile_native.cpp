// mupf's compilers (compile.h): typst watch and latexmk -pvc as children, their output read through a pipe.
#include "compile.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include "compat.h"   // lhde_parent_death_signal

namespace mupf {

namespace {

bool ends_with(const std::string& s, const char* t)
{
    size_t n = strlen(t);
    return s.size() >= n && s.compare(s.size() - n, n, t) == 0;
}

std::string base_name(const std::string& p)
{
    size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

std::string dir_name(const std::string& p)
{
    size_t s = p.rfind('/');
    return s == std::string::npos ? "." : s == 0 ? "/" : p.substr(0, s);
}

// a terminal's escape sequences out (CSI ... letter): typst colours its diagnostics on a terminal only, latexmk not at all,
// but a wrapper script may
std::string plain(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\033') {
            if (i + 1 < s.size() && s[i + 1] == '[') {
                i += 2;
                while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) i++;
            }
            continue;
        }
        if (s[i] != '\r') o += s[i];
    }
    return o;
}

void remove_tree(const std::string& dir)
{
    DIR* d = opendir(dir.c_str());
    if (d) {
        while (dirent* e = readdir(d)) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            std::string p = dir + "/" + e->d_name;
            struct stat st;
            if (lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(p);
            else unlink(p.c_str());
        }
        closedir(d);
    }
    rmdir(dir.c_str());
}

bool make_dirs(const std::string& dir)
{
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/') {
            std::string p = dir.substr(0, i);
            if (mkdir(p.c_str(), 0700) != 0 && errno != EEXIST) return false;
        }
    return true;
}

}   // namespace

void Compiler::sweep(const std::string& parent, const std::string& prefix)
{
    DIR* d = opendir(parent.c_str());
    if (!d) return;
    std::vector<std::string> gone;
    while (dirent* e = readdir(d)) {
        if (strncmp(e->d_name, prefix.c_str(), prefix.size())) continue;
        char* end = nullptr;
        long pid = strtol(e->d_name + prefix.size(), &end, 10);
        if (pid <= 0 || *end || pid == (long)getpid()) continue;
        if (kill((pid_t)pid, 0) != 0 && errno == ESRCH) gone.push_back(parent + "/" + e->d_name);
    }
    closedir(d);
    for (const std::string& g : gone) remove_tree(g);
}

Compiler::Kind Compiler::kind_of(const std::string& path)
{
    if (ends_with(path, ".typ")) return TYPST;
    if (ends_with(path, ".tex")) return LATEX;
    return NONE;
}

const char* Compiler::program(Kind k) { return k == TYPST ? "typst" : k == LATEX ? "latexmk" : ""; }

std::string Compiler::find_program(const char* name)
{
    const char* path = getenv("PATH");
    std::string p = path && *path ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t at = 0;
    while (at <= p.size()) {
        size_t colon = p.find(':', at);
        if (colon == std::string::npos) colon = p.size();
        std::string dir = p.substr(at, colon - at);
        if (dir.empty()) dir = ".";
        std::string f = dir + "/" + name;
        struct stat st;
        if (stat(f.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(f.c_str(), X_OK) == 0) return f;
        at = colon + 1;
    }
    return "";
}

bool Compiler::start(Kind k, const std::string& src, const std::string& dir, std::string* err)
{
    stop();
    std::string prog = find_program(program(k));
    if (prog.empty()) { *err = std::string(program(k)) + " is not installed (not on PATH)"; return false; }
    if (!make_dirs(dir)) { *err = "cannot make " + dir + ": " + strerror(errno); return false; }
    std::string stem = base_name(src);
    stem = stem.substr(0, stem.rfind('.'));
    std::string out = dir + "/" + stem + ".pdf";
    // typst watch: its status and diagnostics on stderr, no colours (not a terminal anyway); --no-serve: no HTTP server
    // (it serves HTML exports alone, but asks for a port). latexmk -pvc: -view=none (it would start a viewer of its own),
    // TeX not stopping for input on an error, the files in dir; run in the source's directory, as TeX looks for the
    // \input files there
    std::vector<std::string> args;
    if (k == TYPST)
        args = { prog, "--color", "never", "watch", "--no-serve", src, out };
    else
        args = { prog, "-pvc", "-pdf", "-view=none", "-interaction=nonstopmode", "-halt-on-error", "-outdir=" + dir,
                 base_name(src) };
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);
    int fds[2];
    if (pipe(fds) < 0) { *err = strerror(errno); return false; }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    std::string cwd = dir_name(src);
    pid_t parent = getpid(), pid = fork();
    if (pid < 0) { *err = strerror(errno); close(fds[0]); close(fds[1]); return false; }
    if (pid == 0) {
        // its own process group (latexmk's TeX runs end with it), ended with mupf if mupf dies without stopping it
        setpgid(0, 0);
        lhde_parent_death_signal(SIGTERM);
        if (getppid() != parent) _exit(1);
        signal(SIGCHLD, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) { dup2(nul, 0); close(nul); }
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        if (k == LATEX && chdir(cwd.c_str()) != 0) _exit(126);
        execv(argv[0], argv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    kind_ = k;
    pid_ = pid;
    fd_ = fds[0];
    src_ = src;
    dir_ = dir;
    out_ = out;
    partial_.clear();
    collecting_ = failed_ = false;
    summary_.clear();
    lines_.clear();
    return true;
}

void Compiler::reap()
{
    if (pid_ <= 0) return;
    kill(-pid_, SIGTERM);
    for (int i = 0; i < 100; i++) {   // a second for it to end
        int st;
        pid_t w = waitpid(pid_, &st, WNOHANG);
        if (w == pid_ || (w < 0 && errno == ECHILD)) { pid_ = -1; break; }
        struct timespec ts = { 0, 10 * 1000000 };
        nanosleep(&ts, nullptr);
    }
    if (pid_ > 0) {
        kill(-pid_, SIGKILL);
        while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
        pid_ = -1;
    }
}

void Compiler::stop()
{
    reap();
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
    if (!dir_.empty()) remove_tree(dir_);
    dir_.clear();
    kind_ = NONE;
}

// typst watch prints, a compile at a time: "watching FILE", "writing to OUT", "[HH:MM:SS] compiling ...", then
// "[HH:MM:SS] compiled successfully in 18.35 ms" (or "compiled with warnings", "compiled with errors") and the
// diagnostics after it. latexmk -pvc prints its run and the TeX log's lines, then "=== Watching for updated files. Use
// ctrl/C to stop ..."; a failed run has "Latexmk: Errors, so I did not complete making targets" and TeX's "! message"
// lines, each followed by its "l.N context" line.
void Compiler::line(const std::string& raw, std::vector<Result>* out)
{
    std::string l = plain(raw);
    if (kind_ == TYPST) {
        size_t close = l.size() > 2 && l[0] == '[' ? l.find("] ") : std::string::npos;
        std::string status = close != std::string::npos ? l.substr(close + 2) : "";
        if (!status.compare(0, 8, "compiled")) {
            collecting_ = true;
            failed_ = status.find("with errors") != std::string::npos;
            summary_ = status;
            lines_.clear();
            sent_ = 0;
            if (!failed_) {
                Result r;
                r.what = Result::COMPILED;
                r.summary = summary_;
                out->push_back(r);
            }
            return;
        }
        if (!l.compare(0, 9, "watching ") || !status.compare(0, 9, "compiling")) { collecting_ = false; return; }
        if (!collecting_) {
            if (!l.empty() && l.compare(0, 11, "writing to ")) lines_.push_back(l);   // (before any compile: a bad argument)
            return;
        }
        if (!l.empty() || !lines_.empty()) lines_.push_back(l);
        return;
    }
    // latexmk
    if (!l.compare(0, 31, "=== Watching for updated files.")) {
        Result r;
        r.what = failed_ ? Result::FAILED : Result::COMPILED;
        r.summary = failed_ ? "compiled with errors" : "compiled successfully";
        for (const std::string& e : lines_) r.text += e + "\n";
        out->push_back(r);
        lines_.clear();
        failed_ = collecting_ = false;
        return;
    }
    if (!l.compare(0, 15, "Latexmk: Errors") || !l.compare(0, 16, "Latexmk: Failure")) failed_ = true;
    if (!l.empty() && l[0] == '!') { lines_.push_back(l); collecting_ = true; return; }
    if (collecting_ && !l.compare(0, 2, "l.")) { lines_.push_back(l); collecting_ = false; }
}

std::vector<Compiler::Result> Compiler::take()
{
    std::vector<Result> out;
    if (fd_ < 0) return out;
    bool ended = false;
    char buf[4096];
    for (;;) {
        ssize_t n = read(fd_, buf, sizeof buf);
        if (n > 0) { partial_.append(buf, (size_t)n); continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n == 0) ended = true;   // (EAGAIN: all read)
        break;
    }
    size_t at = 0, nl;
    while ((nl = partial_.find('\n', at)) != std::string::npos) {
        line(partial_.substr(at, nl - at), &out);
        at = nl + 1;
    }
    partial_.erase(0, at);
    if (ended && !partial_.empty()) { line(partial_, &out); partial_.clear(); }
    // typst: a failed compile's diagnostics as far as they came (more may follow: FAILED again with all of them)
    if (kind_ == TYPST && failed_ && collecting_ && lines_.size() != sent_) {
        sent_ = lines_.size();
        Result r;
        r.what = Result::FAILED;
        r.summary = summary_;
        std::vector<std::string> t = lines_;
        while (!t.empty() && t.back().empty()) t.pop_back();
        for (const std::string& e : t) r.text += e + "\n";
        out.push_back(r);
    }
    if (ended) {
        Result r;
        r.what = Result::ENDED;
        int st = 0;
        if (pid_ > 0 && waitpid(pid_, &st, 0) == pid_) {
            pid_ = -1;
            char b[64];
            if (WIFEXITED(st)) snprintf(b, sizeof b, "exited with status %d", WEXITSTATUS(st));
            else snprintf(b, sizeof b, "ended by signal %d", WIFSIGNALED(st) ? WTERMSIG(st) : 0);
            r.summary = b;
        } else r.summary = "ended";
        if (kind_ == TYPST && !collecting_)
            for (const std::string& e : lines_) r.text += e + "\n";
        out.push_back(r);
        close(fd_);
        fd_ = -1;
    }
    return out;
}

}   // namespace mupf
