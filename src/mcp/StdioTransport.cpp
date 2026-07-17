// src/mcp/StdioTransport.cpp
#include "StdioTransport.h"
#include "common/io/Logger.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

namespace ea::mcp {

struct StdioTransport::Impl {
    FILE* child_stdin = nullptr;
    FILE* child_stdout = nullptr;
    pid_t child_pid = -1;
    bool running = false;
};

StdioTransport::StdioTransport(Config config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}

StdioTransport::~StdioTransport() {
    if (impl_->running) {
        stop();
    }
}

Result<void> StdioTransport::start() {
    if (impl_->running) return {};

    // Build command string
    std::string cmd = config_.command;
    for (const auto& arg : config_.args) {
        cmd += " ";
        if (arg.find(' ') != std::string::npos || arg.find('\'') != std::string::npos) {
            cmd += "'" + arg + "'";
        } else {
            cmd += arg;
        }
    }

    // Set environment variables
    for (const auto& [key, value] : config_.env) {
        setenv(key.c_str(), value.c_str(), 1);
    }

    // Create pipes
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        return Error::io("Failed to create pipes for MCP server");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return Error::io("Failed to fork for MCP server");
    }

    if (pid == 0) {
        // Child process
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    // Parent process
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    impl_->child_pid = pid;
    impl_->child_stdin = fdopen(stdin_pipe[1], "w");
    impl_->child_stdout = fdopen(stdout_pipe[0], "r");
    impl_->running = true;

    if (!impl_->child_stdin || !impl_->child_stdout) {
        stop();
        return Error::io("Failed to open pipes as FILE*");
    }

    setvbuf(impl_->child_stdin, nullptr, _IOLBF, 0);

    EA_INFO("MCP server started: pid={}, command={}", pid, cmd);
    return {};
}

Result<void> StdioTransport::stop() {
    if (!impl_->running) return {};

    if (impl_->child_stdin) {
        fclose(impl_->child_stdin);
        impl_->child_stdin = nullptr;
    }

    if (impl_->child_pid > 0) {
        int status;
        kill(impl_->child_pid, SIGTERM);

        for (int i = 0; i < 20; ++i) {
            if (waitpid(impl_->child_pid, &status, WNOHANG) != 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        kill(impl_->child_pid, SIGKILL);
        waitpid(impl_->child_pid, &status, 0);
    }

    if (impl_->child_stdout) {
        fclose(impl_->child_stdout);
        impl_->child_stdout = nullptr;
    }

    impl_->child_pid = -1;
    impl_->running = false;
    return {};
}

Result<nlohmann::json> StdioTransport::send(const nlohmann::json& request) {
    if (!impl_->running || !impl_->child_stdin || !impl_->child_stdout) {
        return Error::io("MCP transport not running");
    }

    std::string line = request.dump() + "\n";
    fputs(line.c_str(), impl_->child_stdin);
    fflush(impl_->child_stdin);

    char buffer[65536];
    if (!fgets(buffer, sizeof(buffer), impl_->child_stdout)) {
        return Error::io("MCP server closed connection");
    }

    try {
        return nlohmann::json::parse(buffer);
    } catch (const nlohmann::json::parse_error& e) {
        return Error::parse(std::string("MCP response parse error: ") + e.what());
    }
}

bool StdioTransport::is_running() const {
    return impl_->running;
}

}  // namespace ea::mcp
