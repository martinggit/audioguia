#include "ui/SequenceEditor.h"

#include "app/AppController.h"

#include <wx/button.h>
#include <wx/dataobj.h>
#include <wx/dnd.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <array>
#include <exception>
#include <utility>

namespace audioguia {
namespace {

constexpr std::array<Language, 3> all_languages{Language::es, Language::en, Language::pt};
wxString utf8(const std::string& text) { return wxString::FromUTF8(text.c_str()); }

// Local-only drag: external text and drags from another editor are rejected.
class SequenceDropTarget final : public wxTextDropTarget {
public:
    SequenceDropTarget(wxListCtrl& list, std::function<int()> count,
                       std::function<void(int, int)> move)
        : list_(list), count_(std::move(count)), move_(std::move(move)) {}

    int source{-1};
    wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult) override {
        const auto row = target(x, y);
        highlight(row);
        return source >= 0 && row >= 0 ? wxDragMove : wxDragNone;
    }
    void OnLeave() override { highlight(-1); }
    bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override {
        const int row = target(x, y);
        highlight(-1);
        if (source < 0 || row < 0 || data != "audioguia-sequence") return false;
        move_(source, row);
        return true;
    }

private:
    int target(wxCoord x, wxCoord y) const {
        if (source < 0) return -1;
        int flags = 0;
        const auto row = list_.HitTest(wxPoint(x, y), flags);
        return row >= 0 && row < count_() ? static_cast<int>(row) : -1;
    }
    void highlight(int row) {
        for (long i = 0; i < list_.GetItemCount(); ++i)
            list_.SetItemState(i, i == row ? wxLIST_STATE_DROPHILITED : 0, wxLIST_STATE_DROPHILITED);
    }
    wxListCtrl& list_;
    std::function<int()> count_;
    std::function<void(int, int)> move_;
};

} // namespace

SequenceEditor::SequenceEditor(AppController& controller, wxWindow* parent,
                               std::function<void()> on_change)
    : wxPanel(parent), controller_(controller), on_change_(std::move(on_change)) {
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* heading = new wxStaticText(this, wxID_ANY, utf8("ORDEN DE REPRODUCCION"));
    heading->SetFont(GetFont().Bold());
    layout->Add(heading, 0, wxBOTTOM, FromDIP(6));
    layout->Add(new wxStaticText(this, wxID_ANY,
        "Arrastre una fila activa a su nueva posicion, o use Subir / Bajar.\n"
        "Marque los idiomas que desea escuchar. Espacio activa o desactiva la fila."),
        0, wxBOTTOM, FromDIP(10));

    order_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SIMPLE);
    order_->SetName("Orden de idiomas: arrastre o seleccione una fila y use Subir o Bajar");
    auto font = GetFont();
    font.SetPointSize(std::max(13, font.GetPointSize()));
    order_->SetFont(font);
    order_->EnableCheckBoxes();
    order_->InsertColumn(0, "Posicion", wxLIST_FORMAT_LEFT, FromDIP(100));
    order_->InsertColumn(1, "Idioma", wxLIST_FORMAT_LEFT, FromDIP(185));
    order_->InsertColumn(2, "Reproduccion", wxLIST_FORMAT_LEFT, FromDIP(150));
    order_->SetMinSize(FromDIP(wxSize(460, 148)));
    layout->Add(order_, 1, wxEXPAND | wxBOTTOM, FromDIP(10));

    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    up_ = new wxButton(this, wxID_ANY, "Subir");
    down_ = new wxButton(this, wxID_ANY, "Bajar");
    for (auto* button : {up_, down_}) {
        button->SetMinSize(FromDIP(wxSize(100, 36)));
        actions->Add(button, 0, wxRIGHT, FromDIP(8));
    }
    layout->Add(actions, 0, wxBOTTOM, FromDIP(6));
    notice_ = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    notice_->SetMinSize(FromDIP(wxSize(0, 24)));
    notice_->SetForegroundColour(wxColour(64, 82, 96));
    layout->Add(notice_, 0, wxEXPAND);
    SetSizer(layout);

    order_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
        if (syncing_) return;
        const auto row = event.GetIndex();
        if (row >= 0 && static_cast<std::size_t>(row) < rows_.size()) selected_ = rows_[row];
        update_buttons();
    });
    order_->Bind(wxEVT_LIST_ITEM_CHECKED, [this](wxListEvent& event) { check_language(event.GetIndex(), true); });
    order_->Bind(wxEVT_LIST_ITEM_UNCHECKED, [this](wxListEvent& event) { check_language(event.GetIndex(), false); });
    up_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { move_selected(-1); });
    down_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { move_selected(1); });
    auto* drop = new SequenceDropTarget(*order_, [this] { return static_cast<int>(languages_.size()); },
        [this](int source, int target) { move_to(source, target); });
    order_->SetDropTarget(drop);
    order_->Bind(wxEVT_LIST_BEGIN_DRAG, [this, drop](wxListEvent& event) {
        const auto row = event.GetIndex();
        if (!editable_ || row < 0 || static_cast<std::size_t>(row) >= languages_.size()) return;
        selected_ = rows_[row];
        drop->source = static_cast<int>(row);
        wxTextDataObject data("audioguia-sequence");
        wxDropSource source(order_);
        source.SetData(data);
        source.DoDragDrop(wxDrag_DefaultMove);
        drop->source = -1;
        drop->OnLeave();
        update_buttons();
    });
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
    syncing_ = true;
    editable_ = preferences.mode == Mode::custom;
    languages_ = preferences.custom_languages;
    rows_ = languages_;
    for (const auto language : all_languages)
        if (std::find(rows_.begin(), rows_.end(), language) == rows_.end()) rows_.push_back(language);
    if (!selected_) selected_ = rows_.front();
    order_->Freeze();
    order_->DeleteAllItems();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool active = i < languages_.size();
        const long row = order_->InsertItem(static_cast<long>(i), active ? std::to_string(i + 1) : "-");
        order_->SetItem(row, 1, utf8(label(rows_[i])));
        order_->SetItem(row, 2, active ? "Incluido" : "No se reproduce");
        order_->CheckItem(row, active);
        order_->SetItemTextColour(row, active ? wxColour(25, 46, 64) : wxColour(110, 117, 123));
        if (selected_ == rows_[i]) order_->SetItemState(row,
            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }
    order_->Enable(editable_);
    order_->Thaw();
    syncing_ = false;
    update_buttons();
    set_notice("Los idiomas desmarcados no se reproducen. Debe quedar al menos uno.");
}

void SequenceEditor::check_language(long row, bool checked) {
    if (syncing_ || !editable_ || row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return;
    const auto language = rows_[row];
    selected_ = language;
    auto languages = languages_;
    const auto found = std::find(languages.begin(), languages.end(), language);
    if (checked && found == languages.end()) languages.push_back(language);
    else if (!checked && found != languages.end()) {
        if (languages.size() == 1) {
            // Restore only the checkbox inside this native check notification.
            syncing_ = true;
            order_->CheckItem(row, true);
            syncing_ = false;
            set_notice("Debe quedar al menos un idioma. Active otro antes de quitar el ultimo.");
            return;
        }
        languages.erase(found);
    } else return;
    // Rebuilding native rows from a check notification invalidates its item.
    // Defer the update until Windows has finished processing the click/Space.
    syncing_ = true;
    CallAfter([this, languages = std::move(languages)]() mutable { commit(std::move(languages)); });
}

void SequenceEditor::update_buttons() {
    const auto found = selected_ ? std::find(languages_.begin(), languages_.end(), *selected_) : languages_.end();
    const bool active = editable_ && found != languages_.end();
    up_->Enable(active && found != languages_.begin());
    down_->Enable(active && found + 1 != languages_.end());
}

void SequenceEditor::commit(std::vector<Language> languages) {
    try {
        auto preferences = controller_.config().preferences;
        preferences.custom_languages = std::move(languages);
        controller_.set_preferences(std::move(preferences));
        refresh_state();
        set_notice(controller_.warning().empty()
            ? "Orden guardado para el siguiente punto. La pista activa no cambia."
            : utf8(controller_.warning()));
        if (on_change_) on_change_();
    } catch (const std::exception& error) {
        refresh_state();
        set_notice(utf8(std::string("No se pudo cambiar la secuencia: ") + error.what()));
    }
}

void SequenceEditor::move_to(int source, int target) {
    const int count = static_cast<int>(languages_.size());
    if (!editable_ || source < 0 || target < 0 || source >= count || target >= count || source == target) return;
    auto languages = languages_;
    const auto language = languages[static_cast<std::size_t>(source)];
    selected_ = language;
    languages.erase(languages.begin() + source);
    languages.insert(languages.begin() + target, language);
    commit(std::move(languages));
}

void SequenceEditor::move_selected(int offset) {
    if (!selected_) return;
    const auto found = std::find(languages_.begin(), languages_.end(), *selected_);
    if (found == languages_.end()) return;
    const int source = static_cast<int>(std::distance(languages_.begin(), found));
    move_to(source, source + offset);
}

} // namespace audioguia
