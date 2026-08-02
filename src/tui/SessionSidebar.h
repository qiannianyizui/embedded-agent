// SessionSidebar — collapsible panel listing conversations
#pragma once
#include <ftxui/component/component.hpp>
#include "conversation/IConversationStore.h"
#include <functional>
#include <string>
#include <vector>

namespace ea::tui {

class SessionSidebar {
public:
    explicit SessionSidebar(ea::conversation::IConversationStore* store = nullptr);

    ftxui::Component component();
    bool is_showing() const;
    void toggle();
    void refresh();
    void set_active(const std::string& id);
    void set_on_resume(std::function<void(std::string)> fn);

private:
    ea::conversation::IConversationStore* store_;
    ftxui::Component component_;
    std::vector<ea::conversation::ConversationMeta> sessions_;
    std::string active_id_;
    int selected_ = 0;
    bool showing_ = false;
    std::function<void(std::string)> on_resume_;

    ftxui::Element render();
    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
