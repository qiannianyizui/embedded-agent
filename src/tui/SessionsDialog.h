// SessionsDialog — modal session picker: list conversations, Enter resumes one
#pragma once
#include <ftxui/component/component.hpp>
#include "conversation/IConversationStore.h"
#include <functional>
#include <string>
#include <vector>

namespace ea::tui {

class SessionsDialog {
public:
    SessionsDialog();

    ftxui::Component component();
    void set_store(ea::conversation::IConversationStore* store);
    void set_active(const std::string& id);
    void set_on_resume(std::function<void(const std::string&)> fn);
    bool is_showing() const;
    void show();
    void hide();

private:
    ea::conversation::IConversationStore* store_ = nullptr;
    ftxui::Component component_;
    std::vector<ea::conversation::ConversationMeta> sessions_;
    std::string active_id_;
    std::function<void(const std::string&)> on_resume_;
    int selected_ = 0;
    bool showing_ = false;

    void refresh();
    ftxui::Element render();
    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui