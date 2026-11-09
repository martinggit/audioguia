#pragma once

#include "domain/Models.h"

#include <wx/dialog.h>
#include <wx/timer.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class wxButton;
class wxChoice;
class wxListCtrl;
class wxSpinCtrlDouble;
class wxStaticText;
class wxTextCtrl;

namespace audioguia {

class AppController;
struct AudioValidation;

class PreferencesDialog final : public wxDialog {
public:
    PreferencesDialog(AppController& controller, wxWindow* parent);
    ~PreferencesDialog() override;

private:
    void refresh_devices();
    void select_point(std::size_t index);
    void load_association();
    void browse_audio();
    void clear_association();
    void start_validation();
    void refresh_validation();
    void apply();
    void set_message(const wxString& message);

    AppController& controller_;
    AppConfig draft_;
    std::filesystem::path root_;
    wxChoice* output_{nullptr};
    wxTextCtrl* device_message_{nullptr};
    wxSpinCtrlDouble* gap_{nullptr};
    wxListCtrl* table_{nullptr};
    wxChoice* point_{nullptr};
    wxChoice* language_{nullptr};
    wxTextCtrl* association_path_{nullptr};
    wxTextCtrl* association_status_{nullptr};
    wxStaticText* summary_{nullptr};
    wxTextCtrl* message_{nullptr};
    wxButton* apply_button_{nullptr};
    std::vector<std::string> device_ids_;
    std::vector<std::array<bool, 3>> edited_paths_;
    std::vector<std::array<bool, 3>> checked_paths_;
    std::vector<std::array<std::string, 3>> check_messages_;
    std::shared_ptr<AudioValidation> validation_;
    wxTimer validation_timer_;
    bool validation_complete_{false};
    bool syncing_selection_{false};
};

} // namespace audioguia
