// Unit tests for ChatArea TUI component
#include <catch2/catch_test_macros.hpp>
#include "tui/ChatArea.h"

using namespace ea::tui;

TEST_CASE("ChatArea appends user message", "[tui]") {
    ChatArea area;
    area.append_user("Hello");
    REQUIRE(area.has_new_messages());
    area.clear_new_flag();
    REQUIRE_FALSE(area.has_new_messages());
}

TEST_CASE("ChatArea appends assistant message", "[tui]") {
    ChatArea area;
    area.append_assistant("Hi there");
    REQUIRE(area.has_new_messages());
}

TEST_CASE("ChatArea accumulates stream chunks", "[tui]") {
    ChatArea area;
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Content;
    chunk.data = "Hello";
    area.append_stream_chunk(chunk);
    REQUIRE(area.has_new_messages());

    chunk.data = " world";
    area.append_stream_chunk(chunk);
    REQUIRE(area.streaming_content() == "Hello world");

    // After finish, streaming content becomes a full message
    area.finish_message();
    REQUIRE(area.has_new_messages());
    REQUIRE(area.streaming_content().empty());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].role == ea::Role::Assistant);
    REQUIRE(area.messages()[0].content == "Hello world");
}

TEST_CASE("ChatArea stream error chunk", "[tui]") {
    ChatArea area;
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Error;
    chunk.data = "connection lost";
    area.append_stream_chunk(chunk);
    REQUIRE(area.has_new_messages());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].is_error);
}

TEST_CASE("ChatArea clear removes all messages", "[tui]") {
    ChatArea area;
    area.append_user("test");
    area.clear();
    REQUIRE_FALSE(area.has_new_messages());
    REQUIRE(area.messages().empty());
    REQUIRE(area.streaming_content().empty());
}

TEST_CASE("ChatArea tool messages", "[tui]") {
    ChatArea area;
    area.append_tool_start("shell", R"({"command":"ls"})");
    REQUIRE(area.has_new_messages());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].role == ea::Role::Tool);
    REQUIRE(area.messages()[0].tool_name == "shell");

    area.clear_new_flag();
    area.append_tool_end("shell", "file1\nfile2", false);
    REQUIRE(area.has_new_messages());
    REQUIRE(area.messages().size() == 2);
    REQUIRE_FALSE(area.messages()[1].is_error);

    // Tool end with error
    area.append_tool_end("shell", "command failed", true);
    REQUIRE(area.messages().back().is_error);
}

TEST_CASE("ChatArea error messages", "[tui]") {
    ChatArea area;
    area.append_error("Something went wrong");
    REQUIRE(area.has_new_messages());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].is_error);
    REQUIRE(area.messages()[0].content == "Something went wrong");
}

TEST_CASE("ChatArea component returns valid FTXUI component", "[tui]") {
    ChatArea area;
    auto comp = area.component();
    REQUIRE(comp);
    // Calling component() again returns the same instance
    auto comp2 = area.component();
    REQUIRE(comp == comp2);
}

TEST_CASE("ChatArea streaming then finish preserves content", "[tui]") {
    ChatArea area;
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Content;

    chunk.data = "Line 1\n";
    area.append_stream_chunk(chunk);
    chunk.data = "Line 2\n";
    area.append_stream_chunk(chunk);
    chunk.data = "Line 3";
    area.append_stream_chunk(chunk);

    REQUIRE(area.streaming_content() == "Line 1\nLine 2\nLine 3");

    area.finish_message();
    REQUIRE(area.streaming_content().empty());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].content == "Line 1\nLine 2\nLine 3");
    REQUIRE(area.messages()[0].role == ea::Role::Assistant);
}

TEST_CASE("ChatArea finish_message with empty buffer is no-op", "[tui]") {
    ChatArea area;
    area.finish_message();
    REQUIRE_FALSE(area.has_new_messages());
    REQUIRE(area.messages().empty());
}

TEST_CASE("ChatArea multiple message types coexist", "[tui]") {
    ChatArea area;
    area.append_user("What is 2+2?");
    area.append_assistant("Let me calculate that.");
    area.append_tool_start("calc", R"({"expr":"2+2"})");
    area.append_tool_end("calc", "4", false);
    area.append_assistant("The answer is 4.");

    REQUIRE(area.messages().size() == 5);
    REQUIRE(area.messages()[0].role == ea::Role::User);
    REQUIRE(area.messages()[1].role == ea::Role::Assistant);
    REQUIRE(area.messages()[2].role == ea::Role::Tool);
    REQUIRE(area.messages()[3].role == ea::Role::Tool);
    REQUIRE(area.messages()[4].role == ea::Role::Assistant);
}

TEST_CASE("ChatArea clear_new_flag idempotent", "[tui]") {
    ChatArea area;
    area.clear_new_flag();
    REQUIRE_FALSE(area.has_new_messages());
    area.clear_new_flag();  // Should not crash
    REQUIRE_FALSE(area.has_new_messages());
}
