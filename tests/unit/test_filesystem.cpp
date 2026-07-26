#include <catch2/catch_test_macros.hpp>
#include "common/io/FileSystem.h"
#include <fstream>

using namespace ea;
using namespace ea::fs;

TEST_CASE("FileSystem exists returns true for existing file", "[filesystem]") {
    std::string path = "/tmp/ea_fs_test_exists.txt";
    { std::ofstream f(path); f << "test"; }
    auto result = exists(path);
    REQUIRE(result.ok());
    REQUIRE(result.value() == true);
    remove(path);
}

TEST_CASE("FileSystem exists returns false for nonexistent file", "[filesystem]") {
    auto result = exists("/tmp/ea_nonexistent_file_xyz.txt");
    REQUIRE(result.ok());
    REQUIRE(result.value() == false);
}

TEST_CASE("FileSystem is_dir returns true for directory", "[filesystem]") {
    auto result = is_dir("/tmp");
    REQUIRE(result.ok());
    REQUIRE(result.value() == true);
}

TEST_CASE("FileSystem is_dir returns false for file", "[filesystem]") {
    std::string path = "/tmp/ea_fs_test_isdir.txt";
    { std::ofstream f(path); f << "test"; }
    auto result = is_dir(path);
    REQUIRE(result.ok());
    REQUIRE(result.value() == false);
    remove(path);
}

TEST_CASE("FileSystem write_file and read_file roundtrip", "[filesystem]") {
    std::string path = "/tmp/ea_fs_test_rw.txt";
    auto write_result = write_file(path, "hello filesystem");
    REQUIRE(write_result.ok());

    auto read_result = read_file(path);
    REQUIRE(read_result.ok());
    REQUIRE(read_result.value() == "hello filesystem");

    remove(path);
}

TEST_CASE("FileSystem read_file fails for nonexistent file", "[filesystem]") {
    auto result = read_file("/tmp/ea_nonexistent_read_xyz.txt");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("FileSystem mkdir_p creates nested directories", "[filesystem]") {
    std::string path = "/tmp/ea_fs_test_mkdir/a/b/c";
    auto result = mkdir_p(path);
    REQUIRE(result.ok());
    REQUIRE(is_dir(path).value() == true);

    // Cleanup
    remove("/tmp/ea_fs_test_mkdir/a/b/c");
    remove("/tmp/ea_fs_test_mkdir/a/b");
    remove("/tmp/ea_fs_test_mkdir/a");
    remove("/tmp/ea_fs_test_mkdir");
}

TEST_CASE("FileSystem remove deletes file", "[filesystem]") {
    std::string path = "/tmp/ea_fs_test_remove.txt";
    { std::ofstream f(path); f << "to be removed"; }
    REQUIRE(exists(path).value() == true);

    auto result = remove(path);
    REQUIRE(result.ok());
    REQUIRE(exists(path).value() == false);
}

TEST_CASE("FileSystem list_dir returns directory contents", "[filesystem]") {
    std::string dir = "/tmp/ea_fs_test_listdir";
    mkdir_p(dir);
    write_file(dir + "/file1.txt", "a");
    write_file(dir + "/file2.txt", "b");

    auto result = list_dir(dir);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() >= 2);

    // Cleanup
    remove(dir + "/file1.txt");
    remove(dir + "/file2.txt");
    remove(dir);
}

TEST_CASE("FileSystem home_dir returns non-empty path", "[filesystem]") {
    auto result = home_dir();
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.value().empty());
}

TEST_CASE("FileSystem home_dir returns a writable-looking absolute path", "[filesystem]") {
    // On all supported platforms (Linux, WSL, Termux, native Android fallback),
    // home_dir() should return an absolute path starting with '/'.
    auto result = home_dir();
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.value().empty());
    REQUIRE(result.value().front() == '/');
}

TEST_CASE("resolve_data_path returns config_dir/filename when config_dir ok", "[common][filesystem]") {
    auto result = ea::fs::resolve_data_path("test.db");
    REQUIRE(result.ok());
    // Should end with /test.db
    REQUIRE(result.value().size() >= 8);
    REQUIRE(result.value().substr(result.value().size() - 8) == "/test.db");
}

TEST_CASE("resolve_data_path with empty filename", "[common][filesystem]") {
    auto result = ea::fs::resolve_data_path("");
    // Empty filename should still produce a valid path ending in /
    REQUIRE(result.ok());
}

TEST_CASE("expand_tilde expands ~ to home dir", "[filesystem]") {
    auto home = home_dir();
    REQUIRE(home.ok());
    REQUIRE(expand_tilde("~") == home.value());
}

TEST_CASE("expand_tilde expands ~/path", "[filesystem]") {
    auto home = home_dir();
    REQUIRE(home.ok());
    REQUIRE(expand_tilde("~/some/path") == home.value() + "/some/path");
}

TEST_CASE("expand_tilde returns non-tilde path unchanged", "[filesystem]") {
    REQUIRE(expand_tilde("/absolute/path") == "/absolute/path");
    REQUIRE(expand_tilde("relative/path") == "relative/path");
    REQUIRE(expand_tilde("") == "");
}

TEST_CASE("expand_tilde does not expand ~otheruser", "[filesystem]") {
    REQUIRE(expand_tilde("~otheruser/docs").substr(0, 1) == "~");
}

TEST_CASE("config_dir honors EA_CONFIG_DIR env var", "[filesystem]") {
    const char* orig = getenv("EA_CONFIG_DIR");
    std::string orig_str = orig ? orig : "";
    setenv("EA_CONFIG_DIR", "/tmp/ea_test_config", 1);

    auto result = config_dir();
    REQUIRE(result.ok());
    REQUIRE(result.value() == "/tmp/ea_test_config");

    if (orig_str.empty()) unsetenv("EA_CONFIG_DIR");
    else setenv("EA_CONFIG_DIR", orig_str.c_str(), 1);
}

TEST_CASE("config_dir expands ~ in EA_CONFIG_DIR", "[filesystem]") {
    const char* orig = getenv("EA_CONFIG_DIR");
    std::string orig_str = orig ? orig : "";
    auto home = home_dir();
    REQUIRE(home.ok());

    setenv("EA_CONFIG_DIR", "~/custom-config", 1);
    auto result = config_dir();
    REQUIRE(result.ok());
    REQUIRE(result.value() == home.value() + "/custom-config");

    if (orig_str.empty()) unsetenv("EA_CONFIG_DIR");
    else setenv("EA_CONFIG_DIR", orig_str.c_str(), 1);
}

TEST_CASE("config_dir ignores empty EA_CONFIG_DIR", "[filesystem]") {
    const char* orig = getenv("EA_CONFIG_DIR");
    std::string orig_str = orig ? orig : "";

    setenv("EA_CONFIG_DIR", "", 1);
    auto result = config_dir();
    REQUIRE(result.ok());
    REQUIRE(result.value().find(".embedded-agent") != std::string::npos);

    if (orig_str.empty()) unsetenv("EA_CONFIG_DIR");
    else setenv("EA_CONFIG_DIR", orig_str.c_str(), 1);
}
