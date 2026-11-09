#include "ui/MainFrame.h"

#include "app/AppController.h"
#include "input/KeyboardController.h"
#include "ui/PreferencesDialog.h"
#include "ui/SequenceEditor.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcmemory.h>
#include <wx/dialog.h>
#include <wx/display.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
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
const wxColour ink(25, 46, 64);
const wxColour surface(240, 244, 248);
const wxColour green(20, 104, 71);
const wxColour amber(133, 83, 5);
const wxColour red(158, 43, 40);

wxString utf8(const std::string& text) { return wxString::FromUTF8(text.c_str()); }

void set_label(wxStaticText* control, const wxString& text) {
    if (control->GetLabel() != text) {
        control->SetLabel(text);
        control->SetToolTip(text);
    }
}

wxStaticText* line(wxWindow* parent, int height = 24) {
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

wxColour state_colour(PlaybackStatus status) {
    if (status == PlaybackStatus::playing) return green;
    if (status == PlaybackStatus::paused || status == PlaybackStatus::preparing) return amber;
    if (status == PlaybackStatus::error) return red;
    return blue;
}

// Keep native buttons (mouse, keyboard, accessibility) and add a large numpad badge.
wxBitmap key_badge(wxWindow* window, int key, PlaybackStatus status) {
    wxBitmap bitmap;
    bitmap.CreateWithDIPSize(wxSize(76, 54), window->GetDPIScaleFactor());
    wxMemoryDC dc(bitmap);
    dc.SetBackground(wxBrush(window->GetBackgroundColour()));
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(state_colour(status)));
    dc.DrawRectangle(wxRect(wxPoint(0, 0), window->FromDIP(wxSize(58, 54))));
    auto font = window->GetFont();
    font.SetPointSize(27);
    dc.SetFont(font.Bold());
    dc.SetTextForeground(*wxWHITE);
    dc.DrawLabel(std::to_string(key), wxRect(wxPoint(0, 0), window->FromDIP(wxSize(58, 54))), wxALIGN_CENTER);
    dc.SelectObject(wxNullBitmap);
    return bitmap;
}

class ModalInputScope final {
public:
    explicit ModalInputScope(bool& flag) : flag_(flag), previous_(flag) { flag_ = true; }
    ~ModalInputScope() { flag_ = previous_; }
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
    SetBackgroundColour(surface);
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* transport = new wxPanel(this);
    transport->SetBackgroundColour(ink);
    transport->SetForegroundColour(*wxWHITE);
    auto* playback = new wxBoxSizer(wxVERTICAL);
    auto* heading = new wxBoxSizer(wxHORIZONTAL);
    auto* brand = new wxStaticText(transport, wxID_ANY, "TOLKEYEN  /  AUDIOGUIA");
    brand->SetForegroundColour(wxColour(180, 207, 229));
    heading->Add(brand, 1, wxALIGN_CENTER_VERTICAL);
    status_ = line(transport, 25);
    status_->SetMinSize(FromDIP(wxSize(230, 25)));
    status_->SetFont(font.Bold());
    heading->Add(status_, 0, wxEXPAND);
    playback->Add(heading, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    auto* now = new wxBoxSizer(wxHORIZONTAL);
    auto* description = new wxBoxSizer(wxVERTICAL);
    playing_title_ = line(transport, 34);
    playing_title_->SetFont(font.Bold().Larger());
    playing_detail_ = line(transport, 25);
    description->Add(playing_title_, 0, wxEXPAND);
    description->Add(playing_detail_, 0, wxEXPAND);
    now->Add(description, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    pause_ = new wxButton(transport, wxID_ANY, "ENTER  |  Pausar");
    stop_ = new wxButton(transport, wxID_ANY, "0  |  Detener");
    pause_->SetBackgroundColour(wxColour(226, 236, 245));
    stop_->SetBackgroundColour(red);
    stop_->SetForegroundColour(*wxWHITE);
    for (auto* button : {pause_, stop_}) {
        button->SetMinSize(FromDIP(wxSize(170, 60)));
        button->SetFont(font.Bold());
        now->Add(button, 0, wxLEFT, FromDIP(8));
    }
    playback->Add(now, 0, wxEXPAND);
    feedback_ = new wxTextCtrl(transport, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_READONLY | wxTE_MULTILINE | wxBORDER_NONE);
    feedback_->SetMinSize(FromDIP(wxSize(0, 42)));
    feedback_->SetBackgroundColour(ink);
    feedback_->SetForegroundColour(wxColour(255, 200, 180));
    feedback_->SetName("Avisos de audio y guardado");
    playback->Add(feedback_, 0, wxEXPAND | wxTOP, FromDIP(6));
    feedback_->Hide();
    auto* playback_padding = new wxBoxSizer(wxVERTICAL);
    playback_padding->Add(playback, 1, wxEXPAND | wxALL, FromDIP(14));
    transport->SetSizer(playback_padding);
    outer->Add(transport, 0, wxEXPAND);

    // Only the point area scrolls on unusually small screens; transport stays visible.
    scroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxVSCROLL | wxTAB_TRAVERSAL);
    scroll_->SetScrollRate(0, FromDIP(12));
    scroll_->SetBackgroundColour(surface);
    auto* content = new wxBoxSizer(wxVERTICAL);
    auto* direction_row = new wxBoxSizer(wxHORIZONTAL);
    up_ = new wxToggleButton(scroll_, wxID_ANY, "UP | 8 puntos");
    down_ = new wxToggleButton(scroll_, wxID_ANY, "DOWN | 6 puntos");
    for (auto* button : {up_, down_}) {
        button->SetMinSize(FromDIP(wxSize(175, 44)));
        button->SetFont(font.Bold());
        direction_row->Add(button, 0, wxRIGHT, FromDIP(8));
    }
    auto* hint = line(scroll_);
    set_label(hint, "Una tecla o un clic reproduce el punto");
    hint->SetForegroundColour(ink);
    direction_row->Add(hint, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    content->Add(direction_row, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    for (const auto direction : {Direction::up, Direction::down}) {
        auto* panel = new wxPanel(scroll_);
        if (direction == Direction::up) up_points_ = panel;
        else down_points_ = panel;
        auto* grid = new wxGridSizer(0, 2, FromDIP(8), FromDIP(10));
        // Insert in stable point-number order, independent of JSON enumeration.
        const int count = direction == Direction::up ? 8 : 6;
        for (int number = 1; number <= count; ++number) {
            const auto found = std::find_if(controller_.config().points.begin(), controller_.config().points.end(),
                [direction, number](const Point& point) { return point.direction == direction && point.number == number; });
            PointControls controls;
            controls.id = found == controller_.config().points.end() ? std::string{} : found->id;
            controls.direction = direction;
            controls.number = number;
            controls.name_button = new wxButton(panel, wxID_ANY, wxEmptyString,
                                                 wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
            controls.name_button->SetMinSize(FromDIP(wxSize(0, 76)));
            controls.name_button->SetFont(font.Bold().Larger());
            controls.name_button->SetForegroundColour(ink);
            controls.name_button->SetBackgroundColour(*wxWHITE);
            controls.name_button->SetBitmapMargins(FromDIP(12), FromDIP(8));
            controls.name_button->Bind(wxEVT_BUTTON, [this, direction, number](wxCommandEvent&) {
                play_point(direction, number);
            });
            grid->Add(controls.name_button, 1, wxEXPAND);
            points_.push_back(controls);
        }
        panel->SetSizer(grid);
        content->Add(panel, 1, wxEXPAND);
    }
    auto* content_padding = new wxBoxSizer(wxVERTICAL);
    content_padding->Add(content, 1, wxEXPAND | wxALL, FromDIP(12));
    scroll_->SetSizer(content_padding);
    outer->Add(scroll_, 1, wxEXPAND);

    auto* setup = new wxPanel(this);
    setup->SetBackgroundColour(*wxWHITE);
    auto* setup_layout = new wxBoxSizer(wxVERTICAL);
    prepare_ = line(setup, 25);
    prepare_->SetFont(font.Bold());
    prepare_->SetForegroundColour(blue);
    setup_layout->Add(prepare_, 0, wxEXPAND | wxBOTTOM, FromDIP(5));
    auto* selectors = new wxBoxSizer(wxHORIZONTAL);
    const auto add_choice = [&](const wxString& title, wxChoice*& choice) {
        selectors->Add(new wxStaticText(setup, wxID_ANY, title), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        choice = new wxChoice(setup, wxID_ANY);
        choice->SetMinSize(FromDIP(wxSize(135, 32)));
        choice->SetName(title);
        selectors->Add(choice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    };
    add_choice("Modo", mode_);
    for (const auto mode : modes) mode_->Append(utf8(label(mode)));
    add_choice("Idioma", language_);
    for (const auto language : languages) language_->Append(utf8(label(language)));
    sequence_button_ = new wxButton(setup, wxID_ANY, "Editar idiomas...");
    sequence_button_->SetMinSize(FromDIP(wxSize(-1, 34)));
    selectors->Add(sequence_button_, 0, wxALIGN_CENTER_VERTICAL);
    setup_layout->Add(selectors, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    auto* tools = new wxBoxSizer(wxHORIZONTAL);
    tools->Add(new wxStaticText(setup, wxID_ANY, "Volumen"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    volume_ = new wxSlider(setup, wxID_ANY, 75, 0, 100);
    volume_->SetMinSize(FromDIP(wxSize(90, 32)));
    volume_->SetName("Volumen de 0 a 100");
    tools->Add(volume_, 1, wxALIGN_CENTER_VERTICAL);
    volume_label_ = line(setup);
    volume_label_->SetMinSize(FromDIP(wxSize(48, 24)));
    tools->Add(volume_label_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
    auto* compact = new wxButton(setup, wxID_ANY, "Vista compacta");
    auto* settings = new wxButton(setup, wxID_ANY, "Configurar audios y salida");
    for (auto* button : {compact, settings}) {
        button->SetMinSize(FromDIP(wxSize(-1, 34)));
        tools->Add(button, 0, wxLEFT, FromDIP(8));
    }
    setup_layout->Add(tools, 0, wxEXPAND);
    auto* setup_padding = new wxBoxSizer(wxVERTICAL);
    setup_padding->Add(setup_layout, 1, wxEXPAND | wxALL, FromDIP(12));
    setup->SetSizer(setup_padding);
    outer->Add(setup, 0, wxEXPAND);

    auto* keyboard_panel = new wxPanel(this);
    auto* keyboard_row = new wxBoxSizer(wxHORIZONTAL);
    keyboard_status_ = line(keyboard_panel, 24);
    keyboard_status_->SetFont(font.Bold());
    keyboard_row->Add(keyboard_status_, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(8));
    auto* ready = new wxButton(keyboard_panel, wxID_ANY, "Usar numpad");
    ready->SetMinSize(FromDIP(wxSize(130, 36)));
    ready->SetToolTip("Devuelve el foco a los controles de operacion sin iniciar ni detener audio.");
    keyboard_row->Add(ready, 0, wxALL, FromDIP(6));
    keyboard_panel->SetSizer(keyboard_row);
    outer->Add(keyboard_panel, 0, wxEXPAND);
    SetSizer(outer);

    up_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        auto preferences = controller_.config().preferences;
        preferences.direction = Direction::up;
        change_preferences(std::move(preferences));
        focus_transport();
    });
    down_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        auto preferences = controller_.config().preferences;
        preferences.direction = Direction::down;
        change_preferences(std::move(preferences));
        focus_transport();
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
    ready->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { focus_transport(); });
    compact->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (on_compact_) on_compact_(); });
    settings->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_preferences(); });
    sequence_button_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { edit_sequence(); });
    pause_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        try { controller_.toggle_pause(); local_error_.clear(); }
        catch (const std::exception& error) { local_error_ = std::string("No se pudo pausar o reanudar: ") + error.what(); }
        refresh_state();
    });
    stop_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        try { controller_.stop(); local_error_.clear(); }
        catch (const std::exception& error) { local_error_ = std::string("No se pudo detener: ") + error.what(); }
        refresh_state();
    });

    wxRect work = wxGetClientDisplayRect();
    if (wxDisplay::GetCount() != 0) work = wxDisplay(0u).GetClientArea();
    const auto desired = FromDIP(wxSize(1120, 780));
    const auto minimum = FromDIP(wxSize(760, 580));
    const int margin = FromDIP(12);
    const wxSize size(std::min(desired.x, std::max(1, work.width - margin * 2)),
                      std::min(desired.y, std::max(1, work.height - margin * 2)));
    SetMinSize(wxSize(std::min(minimum.x, size.x), std::min(minimum.y, size.y)));
    SetSize(size);
    SetPosition(wxPoint(work.x + (work.width - size.x) / 2, work.y + (work.height - size.y) / 2));
    refresh_state();
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        for (auto& controls : points_) controls.badge_key = -1;
        event.Skip();
    });
}

void MainFrame::focus_transport() {
    stop_->SetFocus();
    refresh_state();
}

void MainFrame::change_preferences(Preferences preferences) {
    try { controller_.set_preferences(std::move(preferences)); local_error_.clear(); }
    catch (const std::exception& error) { local_error_ = std::string("No se pudo cambiar la preferencia: ") + error.what(); }
    refresh_state();
}

void MainFrame::play_point(Direction direction, int number) {
    try { controller_.play(direction, number); local_error_.clear(); }
    catch (const std::exception& error) { local_error_ = std::string("No se pudo iniciar el punto: ") + error.what(); }
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
    focus_transport();
}

void MainFrame::edit_sequence() {
    if (settings_open_) return;
    try {
        ModalInputScope scope(settings_open_);
        wxDialog dialog(this, wxID_ANY, "Preparar idiomas personalizados");
        auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(new wxStaticText(&dialog, wxID_ANY,
            "Los cambios se guardan para el siguiente punto.\nLa reproduccion actual no cambia."), 0, wxALL, FromDIP(16));
        layout->Add(new SequenceEditor(controller_, &dialog, [] {}), 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));
        auto* done = new wxButton(&dialog, wxID_OK, "Listo");
        done->SetMinSize(FromDIP(wxSize(100, 36)));
        layout->Add(done, 0, wxALIGN_RIGHT | wxALL, FromDIP(16));
        dialog.SetSizerAndFit(layout);
        dialog.CentreOnParent();
        dialog.ShowModal();
    } catch (const std::exception& error) { local_error_ = error.what(); }
    focus_transport();
}

void MainFrame::refresh_state() {
    const auto& preferences = controller_.config().preferences;
    const bool up = preferences.direction == Direction::up;
    up_->SetValue(up);
    down_->SetValue(!up);
    for (auto* button : {up_, down_}) {
        const auto colour = button->GetValue() ? blue : wxColour(224, 232, 240);
        if (button->GetBackgroundColour() != colour) {
            button->SetBackgroundColour(colour);
            button->SetForegroundColour(button->GetValue() ? *wxWHITE : ink);
            button->Refresh();
        }
    }
    if (up_points_->IsShown() != up || down_points_->IsShown() == up) {
        up_points_->Show(up);
        down_points_->Show(!up);
        Layout();
        scroll_->Layout();
        scroll_->FitInside();
        scroll_->Scroll(0, 0);
    }
    const auto mode = std::find(modes.begin(), modes.end(), preferences.mode);
    const int mode_index = static_cast<int>(std::distance(modes.begin(), mode));
    if (mode_->GetSelection() != mode_index) mode_->SetSelection(mode_index);
    const auto language = std::find(languages.begin(), languages.end(), preferences.simple_language);
    const int language_index = static_cast<int>(std::distance(languages.begin(), language));
    if (language_->GetSelection() != language_index) language_->SetSelection(language_index);
    language_->Enable(preferences.mode == Mode::simple);
    sequence_button_->Enable(preferences.mode == Mode::custom);
    set_label(prepare_, utf8("PREPARAR SIGUIENTE | " + controller_.selection_text()));

    const auto& active = controller_.active_plan();
    const auto& state = controller_.state();
    for (auto& controls : points_) {
        const bool highlighted = active && active->point_id == controls.id && state.status != PlaybackStatus::stopped;
        const auto badge_status = highlighted ? state.status : PlaybackStatus::stopped;
        const auto key = preferences.keyboard.point_keys[static_cast<std::size_t>(controls.number - 1)];
        if (controls.badge_key != key || controls.badge_status != badge_status) {
            controls.badge_key = key;
            controls.badge_status = badge_status;
            controls.name_button->SetBackgroundColour(highlighted ? wxColour(224, 241, 233) : *wxWHITE);
            controls.name_button->SetBitmap(key_badge(controls.name_button, key, badge_status));
        }
        const auto point = std::find_if(controller_.config().points.begin(), controller_.config().points.end(),
            [&controls](const Point& candidate) { return candidate.id == controls.id; });
        if (point == controller_.config().points.end()) continue;
        auto name = utf8(point->name);
        // Wrap long names at word boundaries without truncating the accessible name.
        const int width = std::max(FromDIP(130), controls.name_button->GetClientSize().x - FromDIP(100));
        wxString wrapped, current;
        for (const auto& word : wxSplit(name, ' ')) {
            const auto candidate = current.empty() ? word : current + " " + word;
            if (!current.empty() && controls.name_button->GetTextExtent(candidate).x > width) {
                if (!wrapped.empty()) wrapped += "\n";
                wrapped += current;
                current = word;
            } else current = candidate;
        }
        if (!wrapped.empty()) wrapped += "\n";
        wrapped += current;
        if (highlighted) wrapped += "\n" + utf8(controller_.status_text());
        if (controls.name_button->GetLabel() != wrapped) {
            controls.name_button->SetLabel(wrapped);
            const auto description = utf8(label(controls.direction) + " " + std::to_string(controls.number) +
                ": " + point->name + " | Numpad " + std::to_string(key));
            controls.name_button->SetName(description);
            controls.name_button->SetToolTip(description);
        }
    }
    set_label(playing_title_, utf8(controller_.playback_title()));
    set_label(playing_detail_, utf8(controller_.playback_detail()));
    set_label(status_, utf8(controller_.status_text()));
    const wxString pause_label = (preferences.keyboard.enter_pauses ? "ENTER  |  " : "") +
        wxString(state.status == PlaybackStatus::paused ? "Reanudar" : "Pausar");
    if (pause_->GetLabel() != pause_label) pause_->SetLabel(pause_label);
    const auto stop_label = utf8(std::to_string(preferences.keyboard.stop_key) + "  |  Detener");
    if (stop_->GetLabel() != stop_label) stop_->SetLabel(stop_label);
    const bool pausable = state.status == PlaybackStatus::playing || state.status == PlaybackStatus::paused;
    if (pause_->IsEnabled() != pausable) {
        // Do not let native disabling send focus into the volume slider at EOF.
        if (!pausable && pause_->HasFocus() && IsActive()) stop_->SetFocus();
        pause_->Enable(pausable);
    }
    const int volume = std::clamp(static_cast<int>(std::lround(preferences.volume * 100.0F)), 0, 100);
    if (!volume_->HasCapture() && volume_->GetValue() != volume) volume_->SetValue(volume);
    set_label(volume_label_, utf8(std::to_string(volume) + "%"));
    std::string feedback = local_error_;
    for (const auto& message : {state.message, controller_.warning()}) {
        if (!message.empty()) { if (!feedback.empty()) feedback += "\n"; feedback += message; }
    }
    if (feedback_->GetValue() != utf8(feedback)) feedback_->ChangeValue(utf8(feedback));
    if (feedback_->IsShown() != !feedback.empty()) { feedback_->Show(!feedback.empty()); Layout(); scroll_->FitInside(); }

    const bool editing = settings_open_ || KeyboardController::editing_focus();
    wxString keyboard;
    if (!IsActive()) keyboard = "SIN FOCO | Active esta ventana para usar el numpad";
    else if (editing) keyboard = "ATAJOS SUSPENDIDOS | Termine de editar y pulse Usar numpad";
    else if (!num_lock_on()) keyboard = "BLOQ NUM APAGADO | Activelo para usar las teclas numericas";
    else keyboard = "NUMPAD LISTO | El numero grande de cada boton es su tecla";
    const auto colour = IsActive() && !editing && num_lock_on() ? green : amber;
    if (keyboard_status_->GetForegroundColour() != colour) keyboard_status_->SetForegroundColour(colour);
    set_label(keyboard_status_, keyboard);
}

} // namespace audioguia
