#pragma once

#include "domain/Models.h"

#include <wx/frame.h>

#include <string>
#include <vector>

class wxButton;
class wxChoice;
class wxPanel;
class wxSlider;
class wxStaticText;
class wxTextCtrl;
class wxToggleButton;
class wxScrolledWindow;

namespace audioguia {

class AppController;
class PlaybackButton;

class MainFrame final : public wxFrame {
public:
    explicit MainFrame(AppController& controller);
    void refresh_state();
    void focus_transport();
    // The application's input controller must also respect native modal disabling.
    bool input_suspended() const noexcept { return settings_open_; }

private:
    struct PointControls {
        std::string id;
        Direction direction{Direction::up};
        int number{1};
        PlaybackButton* name_button{nullptr};
        int badge_key{-1};
        PlaybackStatus badge_status{PlaybackStatus::stopped};
    };

    void change_preferences(Preferences preferences);
    void open_preferences();
    void edit_sequence();
    void play_point(Direction direction, int number);

    AppController& controller_;
    wxToggleButton* up_{nullptr};
    wxToggleButton* down_{nullptr};
    wxPanel* up_points_{nullptr};
    wxPanel* down_points_{nullptr};
    wxScrolledWindow* scroll_{nullptr};
    wxChoice* mode_{nullptr};
    wxChoice* language_{nullptr};
    wxButton* sequence_button_{nullptr};
    wxStaticText* prepare_{nullptr};
    wxStaticText* playing_detail_{nullptr};
    wxStaticText* volume_label_{nullptr};
    wxStaticText* keyboard_status_{nullptr};
    wxTextCtrl* feedback_{nullptr};
    wxButton* pause_{nullptr};
    wxButton* stop_{nullptr};
    wxSlider* volume_{nullptr};
    std::vector<PointControls> points_;
    std::string local_error_;
    bool settings_open_{false};
};

} // namespace audioguia
