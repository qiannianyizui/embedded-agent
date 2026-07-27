#include "Process.h"
#include "log/Logger.h"
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <cstring>
#include <array>
#include <chrono>

extern char** environ;

namespace ea::process {

Result<ExecResult> exec(const std::string& command,
                         const std::string& working_dir,
                         std::chrono::milliseconds timeout,
                         int max_output_bytes) {
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        return Error::io("pipe() failed");
    }

    std::string shell = "/bin/sh";
    std::array<char*, 4> argv = {
        const_cast<char*>(shell.c_str()),
        const_cast<char*>("-c"),
        const_cast<char*>(command.c_str()),
        nullptr
    };

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    pid_t pid;
    int spawn_result = posix_spawnp(&pid, shell.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawn_result != 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return Error::io("posix_spawnp failed: " + std::string(strerror(spawn_result)));
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    std::string stdout_buf, stderr_buf;
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool timed_out = false;

    while (true) {
        struct pollfd fds[2] = {
            {stdout_pipe[0], POLLIN, 0},
            {stderr_pipe[0], POLLIN, 0},
        };

        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            timed_out = true;
            break;
        }

        int ret = poll(fds, 2, static_cast<int>(remaining.count()));
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) {
            timed_out = true;
            break;
        }

        char buf[4096];
        if (fds[0].revents & POLLIN) {
            ssize_t n = read(stdout_pipe[0], buf, sizeof(buf));
            if (n > 0) {
                if (static_cast<int>(stdout_buf.size() + n) <= max_output_bytes) {
                    stdout_buf.append(buf, n);
                }
            }
        }
        if (fds[1].revents & POLLIN) {
            ssize_t n = read(stderr_pipe[0], buf, sizeof(buf));
            if (n > 0) {
                if (static_cast<int>(stderr_buf.size() + n) <= max_output_bytes) {
                    stderr_buf.append(buf, n);
                }
            }
        }

        if ((fds[0].revents & (POLLHUP | POLLERR)) && (fds[1].revents & (POLLHUP | POLLERR))) {
            break;
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return Error::timeout("command timed out: " + command);
    }

    int status;
    waitpid(pid, &status, 0);

    ExecResult result;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.stdout_output = std::move(stdout_buf);
    result.stderr_output = std::move(stderr_buf);
    return result;
}

bool command_exists(const std::string& cmd) {
    std::string which = "command -v " + cmd + " 2>/dev/null";
    auto result = exec(which, "", std::chrono::seconds(5), 1024);
    return result.ok() && result.value().exit_code == 0;
}

}  // namespace ea::process
