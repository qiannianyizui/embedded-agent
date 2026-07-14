#pragma once
#include "common/base/Result.h"
#include <string>
#include <vector>

namespace ea::fs {

Result<std::string> home_dir();
Result<std::string> config_dir();
Result<std::string> data_dir();
Result<bool> exists(const std::string& path);
Result<bool> is_dir(const std::string& path);
Result<void> mkdir_p(const std::string& path);
Result<void> remove(const std::string& path);
Result<std::string> read_file(const std::string& path);
Result<void> write_file(const std::string& path, const std::string& content);
Result<std::vector<std::string>> list_dir(const std::string& path);

}  // namespace ea::fs
