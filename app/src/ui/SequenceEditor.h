#pragma once

#include "domain/Models.h"

#include <wx/panel.h>

#include <functional>
#include <optional>
#include <vector>

class wxButton;
class wxListCtrl;
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
    void move_to(int source, int target);
    void check_language(long row, bool checked);
    void update_buttons();
    void set_notice(const wxString& text);

    AppController& controller_;
    std::function<void()> on_change_;
    wxListCtrl* order_{nullptr};
    wxButton* up_{nullptr};
    wxButton* down_{nullptr};
    wxStaticText* notice_{nullptr};
    std::vector<Language> languages_;
    std::vector<Language> rows_;
    std::optional<Language> selected_;
    bool editable_{false};
    bool syncing_{false};
};

} // namespace audioguia
