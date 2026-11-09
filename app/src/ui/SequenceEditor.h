#pragma once

#include "domain/Models.h"

#include <wx/panel.h>

#include <array>
#include <functional>
#include <vector>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxStaticText;

namespace audioguia {

class AppController;

class SequenceEditor final : public wxPanel {
public:
    SequenceEditor(AppController& controller, wxWindow* parent,
                   std::function<void()> on_change = {});
    void refresh_state();

private:
    void commit(std::vector<Language> languages);
    void move_selected(int offset);
    void update_buttons();
    void set_notice(const wxString& text);

    AppController& controller_;
    std::function<void()> on_change_;
    std::array<wxCheckBox*, 3> checks_{};
    wxChoice* order_{nullptr};
    wxButton* up_{nullptr};
    wxButton* down_{nullptr};
    wxStaticText* notice_{nullptr};
    std::vector<Language> languages_;
    bool editable_{false};
};

} // namespace audioguia
