#include "ui/SequenceEditor.h"

#include "app/AppController.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <exception>
#include <optional>
#include <utility>

namespace audioguia {
namespace {

constexpr std::array<Language, 3> all_languages{Language::es, Language::en, Language::pt};

wxString utf8(const std::string& text) {
    return wxString::FromUTF8(text.c_str());
}

} // namespace

SequenceEditor::SequenceEditor(AppController& controller, wxWindow* parent,
                               std::function<void()> on_change)
    : wxPanel(parent), controller_(controller), on_change_(std::move(on_change)) {
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, "Secuencia:"), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    for (std::size_t i = 0; i < all_languages.size(); ++i) {
        const auto language = all_languages[i];
        checks_[i] = new wxCheckBox(this, wxID_ANY, utf8(label(language)));
        checks_[i]->SetMinSize(FromDIP(wxSize(-1, 28)));
        row->Add(checks_[i], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        checks_[i]->Bind(wxEVT_CHECKBOX, [this, i, language](wxCommandEvent&) {
            auto languages = languages_;
            const auto found = std::find(languages.begin(), languages.end(), language);
            if (checks_[i]->GetValue()) {
                if (found == languages.end()) languages.push_back(language);
            } else if (found != languages.end()) {
                if (languages.size() == 1) {
                    checks_[i]->SetValue(true);
                    set_notice(wxString::FromUTF8(
                        u8"Debe quedar al menos un idioma. Active otro antes de quitar el \u00faltimo."));
                    return;
                }
                languages.erase(found);
            }
            commit(std::move(languages));
        });
    }

    layout->Add(row, 0, wxEXPAND);
    auto* order_row = new wxBoxSizer(wxHORIZONTAL);
    order_ = new wxChoice(this, wxID_ANY);
    order_->SetMinSize(FromDIP(wxSize(130, 32)));
    order_->SetName("Idioma que desea mover en la secuencia");
    order_->SetToolTip("Seleccione un idioma y use Subir o Bajar para cambiar el orden.");
    order_row->Add(order_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    up_ = new wxButton(this, wxID_ANY, "Subir");
    down_ = new wxButton(this, wxID_ANY, "Bajar");
    for (auto* button : {up_, down_}) {
        button->SetMinSize(FromDIP(wxSize(66, 32)));
        order_row->Add(button, 0, wxRIGHT, FromDIP(6));
    }
    order_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { update_buttons(); });
    up_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { move_selected(-1); });
    down_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { move_selected(1); });
    layout->Add(order_row, 0, wxEXPAND);
    notice_ = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    notice_->SetMinSize(FromDIP(wxSize(0, 18)));
    notice_->SetForegroundColour(wxColour(64, 82, 96));
    layout->Add(notice_, 0, wxEXPAND | wxTOP, FromDIP(2));
    SetSizer(layout);
    SetMinSize(FromDIP(wxSize(360, 82)));
    refresh_state();
}

void SequenceEditor::set_notice(const wxString& text) {
    if (notice_->GetLabel() != text) {
        notice_->SetLabel(text);
        notice_->SetToolTip(text);
    }
}

void SequenceEditor::refresh_state() {
    const auto& preferences = controller_.config().preferences;
    const bool changed = languages_ != preferences.custom_languages;
    if (changed) {
        std::optional<Language> selected;
        const auto index = order_->GetSelection();
        if (index != wxNOT_FOUND && static_cast<std::size_t>(index) < languages_.size()) {
            selected = languages_[static_cast<std::size_t>(index)];
        }
        languages_ = preferences.custom_languages;
        order_->Freeze();
        order_->Clear();
        int selection = 0;
        for (std::size_t i = 0; i < languages_.size(); ++i) {
            order_->Append(utf8(std::to_string(i + 1) + ". " + label(languages_[i])));
            if (selected == languages_[i]) selection = static_cast<int>(i);
        }
        if (!languages_.empty()) order_->SetSelection(selection);
        order_->Thaw();
    }
    for (std::size_t i = 0; i < all_languages.size(); ++i) {
        const bool checked = std::find(languages_.begin(), languages_.end(), all_languages[i]) != languages_.end();
        if (checks_[i]->GetValue() != checked) checks_[i]->SetValue(checked);
    }
    const bool editable = preferences.mode == Mode::custom;
    if (editable_ != editable || changed) {
        editable_ = editable;
        for (auto* check : checks_) check->Enable(editable_);
        order_->Enable(editable_);
        update_buttons();
        set_notice(editable_
            ? "El orden se guarda al cambiarlo y se usa en el siguiente punto."
            : "Active Personalizado para editar. Secuencial siempre usa ESP > ENG > PORT.");
    }
}

void SequenceEditor::update_buttons() {
    const int selection = order_->GetSelection();
    const bool can_up = editable_ && selection > 0;
    const bool can_down = editable_ && selection != wxNOT_FOUND &&
        static_cast<std::size_t>(selection + 1) < languages_.size();
    if (up_->IsEnabled() != can_up) up_->Enable(can_up);
    if (down_->IsEnabled() != can_down) down_->Enable(can_down);
}

void SequenceEditor::commit(std::vector<Language> languages) {
    try {
        auto preferences = controller_.config().preferences;
        preferences.custom_languages = std::move(languages);
        controller_.set_preferences(std::move(preferences));
        refresh_state();
        set_notice("Orden guardado para el siguiente punto. La pista activa no cambia.");
        if (on_change_) on_change_();
    } catch (const std::exception& error) {
        refresh_state();
        set_notice(utf8(std::string("No se pudo cambiar la secuencia: ") + error.what()));
    }
}

void SequenceEditor::move_selected(int offset) {
    const int selection = order_->GetSelection();
    const int target = selection + offset;
    if (!editable_ || selection == wxNOT_FOUND || target < 0 ||
        static_cast<std::size_t>(target) >= languages_.size()) return;
    auto languages = languages_;
    std::swap(languages[static_cast<std::size_t>(selection)], languages[static_cast<std::size_t>(target)]);
    commit(std::move(languages));
}

} // namespace audioguia
