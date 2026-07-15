#pragma once
#include <string>

namespace ea::platform {

enum class PlatformKind { Linux, Android, Wsl, Unknown };

PlatformKind detect();
bool is_android();
bool is_linux();
bool is_wsl();
std::string home_dir();
std::string shell_path();
std::string tmp_dir();
bool has_shell_access();
bool has_filesystem_access();
bool supports_long_running();
std::string platform_description();

}  // namespace ea::platform
