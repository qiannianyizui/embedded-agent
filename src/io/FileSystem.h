#pragma once
#include "base/Result.h"
#include <string>
#include <vector>

namespace ea::fs {

Result<std::string> home_dir();
Result<std::string> config_dir();
Result<std::string> data_dir();
Result<std::string> trace_dir();
std::string expand_tilde(const std::string& path);
// Resolve a data file path: prefers config_dir()/filename,
// falls back to home_dir()/.embedded-agent/filename
Result<std::string> resolve_data_path(const std::string& filename);
Result<bool> exists(const std::string& path);
Result<bool> is_dir(const std::string& path);
Result<void> mkdir_p(const std::string& path);
Result<void> remove(const std::string& path);
Result<std::string> read_file(const std::string& path);
Result<void> write_file(const std::string& path, const std::string& content);
Result<std::vector<std::string>> list_dir(const std::string& path);

// Find the directory containing .git by walking from start_dir upward.
// Returns the directory path on success, or Error::not_found if no .git found.
Result<std::string> find_git_root(const std::string& start_dir);

// Get the parent directory of a path.
// Returns empty string for root or empty input.
std::string parent_path(const std::string& path);

// Walk from start_dir upward to stop_at (inclusive), looking for any of
// the given filenames. Returns the first found path, or empty string if none.
// If stop_at is empty, only checks start_dir itself (safety for non-git dirs).
Result<std::string> walk_up_find(const std::string& start_dir,
                                 const std::vector<std::string>& filenames,
                                 const std::string& stop_at = "");

}  // namespace ea::fs
