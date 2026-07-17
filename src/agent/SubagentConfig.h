// SubagentConfig — template configuration for sub-agent delegation
#pragma once
#include <string>
#include <vector>

namespace ea::agent {

struct SubagentConfig {
    std::string name;
    std::string description;
    std::string model;
    std::string system_prompt;
    std::vector<std::string> toolsets;
    bool shared_memory = true;
    int max_iterations = 20;
    bool dangerous = false;
};

}  // namespace ea::agent
