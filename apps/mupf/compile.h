// mupf's compilers: a typesetting source shown as the PDF its compiler makes of it, made again on every save. `mupf
// FILE.typ` runs `typst watch FILE.typ OUT.pdf`, `mupf FILE.tex` runs `latexmk -pvc -pdf` (each found on PATH when it is
// run), into a directory of mupf's own; the window shows OUT.pdf and opens it again when a compile has written it, and
// shows the compiler's diagnostics when one fails (the last good PDF stays). No win32 here (compile_native.cpp, POSIX):
// the window reads fd() in its message loop and calls take() when it is readable.
#pragma once
#include <string>
#include <sys/types.h>
#include <vector>

namespace mupf {

class Compiler {
public:
    enum Kind { NONE, TYPST, LATEX };
    static Kind kind_of(const std::string& path);           // by the file's extension: .typ, .tex
    static const char* program(Kind k);                     // "typst", "latexmk"
    static std::string find_program(const char* name);      // its path on PATH, "" when it is not there
    // removes the directories PARENT/PREFIX<pid> of processes that are gone (a mupf killed before it could)
    static void sweep(const std::string& parent, const std::string& prefix);

    // what the compiler said, a compile at a time
    struct Result {
        enum What { COMPILED, FAILED, ENDED } what = COMPILED;
        std::string summary;    // its status line without the time ("compiled successfully in 18.35 ms")
        std::string text;       // the diagnostics (errors, warnings), "" when none
    };

    Compiler() = default;
    ~Compiler() { stop(); }
    Compiler(const Compiler&) = delete;
    Compiler& operator=(const Compiler&) = delete;

    // starts the compiler on src writing into dir (created): false with *err when it cannot be run
    bool start(Kind k, const std::string& src, const std::string& dir, std::string* err);
    // ends it (SIGTERM to its process group, SIGKILL after a second) and removes dir and what is in it
    void stop();
    bool running() const { return pid_ > 0; }
    int fd() const { return fd_; }
    Kind kind() const { return kind_; }
    const std::string& source() const { return src_; }
    const std::string& output() const { return out_; }       // the PDF
    // reads what is pending (never blocks); the compiles that ended since, in order. A failed compile's diagnostics may come
    // in more than one read: each read with more of them repeats the FAILED, its text the whole so far.
    std::vector<Result> take();

private:
    void line(const std::string& l, std::vector<Result>* out);
    void reap();
    Kind kind_ = NONE;
    pid_t pid_ = -1;
    int fd_ = -1;
    std::string src_, dir_, out_, partial_;
    // typst: the diagnostics after "compiled with errors" (or warnings) until its next "watching" block; latexmk: the
    // lines of a run until its "=== Watching for updated files"
    bool collecting_ = false, failed_ = false;
    std::string summary_;
    std::vector<std::string> lines_;
    size_t sent_ = 0;           // the diagnostics' lines a FAILED has had
};

}   // namespace mupf
