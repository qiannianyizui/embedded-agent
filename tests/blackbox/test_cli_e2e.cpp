// tests/blackbox/test_cli_e2e.cpp
// CLI end-to-end (black-box) tests — interact with the compiled embedded-agent
// binary via subprocess stdin/stdout. No knowledge of internal APIs.
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>

namespace {

std::string binary_path() {
    return std::string(CMAKE_BINARY_DIR) + "/embedded-agent";
}

// Run the binary with given args, return {stdout, stderr, exit_code}.
// Optionally writes stdin_data to the child's stdin.
struct SubprocessResult {
    std::string stdout_output;
    std::string stderr_output;
    int exit_code = -1;
    bool timed_out = false;
};

SubprocessResult run_binary(const std::vector<std::string>& args,
                             const std::string& stdin_data = "",
                             int timeout_ms = 5000) {
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        return {};
    }

    pid_t pid = fork();
    if (pid < 0) return {};

    if (pid == 0) {
        // Child
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        std::vector<char*> argv;
        std::string bin = binary_path();
        argv.push_back(bin.data());
        std::vector<std::string> arg_storage = args;
        for (auto& a : arg_storage) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);
        execv(bin.c_str(), argv.data());
        // execv only returns on failure
        _exit(127);
    }

    // Parent
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Write stdin data
    if (!stdin_data.empty()) {
        write(stdin_pipe[1], stdin_data.data(), stdin_data.size());
    }
    close(stdin_pipe[1]);

    // Set pipes non-blocking
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    SubprocessResult result;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        std::array<pollfd, 2> fds = {
            pollfd{stdout_pipe[0], POLLIN, 0},
            pollfd{stderr_pipe[0], POLLIN, 0}
        };
        int remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            result.timed_out = true;
            kill(pid, SIGKILL);
            break;
        }
        int pr = poll(fds.data(), 2, std::min(remaining, 100));
        if (pr > 0) {
            std::array<char, 4096> buf;
            if (fds[0].revents & POLLIN) {
                ssize_t n = read(stdout_pipe[0], buf.data(), buf.size());
                if (n > 0) result.stdout_output.append(buf.data(), n);
            }
            if (fds[1].revents & POLLIN) {
                ssize_t n = read(stderr_pipe[0], buf.data(), buf.size());
                if (n > 0) result.stderr_output.append(buf.data(), n);
            }
        }

        // Check if child exited
        int status;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            // Drain remaining output
            std::array<char, 4096> buf;
            while (true) {
                ssize_t n = read(stdout_pipe[0], buf.data(), buf.size());
                if (n <= 0) break;
                result.stdout_output.append(buf.data(), n);
            }
            while (true) {
                ssize_t n = read(stderr_pipe[0], buf.data(), buf.size());
                if (n <= 0) break;
                result.stderr_output.append(buf.data(), n);
            }
            if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) result.exit_code = -WTERMSIG(status);
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    return result;
}

// Write a minimal config to a temp file that points to a non-existent Ollama.
// The binary will start but any provider call will fail. /quit works before calls.
std::string write_minimal_config() {
    auto tmpdir = std::filesystem::temp_directory_path();
    auto path = tmpdir / "ea-blackbox-test-config.toml";
    std::ofstream f(path);
    f << R"([provider]
type = "ollama"
base_url = "http://127.0.0.1:1"

[memory]
backend = "sqlite"
path = ":memory:"

[security]
autonomy = "supervised"

[agent]
max_iterations = 3
stream = false
)";
    f.close();
    return path.string();
}

}  // anonymous namespace

TEST_CASE("CLI E2E: binary exists and is executable", "[e2e][blackbox][cli]") {
    REQUIRE(std::filesystem::exists(binary_path()));
}

TEST_CASE("CLI E2E: nonexistent config uses defaults", "[e2e][blackbox][cli]") {
    // config::load returns defaults for nonexistent files (doesn't error)
    auto result = run_binary({"-c", "/nonexistent/path/config.toml"}, "/quit\n", 10000);
    REQUIRE_FALSE(result.timed_out);
    // Should start with defaults and exit cleanly after /quit
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("CLI E2E: /quit command exits cleanly", "[e2e][blackbox][cli]") {
    std::string config = write_minimal_config();
    // Send /quit immediately — should exit before any provider call
    auto result = run_binary({"-c", config}, "/quit\n", 10000);
    REQUIRE_FALSE(result.timed_out);
    // Should exit with code 0 (clean exit)
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("CLI E2E: startup prints version banner", "[e2e][blackbox][cli]") {
    std::string config = write_minimal_config();
    auto result = run_binary({"-c", config}, "/quit\n", 10000);
    REQUIRE_FALSE(result.timed_out);
    REQUIRE(result.stdout_output.find("embedded-agent v0.1.0") != std::string::npos);
}

TEST_CASE("CLI E2E: EOF on stdin exits cleanly", "[e2e][blackbox][cli]") {
    std::string config = write_minimal_config();
    // Send empty stdin (EOF immediately)
    auto result = run_binary({"-c", config}, "", 10000);
    REQUIRE_FALSE(result.timed_out);
    REQUIRE(result.exit_code == 0);
}
