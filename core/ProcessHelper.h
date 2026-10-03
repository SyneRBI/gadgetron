#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
// The C++ platform headers don't always expose the POSIX `environ` symbol
// (macOS); declare it (matches <stdlib.h>'s declaration) so all POSIX
// platforms see it.
extern char **environ;
#else
#include <stdexcept>
// MSVC declares popen/pclose in <process.h>. This file was deliberately
// named ProcessHelper.h (not Process.h) because on Windows, a file named
// Process.h in core/ (on the include path) would shadow the CRT's
// <process.h> for every std header that includes it (case-insensitive FS).
// Declare the CRT functions directly instead of including <process.h>.
extern "C" {
FILE *popen(const char *, const char *);
int pclose(FILE *);
}
#endif

// Self-contained process helper. Replaces boost::process, whose v1 API
// (search_path / args / std_out> / ipstream / limit_handles) was dropped in
// Boost 1.88+ (conda-forge now ships only the v2 API).
// On Windows only the path helpers are functional; spawning external
// processes (MATLAB/Python/Julia nodes) is unsupported and throws.
namespace Gadgetron::Process {

    namespace detail {
        std::lock_guard<std::mutex> obtain_process_lock();
    }

    // Resolve `name` against $PATH (replaces boost::process::search_path).
    inline std::filesystem::path search_path(const std::string &name) {
        if (name.empty())
            return {};
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
            return name;
        const char *path_env = std::getenv("PATH");
        const std::string dirs = path_env ? path_env : "";
        std::string cur;
        for (size_t i = 0; i <= dirs.size(); ++i) {
            if (i == dirs.size() || dirs[i] == ':' || dirs[i] == ';') {
                if (!cur.empty()) {
                    std::error_code ec;
                    const std::filesystem::path candidate = std::filesystem::path(cur) / name;
                    if (std::filesystem::exists(candidate, ec)
#ifndef _WIN32
                        && ::access(candidate.c_str(), X_OK) == 0
#endif
                    )
                        return candidate;
                }
                cur.clear();
            } else {
                cur.push_back(dirs[i]);
            }
        }
        return {};
    }

    // Run a shell command and capture its stdout (replaces the boost::process::system
    // calls that redirected stdout into a std::future<std::string>).
    inline std::string capture_output(const std::string &command) {
        std::string result;
        if (FILE *pipe = ::popen(command.c_str(), "r")) {
            char buf[4096];
            while (std::fgets(buf, sizeof buf, pipe))
                result += buf;
            ::pclose(pipe);
        }
        return result;
    }

    // RAII handle to a spawned child process (POSIX fork/exec), replacing boost::process::child.
#ifndef _WIN32
    class child {
    public:
        child() = default;

        child(std::filesystem::path program,
              std::vector<std::string> args,
              std::vector<std::pair<std::string, std::string>> env = {},
              bool null_stdout = false,
              bool null_stderr = false) {
            auto guard = detail::obtain_process_lock();

            // Final environment = current environ with `env` overrides applied.
            std::vector<std::string> final_env;
            for (char **e = ::environ; e && *e; ++e)
                final_env.push_back(*e);
            for (auto &kv : env) {
                const std::string prefix = kv.first + "=";
                bool replaced = false;
                for (auto &entry : final_env) {
                    if (entry.rfind(prefix, 0) == 0) {
                        entry = prefix + kv.second;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced)
                    final_env.push_back(prefix + kv.second);
            }

            // Keep every string's bytes alive across fork/exec.
            std::vector<std::vector<char>> store;
            std::vector<char *> argvp, envp;
            auto intern = [&](std::vector<char *> &out, const std::string &s) {
                store.emplace_back(s.begin(), s.end());
                store.back().push_back('\0'); // NUL-terminate: execve takes C strings
                out.push_back(store.back().data());
            };
            intern(argvp, program.string());
            for (auto &a : args)
                intern(argvp, a);
            argvp.push_back(nullptr);
            for (auto &s : final_env)
                intern(envp, s);
            envp.push_back(nullptr);

            pid_t pid = ::fork();
            if (pid == 0) {
                if (null_stdout) {
                    int fd = ::open("/dev/null", O_WRONLY);
                    if (fd >= 0) ::dup2(fd, 1);
                }
                if (null_stderr) {
                    int fd = ::open("/dev/null", O_WRONLY);
                    if (fd >= 0) ::dup2(fd, 2);
                }
                // execvpe is a GNU extension (not on macOS); resolve the program
                // against $PATH ourselves and exec the absolute path with an
                // explicit envp.
                const std::filesystem::path resolved = search_path(program.string());
                ::execve(resolved.c_str(), argvp.data(), envp.data());
                ::_exit(127);
            } else if (pid > 0) {
                pid_ = pid;
            }
        }

        ~child() {
            if (pid_ > 0) {
                int status = 0;
                if (::waitpid(pid_, &status, WNOHANG) == 0) { // still running
                    ::kill(pid_, SIGTERM);
                    ::waitpid(pid_, &status, 0);
                }
            }
        }

        child(child &&o) noexcept : pid_(o.pid_) { o.pid_ = -1; }
        child &operator=(child &&o) noexcept {
            if (this != &o) {
                if (pid_ > 0) {
                    ::kill(pid_, SIGTERM);
                    int status = 0;
                    ::waitpid(pid_, &status, 0);
                }
                pid_ = o.pid_;
                o.pid_ = -1;
            }
            return *this;
        }
        child(const child &) = delete;
        child &operator=(const child &) = delete;

        pid_t id() const { return pid_; }

        bool running() {
            if (pid_ <= 0)
                return false;
            int status = 0;
            return ::waitpid(pid_, &status, WNOHANG) == 0;
        }

        void terminate() {
            if (pid_ > 0)
                ::kill(pid_, SIGTERM);
        }

        void wait() {
            if (pid_ > 0) {
                int status = 0;
                ::waitpid(pid_, &status, 0);
                pid_ = -1;
            }
        }

    private:
        pid_t pid_ = -1;
    };
#else
    // Windows: external process nodes are unsupported; construction throws. The
    // type exists so the node code (External.cpp & co.) compiles unmodified.
    class child {
    public:
        child() = default;
        child(const std::filesystem::path &,
              std::vector<std::string>,
              std::vector<std::pair<std::string, std::string>> = {},
              bool = false,
              bool = false) {
            throw std::runtime_error("Gadgetron external process nodes are not supported on Windows");
        }
        child(child &&) noexcept = default;
        child &operator=(child &&) noexcept = default;
        child(const child &) = delete;
        child &operator=(const child &) = delete;
        bool running() const { return false; }
        void terminate() {}
        void wait() {}
    };
#endif

} // namespace Gadgetron::Process
