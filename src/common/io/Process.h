#pragma once
#include "common/base/Result.h"
#include <string>
#include <chrono>

namespace ea::process {

struct ExecResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
};

Result<ExecResult> exec(const std::string& command,
                         const std::string& working_dir = "",
                         std::chrono::milliseconds timeout = std::chrono::seconds(30),
                         int max_output_bytes = 65536);

bool command_exists(const std::string& cmd);

}  // namespace ea::process
