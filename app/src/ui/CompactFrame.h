#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <functional>
#include <string>

class wxButton;
class wxCheckBox;
class wxStaticText;
class wxTextCtrl;

namespace audioguia {

class AppController;

class CompactFrame final : public wxFrame {
public:
    CompactFrame(AppController& controller, std::function<void()> on_full);
    void refresh_state();

private:
    void restore_position();
    void save_position();
    void return_to_full();
    void change_on_top(bool enabled);

    AppController& controller_;
    std::function<void()> on_full_;
    wxStaticText* selection_{nullptr};
    wxStaticText* playing_title_{nullptr};
    wxStaticText* playing_detail_{nullptr};
    wxStaticText* status_{nullptr};
    wxStaticText* focus_notice_{nullptr};
    wxTextCtrl* feedback_{nullptr};
    wxButton* pause_{nullptr};
    wxButton* stop_{nullptr};
    wxCheckBox* on_top_{nullptr};
    wxTimer position_timer_;
    bool position_dirty_{false};
    std::string local_error_;
};

} // namespace audioguia
