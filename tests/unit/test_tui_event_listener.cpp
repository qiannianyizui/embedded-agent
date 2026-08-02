// Unit tests for TuiEventListener — bridges AgentEvent to UI updates
#include <catch2/catch_test_macros.hpp>
#include "tui/TuiEventListener.h"
#include "tui/ChatArea.h"
#include "tui/StatusBar.h"
#include "tui/TopBar.h"
#include <vector>
#include <mutex>

using namespace ea::tui;

TEST_CASE("TuiEventListener dispatches TurnStart", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    std::vector<std::function<void()>> posted;
    std::mutex mtx;

    TuiEventListener listener(chat_area, status_bar, top_bar,
        [&](std::function<void()> fn) {
            std::lock_guard<std::mutex> lock(mtx);
            posted.push_back(std::move(fn));
        });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::TurnStart;
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
    // Execute the posted task
    posted[0]();
    // After execution, status bar should reflect busy state
    // (no direct accessor for busy state, but no crash confirms correctness)
}

TEST_CASE("TuiEventListener dispatches TurnEnd", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar, top_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::TurnEnd;
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
    posted[0]();
}

TEST_CASE("TuiEventListener dispatches LLMResponse with usage", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar, top_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::LLMResponse;
    event.usage = ea::Usage{100, 50, 0, 0};
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
    posted[0]();
}

TEST_CASE("TuiEventListener dispatches ToolCallStart", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar, top_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::ToolCallStart;
    event.tool_name = "shell";
    event.tool_arguments = nlohmann::json{{"command", "ls"}};
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
    posted[0]();
    // Verify ChatArea received the tool start
    REQUIRE(chat_area.messages().size() == 1);
    REQUIRE(chat_area.messages()[0].role == ea::Role::Tool);
    REQUIRE(chat_area.messages()[0].tool_name == "shell");
}

TEST_CASE("TuiEventListener dispatches ToolCallEnd", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar, top_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::ToolCallEnd;
    event.tool_name = "shell";
    event.tool_result = "file1\nfile2";
    event.tool_error = false;
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
    posted[0]();
    REQUIRE(chat_area.messages().size() == 1);
    REQUIRE(chat_area.messages()[0].content == "file1\nfile2");
    REQUIRE_FALSE(chat_area.messages()[0].is_error);
}

TEST_CASE("TuiEventListener dispatches ToolCallEnd with error", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar, top_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::ToolCallEnd;
    event.tool_name = "shell";
    event.tool_result = "command failed";
    event.tool_error = true;
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
    posted[0]();
    REQUIRE(chat_area.messages().size() == 1);
    REQUIRE(chat_area.messages()[0].is_error);
}

TEST_CASE("TuiEventListener dispatches Error", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar, top_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::Error;
    event.error_message = "Connection failed";
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
    posted[0]();
    REQUIRE(chat_area.messages().size() == 1);
    REQUIRE(chat_area.messages()[0].is_error);
    REQUIRE(chat_area.messages()[0].content == "Connection failed");
}

TEST_CASE("TuiEventListener dispatches Interrupt", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar, top_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::Interrupt;
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
    posted[0]();
    REQUIRE(chat_area.messages().size() == 1);
    REQUIRE(chat_area.messages()[0].is_error);
    REQUIRE(chat_area.messages()[0].content == "Interrupted");
}

TEST_CASE("TuiEventListener multiple events accumulate posted tasks", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar, top_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::TurnStart;
    listener.on_event(event);

    event.type = ea::agent::AgentEventType::ToolCallStart;
    event.tool_name = "read";
    event.tool_arguments = nlohmann::json{{"path", "/tmp/test"}};
    listener.on_event(event);

    event.type = ea::agent::AgentEventType::ToolCallEnd;
    event.tool_name = "read";
    event.tool_result = "contents";
    event.tool_error = false;
    listener.on_event(event);

    event.type = ea::agent::AgentEventType::TurnEnd;
    listener.on_event(event);

    REQUIRE(posted.size() == 4);

    // Execute all posted tasks
    for (auto& fn : posted) {
        fn();
    }

    // ChatArea should have tool start + tool end messages
    REQUIRE(chat_area.messages().size() == 2);
}

TEST_CASE("TuiEventListener is an IEventListener", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    TopBar top_bar(status_bar.spinner_state());
    TuiEventListener listener(chat_area, status_bar, top_bar,
        [](std::function<void()>) {});

    ea::agent::IEventListener* iface = &listener;
    REQUIRE(iface != nullptr);
}
