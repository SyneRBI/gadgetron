#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// Self-contained POSIX process helper. Replaces boost::process, whose v1 API
// (search_path / args / std_out> / ipstream / limit_handles) was dropped in
// Boost 1.88+ (conda-forge now ships only the v2 API).
namespace Gadgetron::Process {

    namespace detail {
        std::lock_guard<std::mutex> obtain_process_lock();
    }

    // Resolve `name` against $PATH (replaces boost::process::search_path).
    inline std::filesystem::path search_path(const std::string &name) {
        if (name.empty())
            return {};
        if (name.find('/') != std::string::npos)
            return name;
        const char *path_env = std::getenv("PATH");
        const std::string dirs = path_env ? path_env : "";
        std::string cur;
        for (size_t i = 0; i <= dirs.size(); ++i) {
            if (i == dirs.size() || dirs[i] == ':') {
                if (!cur.empty()) {
                    std::error_code ec;
                    const std::filesystem::path candidate = std::filesystem::path(cur) / name;
                    if (std::filesystem::exists(candidate, ec) && ::access(candidate.c_str(), X_OK) == 0)
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
                ::execvpe(program.string().c_str(), argvp.data(), envp.data());
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

} // namespace Gadgetron::Process
