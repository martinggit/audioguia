#include "ui/CompactFrame.h"

#include "app/AppController.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/display.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace audioguia {
namespace {

wxString utf8(const std::string& text) {
    return wxString::FromUTF8(text.c_str());
}

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

} // namespace

CompactFrame::CompactFrame(AppController& controller, std::function<void()> on_full)
    : wxFrame(nullptr, wxID_ANY, "Tolkeyen | Vista compacta", wxDefaultPosition, wxDefaultSize,
              (wxDEFAULT_FRAME_STYLE & ~wxMAXIMIZE_BOX) |
                  (controller.config().preferences.always_on_top ? wxSTAY_ON_TOP : 0)),
      controller_(controller), on_full_(std::move(on_full)), position_timer_(this) {
    auto font = GetFont();
    font.SetPointSize(std::max(11, font.GetPointSize()));
    SetFont(font);
    SetBackgroundColour(*wxWHITE);
    auto* scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                         wxHSCROLL | wxVSCROLL | wxTAB_TRAVERSAL);
    scroll->SetBackgroundColour(*wxWHITE);
    scroll->SetScrollRate(FromDIP(10), FromDIP(10));
    auto* content = new wxBoxSizer(wxVERTICAL);
    auto* brand = new wxStaticText(scroll, wxID_ANY, "Tolkeyen Patagonia Turismo");
    brand->SetFont(font.Bold().Larger());
    brand->SetForegroundColour(wxColour(18, 83, 139));
    content->Add(brand, 0, wxBOTTOM, FromDIP(8));
    auto* prepare = new wxStaticText(scroll, wxID_ANY, "PREPARAR SIGUIENTE");
    prepare->SetForegroundColour(wxColour(18, 83, 139));
    content->Add(prepare, 0, wxBOTTOM, FromDIP(2));
    selection_ = line(scroll);
    content->Add(selection_, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    auto* now = new wxStaticText(scroll, wxID_ANY, wxString::FromUTF8(u8"EN REPRODUCCI\u00d3N"));
    now->SetFont(font.Bold());
    content->Add(now, 0, wxBOTTOM, FromDIP(2));
    playing_title_ = line(scroll, 28);
    playing_title_->SetFont(font.Bold().Larger());
    playing_detail_ = line(scroll, 25);
    status_ = line(scroll, 25);
    status_->SetFont(font.Bold());
    content->Add(playing_title_, 0, wxEXPAND);
    content->Add(playing_detail_, 0, wxEXPAND);
    content->Add(status_, 0, wxEXPAND);
    feedback_ = new wxTextCtrl(scroll, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_READONLY | wxTE_MULTILINE | wxBORDER_NONE);
    feedback_->SetBackgroundColour(*wxWHITE);
    feedback_->SetForegroundColour(wxColour(147, 44, 34));
    feedback_->SetMinSize(FromDIP(wxSize(0, 48)));
    feedback_->SetName("Errores de audio y guardado");
    content->Add(feedback_, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(3));

    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    pause_ = new wxButton(scroll, wxID_ANY, "Pausar");
    stop_ = new wxButton(scroll, wxID_ANY, "Detener");
    auto* full = new wxButton(scroll, wxID_ANY, "Vista completa");
    for (auto* button : {pause_, stop_, full}) {
        button->SetMinSize(FromDIP(wxSize(-1, 44)));
        actions->Add(button, button == full ? 2 : 1, wxRIGHT, FromDIP(6));
    }
    content->Add(actions, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    on_top_ = new wxCheckBox(scroll, wxID_ANY, "Siempre visible (encima de otras ventanas)");
    on_top_->SetMinSize(FromDIP(wxSize(-1, 44)));
    on_top_->SetValue(controller_.config().preferences.always_on_top);
    content->Add(on_top_, 0, wxEXPAND);
    focus_notice_ = line(scroll, 24);
    content->Add(focus_notice_, 0, wxEXPAND);
    auto* padding = new wxBoxSizer(wxVERTICAL);
    padding->Add(content, 1, wxEXPAND | wxALL, FromDIP(12));
    scroll->SetSizer(padding);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(scroll, 1, wxEXPAND);
    SetSizer(outer);

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
    full->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { return_to_full(); });
    on_top_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { change_on_top(on_top_->GetValue()); });

    restore_position();
    Layout();
    scroll->FitInside();
    Bind(wxEVT_MOVE, [this](wxMoveEvent& event) {
        if (IsShownOnScreen() && !IsIconized()) {
            position_dirty_ = true;
            position_timer_.StartOnce(650);
        }
        event.Skip();
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { save_position(); }, position_timer_.GetId());
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (event.CanVeto()) {
            event.Veto();
            return_to_full();
        } else {
            save_position();
            event.Skip();
        }
    });
    refresh_state();
}

void CompactFrame::restore_position() {
    const auto& preferences = controller_.config().preferences;
    const bool configured = preferences.compact_x != -1 || preferences.compact_y != -1;
    wxRect work = wxGetClientDisplayRect();
    double closest = std::numeric_limits<double>::max();
    for (unsigned int i = 0; i < wxDisplay::GetCount(); ++i) {
        const auto area = wxDisplay(i).GetClientArea();
        if (!configured) {
            if (i == 0 || wxDisplay(i).IsPrimary()) work = area;
            continue;
        }
        const double x = preferences.compact_x;
        const double y = preferences.compact_y;
        const double dx = x - std::clamp(x, static_cast<double>(area.x),
                                         static_cast<double>(area.x) + area.width - 1);
        const double dy = y - std::clamp(y, static_cast<double>(area.y),
                                         static_cast<double>(area.y) + area.height - 1);
        const double distance = dx * dx + dy * dy;
        if (distance < closest) {
            closest = distance;
            work = area;
        }
    }
    const auto desired = FromDIP(wxSize(480, 420));
    const auto minimum = FromDIP(wxSize(390, 330));
    const wxSize size(std::min(desired.x, std::max(1, work.width)),
                      std::min(desired.y, std::max(1, work.height)));
    SetMinSize(wxSize(std::min(minimum.x, size.x), std::min(minimum.y, size.y)));
    SetSize(size);
    const int x = configured ? std::clamp(preferences.compact_x, work.x, work.x + work.width - size.x)
                             : work.x + (work.width - size.x) / 2;
    const int y = configured ? std::clamp(preferences.compact_y, work.y, work.y + work.height - size.y)
                             : work.y + (work.height - size.y) / 2;
    SetPosition(wxPoint(x, y));
}

void CompactFrame::save_position() {
    position_timer_.Stop();
    if (!position_dirty_ || IsIconized()) return;
    position_dirty_ = false;
    const auto position = GetPosition();
    const auto& preferences = controller_.config().preferences;
    if (position.x == preferences.compact_x && position.y == preferences.compact_y) return;
    try {
        controller_.remember_compact_position(position.x, position.y);
    } catch (const std::exception& error) {
        local_error_ = std::string("No se pudo guardar la posicion: ") + error.what();
    }
}

void CompactFrame::return_to_full() {
    position_dirty_ = true;
    save_position();
    // App.cpp owns visibility and lifetime; closing this view is not application exit.
    if (on_full_) on_full_();
}

void CompactFrame::change_on_top(bool enabled) {
    const bool previous = controller_.config().preferences.always_on_top;
    try {
        auto preferences = controller_.config().preferences;
        preferences.always_on_top = enabled;
        controller_.set_preferences(std::move(preferences));
#ifdef _WIN32
        if (!::SetWindowPos(reinterpret_cast<HWND>(GetHandle()), enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
                            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
            auto rollback = controller_.config().preferences;
            rollback.always_on_top = previous;
            controller_.set_preferences(std::move(rollback));
            throw AppError("Windows no pudo cambiar Siempre visible.");
        }
#else
        const auto style = GetWindowStyleFlag();
        SetWindowStyleFlag(enabled ? style | wxSTAY_ON_TOP : style & ~wxSTAY_ON_TOP);
#endif
        local_error_.clear();
    } catch (const std::exception& error) {
        local_error_ = std::string("Siempre visible: ") + error.what();
    }
    refresh_state();
}

void CompactFrame::refresh_state() {
    set_label(selection_, utf8(controller_.selection_text()));
    set_label(playing_title_, utf8(controller_.playback_title()));
    set_label(playing_detail_, utf8(controller_.playback_detail()));
    set_label(status_, utf8(controller_.status_text()));
    const auto& state = controller_.state();
    const wxString pause_label = state.status == PlaybackStatus::paused ? "Reanudar" : "Pausar";
    if (pause_->GetLabel() != pause_label) pause_->SetLabel(pause_label);
    const bool pausable = state.status == PlaybackStatus::playing || state.status == PlaybackStatus::paused;
    if (pause_->IsEnabled() != pausable) pause_->Enable(pausable);
    const bool on_top = controller_.config().preferences.always_on_top;
    if (on_top_->GetValue() != on_top) on_top_->SetValue(on_top);
    std::string feedback = local_error_;
    for (const auto& message : {state.message, controller_.warning()}) {
        if (!message.empty()) {
            if (!feedback.empty()) feedback += "\n";
            feedback += message;
        }
    }
    if (feedback_->GetValue() != utf8(feedback)) feedback_->ChangeValue(utf8(feedback));
    set_label(focus_notice_, IsActive()
        ? "Teclado: mantenga esta ventana en foco."
        : "Sin foco: vuelva a la ventana para usar el teclado.");
}

} // namespace audioguia
