#include "ui/MainFrame.h"

#include "app/AppController.h"
#include "ui/PreferencesDialog.h"
#include "ui/SequenceEditor.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/display.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace audioguia {
namespace {

constexpr std::array<Language, 3> languages{Language::es, Language::en, Language::pt};
constexpr std::array<Mode, 3> modes{Mode::simple, Mode::sequential, Mode::custom};
const wxColour blue(18, 83, 139);
const wxColour row_colour(239, 242, 245);
const wxColour selected_colour(222, 237, 249);

wxString utf8(const std::string& text) {
    return wxString::FromUTF8(text.c_str());
}

void set_label(wxStaticText* control, const wxString& text) {
    if (control->GetLabel() != text) {
        control->SetLabel(text);
        control->SetToolTip(text);
    }
}

wxStaticText* line(wxWindow* parent, int height = 23) {
    auto* text = new wxStaticText(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    text->SetMinSize(parent->FromDIP(wxSize(0, height)));
    return text;
}

bool num_lock_on() {
#ifdef _WIN32
    return (::GetKeyState(VK_NUMLOCK) & 1) != 0;
#else
    return wxGetKeyState(WXK_NUMLOCK);
#endif
}

class ModalInputScope final {
public:
    explicit ModalInputScope(bool& flag) : flag_(flag), previous_(flag) { flag_ = true; }
    ~ModalInputScope() { flag_ = previous_; }
    ModalInputScope(const ModalInputScope&) = delete;
    ModalInputScope& operator=(const ModalInputScope&) = delete;
private:
    bool& flag_;
    bool previous_;
};

} // namespace

MainFrame::MainFrame(AppController& controller, std::function<void()> on_compact)
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8(u8"Tolkeyen Patagonia Turismo | Audiogu\u00eda")),
      controller_(controller), on_compact_(std::move(on_compact)) {
    auto font = GetFont();
    font.SetPointSize(std::max(11, font.GetPointSize()));
    SetFont(font);
    SetBackgroundColour(*wxWHITE);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* header = new wxPanel(this);
    auto* header_row = new wxBoxSizer(wxHORIZONTAL);
    auto* identity = new wxBoxSizer(wxVERTICAL);
    auto* brand = new wxStaticText(header, wxID_ANY, "Tolkeyen");
    brand->SetFont(font.Bold().Larger());
    brand->SetForegroundColour(blue);
    auto* subtitle = new wxStaticText(header, wxID_ANY, "Patagonia Turismo");
    subtitle->SetForegroundColour(blue);
    identity->Add(brand);
    identity->Add(subtitle, 0, wxTOP, FromDIP(1));
    header_row->Add(identity, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    auto* compact = new wxButton(header, wxID_ANY, "Vista compacta");
    auto* settings = new wxButton(header, wxID_ANY, "Configurar audios y salida");
    for (auto* button : {compact, settings}) button->SetMinSize(FromDIP(wxSize(-1, 38)));
    header_row->Add(compact, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    header_row->Add(settings, 0, wxALIGN_CENTER_VERTICAL);
    auto* header_padding = new wxBoxSizer(wxVERTICAL);
    header_padding->Add(header_row, 1, wxEXPAND | wxALL, FromDIP(8));
    header->SetSizer(header_padding);
    outer->Add(header, 0, wxEXPAND);
    compact->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (on_compact_) on_compact_();
    });
    settings->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_preferences(); });

    auto* scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                         wxHSCROLL | wxVSCROLL | wxTAB_TRAVERSAL);
    scroll->SetScrollRate(FromDIP(12), FromDIP(12));
    scroll->SetBackgroundColour(*wxWHITE);
    auto* content = new wxBoxSizer(wxVERTICAL);
    auto* selectors = new wxBoxSizer(wxHORIZONTAL);
    const auto add_choice = [&](const wxString& title, wxChoice*& choice, int proportion) {
        auto* group = new wxBoxSizer(wxHORIZONTAL);
        group->Add(new wxStaticText(scroll, wxID_ANY, title), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        choice = new wxChoice(scroll, wxID_ANY);
        choice->SetMinSize(FromDIP(wxSize(100, 32)));
        choice->SetName(title);
        group->Add(choice, 1, wxALIGN_CENTER_VERTICAL);
        selectors->Add(group, proportion, wxEXPAND | wxRIGHT, FromDIP(12));
    };
    add_choice("Recorrido", direction_, 1);
    direction_->Append("UP");
    direction_->Append("DOWN");
    add_choice("Modo", mode_, 2);
    for (const auto mode : modes) mode_->Append(utf8(label(mode)));
    add_choice("Idioma", language_, 2);
    for (const auto language : languages) language_->Append(utf8(label(language)));
    content->Add(selectors, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    sequence_ = new SequenceEditor(controller_, scroll, [this] { refresh_state(); });
    prepare_ = line(scroll, 25);
    prepare_->SetForegroundColour(blue);
    prepare_->SetFont(font.Bold());
    content->Add(prepare_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    for (const auto direction : {Direction::up, Direction::down}) {
        auto* column = new wxBoxSizer(wxVERTICAL);
        auto* heading = line(scroll, 27);
        heading->SetFont(font.Bold());
        set_label(heading, direction == Direction::up ? "UP | 8 puntos" : "DOWN | 6 puntos");
        if (direction == Direction::up) up_heading_ = heading;
        else down_heading_ = heading;
        column->Add(heading, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
        const int count = direction == Direction::up ? 8 : 6;
        for (int number = 1; number <= count; ++number) {
            const auto found = std::find_if(controller_.config().points.begin(), controller_.config().points.end(),
                [direction, number](const Point& point) {
                    return point.direction == direction && point.number == number;
                });
            PointControls controls;
            controls.id = found == controller_.config().points.end() ? std::string{} : found->id;
            controls.direction = direction;
            controls.number = number;
            controls.panel = new wxPanel(scroll);
            controls.panel->SetBackgroundColour(row_colour);
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            controls.number_button = new wxButton(controls.panel, wxID_ANY, std::to_string(number),
                                                   wxDefaultPosition, FromDIP(wxSize(44, 44)), wxBU_EXACTFIT);
            controls.number_button->SetMinSize(FromDIP(wxSize(44, 44)));
            controls.number_button->SetMaxSize(FromDIP(wxSize(44, 44)));
            controls.number_button->SetBackgroundColour(wxColour(151, 205, 240));
            controls.number_button->SetForegroundColour(wxColour(16, 53, 82));
            controls.number_button->SetFont(font.Bold());
            const auto name = found == controller_.config().points.end()
                ? wxString("Punto sin configurar") : utf8(found->name);
            controls.name_button = new wxButton(controls.panel, wxID_ANY, name,
                                                 wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBORDER_NONE);
            controls.name_button->SetMinSize(FromDIP(wxSize(0, 44)));
            controls.name_button->SetBackgroundColour(row_colour);
            controls.name_button->SetForegroundColour(wxColour(32, 49, 63));
            const auto accessible_name = utf8(label(direction) + " " + std::to_string(number) + ": ") + name;
            controls.name_button->SetName(accessible_name);
            controls.number_button->SetName(accessible_name);
            controls.name_button->SetToolTip(accessible_name);
            controls.number_button->SetToolTip(accessible_name);
            const auto play = [this, direction, number](wxCommandEvent&) { play_point(direction, number); };
            controls.number_button->Bind(wxEVT_BUTTON, play);
            controls.name_button->Bind(wxEVT_BUTTON, play);
            row->Add(controls.number_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
            row->Add(controls.name_button, 1, wxEXPAND);
            controls.panel->SetSizer(row);
            column->Add(controls.panel, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
            points_.push_back(controls);
        }
        if (direction == Direction::down) {
            column->Add(sequence_, 0, wxEXPAND | wxTOP, FromDIP(4));
        }
        columns->Add(column, 1, wxEXPAND | (direction == Direction::up ? wxRIGHT : 0),
                     direction == Direction::up ? FromDIP(14) : 0);
    }
    content->Add(columns, 1, wxEXPAND);
    auto* content_padding = new wxBoxSizer(wxVERTICAL);
    content_padding->Add(content, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    scroll->SetSizer(content_padding);
    outer->Add(scroll, 1, wxEXPAND);

    auto* transport = new wxPanel(this);
    transport->SetBackgroundColour(wxColour(247, 249, 251));
    auto* playback = new wxBoxSizer(wxVERTICAL);
    auto* state_row = new wxBoxSizer(wxHORIZONTAL);
    playing_title_ = line(transport, 28);
    playing_title_->SetFont(font.Bold().Larger());
    state_row->Add(playing_title_, 1, wxEXPAND | wxRIGHT, FromDIP(12));
    status_ = line(transport, 28);
    status_->SetMinSize(FromDIP(wxSize(220, 28)));
    status_->SetForegroundColour(blue);
    status_->SetFont(font.Bold());
    state_row->Add(status_, 0, wxEXPAND);
    playback->Add(state_row, 0, wxEXPAND);
    playing_detail_ = line(transport, 20);
    playback->Add(playing_detail_, 0, wxEXPAND | wxBOTTOM, FromDIP(3));
    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    pause_ = new wxButton(transport, wxID_ANY, "Pausar");
    stop_ = new wxButton(transport, wxID_ANY, "Detener");
    for (auto* button : {pause_, stop_}) {
        button->SetMinSize(FromDIP(wxSize(110, 40)));
        actions->Add(button, 0, wxRIGHT, FromDIP(8));
    }
    actions->Add(new wxStaticText(transport, wxID_ANY, "Volumen"), 0,
                 wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
    volume_ = new wxSlider(transport, wxID_ANY, 75, 0, 100);
    volume_->SetMinSize(FromDIP(wxSize(100, 40)));
    volume_->SetName("Volumen de 0 a 100");
    actions->Add(volume_, 1, wxALIGN_CENTER_VERTICAL);
    volume_label_ = line(transport);
    volume_label_->SetMinSize(FromDIP(wxSize(52, 23)));
    actions->Add(volume_label_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    playback->Add(actions, 0, wxEXPAND);
    feedback_ = new wxTextCtrl(transport, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_READONLY | wxTE_MULTILINE | wxBORDER_NONE);
    feedback_->SetMinSize(FromDIP(wxSize(0, 28)));
    feedback_->SetBackgroundColour(transport->GetBackgroundColour());
    feedback_->SetForegroundColour(wxColour(147, 44, 34));
    feedback_->SetName("Avisos de audio y guardado");
    playback->Add(feedback_, 0, wxEXPAND | wxTOP, FromDIP(3));
    keyboard_status_ = line(transport, 20);
    playback->Add(keyboard_status_, 0, wxEXPAND);
    auto* playback_padding = new wxBoxSizer(wxVERTICAL);
    playback_padding->Add(playback, 1, wxEXPAND | wxALL, FromDIP(8));
    transport->SetSizer(playback_padding);
    outer->Add(transport, 0, wxEXPAND);
    SetSizer(outer);

    direction_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        auto preferences = controller_.config().preferences;
        preferences.direction = direction_->GetSelection() == 0 ? Direction::up : Direction::down;
        change_preferences(std::move(preferences));
    });
    mode_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const auto index = mode_->GetSelection();
        if (index < 0 || static_cast<std::size_t>(index) >= modes.size()) return;
        auto preferences = controller_.config().preferences;
        preferences.mode = modes[static_cast<std::size_t>(index)];
        change_preferences(std::move(preferences));
    });
    language_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const auto index = language_->GetSelection();
        if (index < 0 || static_cast<std::size_t>(index) >= languages.size()) return;
        auto preferences = controller_.config().preferences;
        preferences.simple_language = languages[static_cast<std::size_t>(index)];
        change_preferences(std::move(preferences));
    });
    volume_->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        auto preferences = controller_.config().preferences;
        preferences.volume = static_cast<float>(volume_->GetValue()) / 100.0F;
        change_preferences(std::move(preferences));
    });
    pause_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        try {
            controller_.toggle_pause();
            local_error_.clear();
        } catch (const std::exception& error) {
            local_error_ = std::string("No se pudo pausar o reanudar: ") + error.what();
        }
        refresh_state();
    });
    stop_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        try {
            controller_.stop();
            local_error_.clear();
        } catch (const std::exception& error) {
            local_error_ = std::string("No se pudo detener: ") + error.what();
        }
        refresh_state();
    });

    wxRect work = wxGetClientDisplayRect();
    if (wxDisplay::GetCount() != 0) work = wxDisplay(0u).GetClientArea();
    const auto desired = FromDIP(wxSize(1000, 720));
    const auto minimum = FromDIP(wxSize(680, 480));
    const int margin = FromDIP(12);
    const wxSize size(std::min(desired.x, std::max(1, work.width - margin * 2)),
                      std::min(desired.y, std::max(1, work.height - margin * 2)));
    SetMinSize(wxSize(std::min(minimum.x, size.x), std::min(minimum.y, size.y)));
    SetSize(size);
    SetPosition(wxPoint(work.x + (work.width - size.x) / 2, work.y + (work.height - size.y) / 2));
    Layout();
    scroll->FitInside();
    refresh_state();
}

void MainFrame::change_preferences(Preferences preferences) {
    try {
        controller_.set_preferences(std::move(preferences));
        local_error_.clear();
    } catch (const std::exception& error) {
        local_error_ = std::string("No se pudo cambiar la preferencia: ") + error.what();
    }
    refresh_state();
}

void MainFrame::play_point(Direction direction, int number) {
    try {
        controller_.play(direction, number);
        local_error_.clear();
    } catch (const std::exception& error) {
        local_error_ = std::string("No se pudo iniciar el punto: ") + error.what();
    }
    refresh_state();
}

void MainFrame::open_preferences() {
    if (settings_open_) return;
    try {
        ModalInputScope scope(settings_open_);
        PreferencesDialog dialog(controller_, this);
        dialog.ShowModal();
    } catch (const std::exception& error) {
        local_error_ = std::string("No se pudo abrir la configuracion: ") + error.what();
    }
    refresh_state();
}

void MainFrame::refresh_state() {
    const auto& preferences = controller_.config().preferences;
    const int direction = preferences.direction == Direction::up ? 0 : 1;
    if (direction_->GetSelection() != direction) direction_->SetSelection(direction);
    const auto mode = std::find(modes.begin(), modes.end(), preferences.mode);
    const int mode_index = static_cast<int>(std::distance(modes.begin(), mode));
    if (mode_->GetSelection() != mode_index) mode_->SetSelection(mode_index);
    const auto language = std::find(languages.begin(), languages.end(), preferences.simple_language);
    const int language_index = static_cast<int>(std::distance(languages.begin(), language));
    if (language_->GetSelection() != language_index) language_->SetSelection(language_index);
    const bool simple = preferences.mode == Mode::simple;
    if (language_->IsEnabled() != simple) language_->Enable(simple);
    sequence_->refresh_state();
    set_label(prepare_, wxString::FromUTF8(u8"PREPARAR SIGUIENTE | ") + utf8(controller_.selection_text()));

    const auto up_colour = preferences.direction == Direction::up ? blue : wxColour(75, 89, 101);
    const auto down_colour = preferences.direction == Direction::down ? blue : wxColour(75, 89, 101);
    if (up_heading_->GetForegroundColour() != up_colour) up_heading_->SetForegroundColour(up_colour);
    if (down_heading_->GetForegroundColour() != down_colour) down_heading_->SetForegroundColour(down_colour);
    const auto& active = controller_.active_plan();
    const auto& state = controller_.state();
    for (auto& controls : points_) {
        const bool highlighted = active && active->point_id == controls.id && state.status != PlaybackStatus::stopped;
        if (highlighted != controls.highlighted) {
            controls.highlighted = highlighted;
            const auto colour = highlighted ? selected_colour : row_colour;
            controls.panel->SetBackgroundColour(colour);
            controls.name_button->SetBackgroundColour(colour);
            controls.panel->Refresh(false);
        }
        const auto point = std::find_if(controller_.config().points.begin(), controller_.config().points.end(),
            [&controls](const Point& candidate) { return candidate.id == controls.id; });
        if (point != controller_.config().points.end() && controls.name_button->GetLabel() != utf8(point->name)) {
            controls.name_button->SetLabel(utf8(point->name));
            controls.name_button->SetToolTip(utf8(point->name));
        }
    }
    set_label(playing_title_, utf8(controller_.playback_title()));
    set_label(playing_detail_, utf8(controller_.playback_detail()));
    set_label(status_, utf8(controller_.status_text()));
    const wxString pause_label = state.status == PlaybackStatus::paused ? "Reanudar" : "Pausar";
    if (pause_->GetLabel() != pause_label) pause_->SetLabel(pause_label);
    const bool pausable = state.status == PlaybackStatus::playing || state.status == PlaybackStatus::paused;
    if (pause_->IsEnabled() != pausable) pause_->Enable(pausable);
    // Keep Stop focusable at rest; disabling it moves native focus to Volume.
    const int volume = std::clamp(static_cast<int>(std::lround(preferences.volume * 100.0F)), 0, 100);
    if (!volume_->HasCapture() && volume_->GetValue() != volume) volume_->SetValue(volume);
    set_label(volume_label_, utf8(std::to_string(volume) + "%"));

    std::string feedback = local_error_;
    for (const auto& message : {state.message, controller_.warning()}) {
        if (!message.empty()) {
            if (!feedback.empty()) feedback += "\n";
            feedback += message;
        }
    }
    if (feedback_->GetValue() != utf8(feedback)) feedback_->ChangeValue(utf8(feedback));
    if (feedback_->IsShown() != !feedback.empty()) {
        feedback_->Show(!feedback.empty());
        Layout();
    }
    auto keyboard = std::string("Bloq Num: ") + (num_lock_on() ? "ACTIVADO" : "DESACTIVADO");
    keyboard += settings_open_ ? " | Atajos suspendidos mientras configura."
        : IsActive() ? " | Teclado: mantenga esta ventana en foco."
                     : " | Sin foco: vuelva a la ventana para usar el teclado.";
    set_label(keyboard_status_, utf8(keyboard));
}

} // namespace audioguia
