#include "ui/PreferencesDialog.h"

#include "app/AppController.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/display.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/thread.h>
#include <wx/utils.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

namespace audioguia {

struct AudioValidation {
    struct Result {
        std::string message{"Comprobando..."};
        bool valid{false};
        bool warning{false};
    };
    std::mutex mutex;
    std::atomic_bool cancelled{false};
    std::vector<std::array<Result, 3>> results;
    std::size_t completed{0};
    bool done{false};
};

namespace {

namespace fs = std::filesystem;
constexpr std::array<Language, 3> languages{Language::es, Language::en, Language::pt};
constexpr std::array<const char*, 3> language_folders{"0001-ESP", "0002-ENG", "0003-PORT"};

wxString utf8(const std::string& text) {
    return wxString::FromUTF8(text.c_str());
}

void change_text(wxTextCtrl* control, const wxString& text) {
    if (control->GetValue() != text) control->ChangeValue(text);
}

wxTextCtrl* read_only(wxWindow* parent, int height, bool multiline = false) {
    auto* control = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxDefaultSize, wxTE_READONLY | (multiline ? wxTE_MULTILINE : 0));
    control->SetMinSize(parent->FromDIP(wxSize(0, height)));
    return control;
}

bool equal_component(const fs::path& left, const fs::path& right) {
#ifdef _WIN32
    return utf8(left.u8string()).CmpNoCase(utf8(right.u8string())) == 0;
#else
    return left == right;
#endif
}

std::optional<fs::path> relative_inside(const fs::path& path, const fs::path& directory) {
    const auto normalized_path = path.lexically_normal();
    const auto normalized_directory = directory.lexically_normal();
    auto component = normalized_path.begin();
    for (const auto& base_component : normalized_directory) {
        if (component == normalized_path.end() || !equal_component(*component, base_component)) return std::nullopt;
        ++component;
    }
    fs::path relative;
    for (; component != normalized_path.end(); ++component) relative /= *component;
    if (relative.empty() || relative.is_absolute()) return std::nullopt;
    for (const auto& part : relative) {
        if (part == ".." || part == ".") return std::nullopt;
    }
    return relative;
}

AudioValidation::Result check_audio(const fs::path& root, const fs::path& audio_root,
                                    const std::string& root_error, const fs::path& relative,
                                    std::size_t language) {
    if (relative.empty()) return {"SIN ASOCIAR", false, false};
    const auto text = relative.generic_u8string();
    if (!root_error.empty()) return {"NO ACCESIBLE | " + text + " | " + root_error, false, false};
    const auto absolute = root / relative;
    std::error_code error;
    const auto status = fs::status(absolute, error);
    if (error == std::errc::no_such_file_or_directory || (!error && !fs::exists(status))) {
        return {"NO EXISTE | " + text, false, false};
    }
    if (error) return {"NO ACCESIBLE | " + text + " | " + error.message(), false, false};
    if (!fs::is_regular_file(status)) return {"NO ES UN ARCHIVO | " + text, false, false};
    const auto actual = fs::canonical(absolute, error);
    if (error) return {"NO ACCESIBLE | " + text + " | " + error.message(), false, false};
    if (!relative_inside(actual, audio_root)) {
        return {"FUERA DE audio/ (enlace o ruta) | " + text, false, false};
    }
    const auto expected = fs::path("audio") / language_folders[language];
    if (!relative_inside(relative, expected)) {
        return {"REVISAR CARPETA | " + text + " | Asociado explicitamente a " + label(languages[language]) +
                "; carpeta habitual: " + expected.generic_u8string(), true, true};
    }
    return {"OK | " + text, true, false};
}

// A self-owned wx worker only touches copied data and its shared mailbox, never a window.
// Closing the dialog cancels future checks without waiting for filesystem I/O on the UI thread.
class AudioCheckThread final : public wxThread {
public:
    AudioCheckThread(AppConfig config, fs::path root, std::shared_ptr<AudioValidation> result)
        : wxThread(wxTHREAD_DETACHED), config_(std::move(config)), root_(std::move(root)),
          result_(std::move(result)) {}

private:
    ExitCode Entry() override {
        std::error_code error;
        const auto audio_root = fs::canonical(root_ / "audio", error);
        const std::string root_error = error ? "No se pudo acceder a audio/: " + error.message() : std::string{};
        for (std::size_t point = 0; point < config_.points.size(); ++point) {
            for (std::size_t language = 0; language < languages.size(); ++language) {
                if (result_->cancelled.load(std::memory_order_relaxed)) return nullptr;
                AudioValidation::Result checked;
                try {
                    const auto found = config_.points[point].audio.find(languages[language]);
                    checked = check_audio(root_, audio_root, root_error,
                        found == config_.points[point].audio.end() ? fs::path{} : found->second, language);
                } catch (const std::exception& exception) {
                    checked = {std::string("NO SE PUDO COMPROBAR | ") + exception.what(), false, false};
                }
                std::lock_guard<std::mutex> lock(result_->mutex);
                result_->results[point][language] = std::move(checked);
                ++result_->completed;
            }
        }
        std::lock_guard<std::mutex> lock(result_->mutex);
        result_->done = true;
        return nullptr;
    }

    AppConfig config_;
    fs::path root_;
    std::shared_ptr<AudioValidation> result_;
};

} // namespace

PreferencesDialog::PreferencesDialog(AppController& controller, wxWindow* parent)
    : wxDialog(parent, wxID_ANY, wxString::FromUTF8(u8"Configurar audios y salida | Tolkeyen"),
                wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      controller_(controller), draft_(controller.config()), root_(fs::absolute(controller.root()).lexically_normal()),
      validation_timer_(this) {
    auto font = GetFont();
    font.SetPointSize(std::max(11, font.GetPointSize()));
    SetFont(font);
    edited_paths_.resize(draft_.points.size());
    checked_paths_.resize(draft_.points.size());
    check_messages_.resize(draft_.points.size());
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                         wxHSCROLL | wxVSCROLL | wxTAB_TRAVERSAL);
    scroll->SetScrollRate(FromDIP(12), FromDIP(12));
    auto* content = new wxBoxSizer(wxVERTICAL);
    auto* heading = new wxStaticText(scroll, wxID_ANY, "Audios y salida");
    heading->SetFont(font.Bold().Larger());
    heading->SetForegroundColour(wxColour(18, 83, 139));
    content->Add(heading, 0, wxBOTTOM, FromDIP(8));
    content->Add(new wxStaticText(scroll, wxID_ANY, wxString::FromUTF8(u8"Configuraci\u00f3n real (JSON)")),
                 0, wxBOTTOM, FromDIP(3));
    auto* config_path = read_only(scroll, 44);
    config_path->ChangeValue(utf8((root_ / "app" / "config" / "audioguia.json").u8string()));
    config_path->SetName(wxString::FromUTF8(u8"Ruta real de configuraci\u00f3n"));
    content->Add(config_path, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    content->Add(new wxStaticText(scroll, wxID_ANY, "Carpeta real de audios"), 0, wxBOTTOM, FromDIP(3));
    auto* audio_path = read_only(scroll, 44);
    audio_path->ChangeValue(utf8((root_ / "audio").u8string()));
    audio_path->SetName("Ruta real de audio");
    content->Add(audio_path, 0, wxEXPAND | wxBOTTOM, FromDIP(12));

    auto* output_label = new wxStaticText(scroll, wxID_ANY, "Dispositivo de salida");
    output_label->SetFont(font.Bold());
    content->Add(output_label, 0, wxBOTTOM, FromDIP(4));
    auto* devices_row = new wxBoxSizer(wxHORIZONTAL);
    output_ = new wxChoice(scroll, wxID_ANY);
    output_->SetMinSize(FromDIP(wxSize(300, 44)));
    output_->SetName("Dispositivo de salida (ID estable)");
    output_->Append("Predeterminado del sistema (al comenzar)");
    device_ids_.push_back({});
    if (!draft_.preferences.output_device.empty()) {
        output_->Append("Dispositivo guardado (pendiente de comprobar)");
        device_ids_.push_back(draft_.preferences.output_device);
        output_->SetSelection(1);
    } else {
        output_->SetSelection(0);
    }
    devices_row->Add(output_, 1, wxRIGHT, FromDIP(8));
    auto* refresh = new wxButton(scroll, wxID_ANY, "Refrescar dispositivos");
    refresh->SetMinSize(FromDIP(wxSize(-1, 44)));
    devices_row->Add(refresh, 0);
    content->Add(devices_row, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    auto* output_help = new wxStaticText(scroll, wxID_ANY, wxString::FromUTF8(
        u8"Cambiar la salida al aplicar detiene el audio. Predeterminado guarda un ID vac\u00edo.\n"
        u8"El dispositivo predeterminado se resuelve al comenzar la reproducci\u00f3n."));
    content->Add(output_help, 0, wxBOTTOM, FromDIP(4));
    device_message_ = read_only(scroll, 44, true);
    device_message_->SetName("Estado de los dispositivos de salida");
    content->Add(device_message_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    auto* gap_row = new wxBoxSizer(wxHORIZONTAL);
    gap_row->Add(new wxStaticText(scroll, wxID_ANY, "Pausa entre idiomas (segundos, 0 a 60)"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    gap_ = new wxSpinCtrlDouble(scroll, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, 0.0, 60.0, draft_.preferences.gap_ms / 1000.0, 0.1);
    gap_->SetDigits(3);
    gap_->SetMinSize(FromDIP(wxSize(130, 44)));
    gap_->SetName("Pausa entre idiomas en segundos");
    gap_row->Add(gap_, 0);
    content->Add(gap_row, 0, wxEXPAND | wxBOTTOM, FromDIP(14));

    auto* associations = new wxStaticText(scroll, wxID_ANY, "Asociaciones por punto e idioma");
    associations->SetFont(font.Bold());
    content->Add(associations, 0, wxBOTTOM, FromDIP(4));
    auto* association_help = new wxStaticText(scroll, wxID_ANY, wxString::FromUTF8(
        u8"Seleccione una fila y un idioma para asociar un archivo. La columna define el idioma;\n"
        u8"nunca se deduce del nombre. Se comprueban ubicaci\u00f3n y existencia, sin decodificar audios."));
    content->Add(association_help, 0, wxBOTTOM, FromDIP(6));
    table_ = new wxListCtrl(scroll, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    table_->SetMinSize(FromDIP(wxSize(720, 220)));
    table_->SetName("Tabla de asociaciones de los catorce puntos en tres idiomas");
    table_->InsertColumn(0, "Punto", wxLIST_FORMAT_LEFT, FromDIP(300));
    for (std::size_t i = 0; i < languages.size(); ++i) {
        table_->InsertColumn(static_cast<long>(i + 1), utf8(label(languages[i])), wxLIST_FORMAT_LEFT, FromDIP(190));
    }
    point_ = new wxChoice(scroll, wxID_ANY);
    point_->SetMinSize(FromDIP(wxSize(300, 44)));
    point_->SetName("Punto cuya ruta se edita");
    for (std::size_t i = 0; i < draft_.points.size(); ++i) {
        const auto& point = draft_.points[i];
        const auto name = utf8(label(point.direction) + " " + std::to_string(point.number) + " | " + point.name);
        table_->InsertItem(static_cast<long>(i), name);
        point_->Append(name);
    }
    content->Add(table_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    summary_ = new wxStaticText(scroll, wxID_ANY, "Comprobando asociaciones...", wxDefaultPosition,
                                wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    summary_->SetMinSize(FromDIP(wxSize(0, 25)));
    content->Add(summary_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    auto* association_selectors = new wxBoxSizer(wxHORIZONTAL);
    auto* point_group = new wxBoxSizer(wxVERTICAL);
    point_group->Add(new wxStaticText(scroll, wxID_ANY, "Punto"), 0, wxBOTTOM, FromDIP(3));
    point_group->Add(point_, 0, wxEXPAND);
    association_selectors->Add(point_group, 3, wxEXPAND | wxRIGHT, FromDIP(12));
    auto* language_group = new wxBoxSizer(wxVERTICAL);
    language_group->Add(new wxStaticText(scroll, wxID_ANY, "Idioma de esta columna"), 0, wxBOTTOM, FromDIP(3));
    language_ = new wxChoice(scroll, wxID_ANY);
    language_->SetMinSize(FromDIP(wxSize(200, 44)));
    language_->SetName("Idioma al que se asocia el archivo");
    for (const auto language : languages) language_->Append(utf8(label(language)));
    language_->SetSelection(0);
    language_group->Add(language_, 0, wxEXPAND);
    association_selectors->Add(language_group, 2, wxEXPAND);
    content->Add(association_selectors, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    content->Add(new wxStaticText(scroll, wxID_ANY, "Ruta relativa guardada en el mapa JSON"),
                 0, wxBOTTOM, FromDIP(3));
    auto* file_row = new wxBoxSizer(wxHORIZONTAL);
    association_path_ = read_only(scroll, 44);
    association_path_->SetName("Ruta relativa de la asociacion seleccionada");
    file_row->Add(association_path_, 1, wxRIGHT, FromDIP(8));
    auto* browse = new wxButton(scroll, wxID_ANY, "Buscar archivo...");
    auto* clear = new wxButton(scroll, wxID_ANY, wxString::FromUTF8(u8"Quitar asociaci\u00f3n"));
    for (auto* button : {browse, clear}) {
        button->SetMinSize(FromDIP(wxSize(-1, 44)));
        file_row->Add(button, 0, wxRIGHT, FromDIP(6));
    }
    content->Add(file_row, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    association_status_ = read_only(scroll, 52, true);
    association_status_->SetName("Comprobacion de ruta e idioma seleccionados");
    content->Add(association_status_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    auto* external_help = new wxStaticText(scroll, wxID_ANY, wxString::FromUTF8(
        u8"Solo se seleccionan archivos existentes dentro de audio/. Esta ventana no copia ni sobrescribe.\n"
        u8"Si el archivo es externo, col\u00f3quelo primero en la carpeta del idioma con un nombre nuevo:\n"
        u8"Espa\u00f1ol: audio/0001-ESP | Ingl\u00e9s: audio/0002-ENG | Portugu\u00e9s: audio/0003-PORT.\n"
        u8"Las rutas ya configuradas se conservan; no se renombran ni se sustituyen archivos ausentes."));
    content->Add(external_help, 0, wxBOTTOM, FromDIP(10));
    auto* padding = new wxBoxSizer(wxVERTICAL);
    padding->Add(content, 1, wxEXPAND | wxALL, FromDIP(12));
    scroll->SetSizer(padding);
    outer->Add(scroll, 1, wxEXPAND);

    auto* bottom = new wxBoxSizer(wxVERTICAL);
    message_ = read_only(this, 52, true);
    message_->SetName("Resultado de aplicar la configuracion");
    bottom->Add(message_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    message_->Hide();
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    auto* cancel = new wxButton(this, wxID_CANCEL, "Cancelar");
    apply_button_ = new wxButton(this, wxID_OK, "Aplicar y cerrar");
    for (auto* button : {cancel, apply_button_}) {
        button->SetMinSize(FromDIP(wxSize(150, 44)));
        buttons->Add(button, 0, wxLEFT, FromDIP(8));
    }
    bottom->Add(buttons, 0, wxEXPAND);
    outer->Add(bottom, 0, wxEXPAND | wxALL, FromDIP(12));
    SetSizer(outer);
    SetEscapeId(wxID_CANCEL);
    apply_button_->SetDefault();
    apply_button_->Enable(false);

    refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_devices(); });
    output_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const auto index = output_->GetSelection();
        if (index != wxNOT_FOUND && static_cast<std::size_t>(index) < device_ids_.size()) {
            output_->SetToolTip(utf8("ID estable: " + device_ids_[static_cast<std::size_t>(index)]));
        }
    });
    point_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const int index = point_->GetSelection();
        if (index != wxNOT_FOUND) select_point(static_cast<std::size_t>(index));
    });
    language_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { load_association(); });
    table_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
        if (syncing_selection_ || event.GetIndex() < 0) return;
        const auto index = static_cast<std::size_t>(event.GetIndex());
        if (index >= draft_.points.size()) return;
        if (point_->GetSelection() != static_cast<int>(index)) point_->SetSelection(static_cast<int>(index));
        load_association();
    });
    browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { browse_audio(); });
    clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { clear_association(); });
    apply_button_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { apply(); });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { refresh_validation(); }, validation_timer_.GetId());

    wxRect work = wxGetClientDisplayRect();
    const int display = parent ? wxDisplay::GetFromWindow(parent) : wxNOT_FOUND;
    if (display != wxNOT_FOUND) work = wxDisplay(static_cast<unsigned int>(display)).GetClientArea();
    else if (wxDisplay::GetCount() != 0) work = wxDisplay(0u).GetClientArea();
    const auto desired = FromDIP(wxSize(960, 700));
    const auto minimum = FromDIP(wxSize(680, 450));
    const int margin = FromDIP(12);
    const wxSize size(std::min(desired.x, std::max(1, work.width - margin * 2)),
                      std::min(desired.y, std::max(1, work.height - margin * 2)));
    SetMinSize(wxSize(std::min(minimum.x, size.x), std::min(minimum.y, size.y)));
    SetSize(size);
    SetPosition(wxPoint(work.x + (work.width - size.x) / 2, work.y + (work.height - size.y) / 2));
    Layout();
    scroll->FitInside();
    if (!draft_.points.empty()) select_point(0);
    refresh_devices();
    start_validation();
}

PreferencesDialog::~PreferencesDialog() {
    validation_timer_.Stop();
    if (validation_) validation_->cancelled.store(true, std::memory_order_relaxed);
}

void PreferencesDialog::set_message(const wxString& message) {
    change_text(message_, message);
    if (message_->IsShown() != !message.empty()) {
        message_->Show(!message.empty());
        Layout();
    }
}

void PreferencesDialog::refresh_devices() {
    const auto selected = output_->GetSelection();
    const auto selected_id = selected != wxNOT_FOUND && static_cast<std::size_t>(selected) < device_ids_.size()
        ? device_ids_[static_cast<std::size_t>(selected)] : draft_.preferences.output_device;
    try {
        const auto devices = controller_.devices();
        output_->Freeze();
        output_->Clear();
        device_ids_.clear();
        output_->Append("Predeterminado del sistema (al comenzar)");
        device_ids_.push_back({});
        int selection = 0;
        for (const auto& device : devices) {
            if (device.id.empty() || std::find(device_ids_.begin(), device_ids_.end(), device.id) != device_ids_.end()) continue;
            output_->Append(utf8(device.name.empty() ? device.id : device.name));
            device_ids_.push_back(device.id);
            if (device.id == selected_id) selection = static_cast<int>(device_ids_.size() - 1);
        }
        const bool missing = !selected_id.empty() && selection == 0;
        if (missing) {
            output_->Append("No disponible: conservar el ID guardado");
            device_ids_.push_back(selected_id);
            selection = static_cast<int>(device_ids_.size() - 1);
        }
        output_->SetSelection(selection);
        output_->SetToolTip(utf8("ID estable: " + selected_id));
        output_->Thaw();
        change_text(device_message_, missing
            ? wxString::FromUTF8(u8"El ID seleccionado no est\u00e1 disponible. Se conserva hasta que elija otra salida. Use Refrescar para reintentar.")
            : utf8(std::to_string(device_ids_.size() - 1) + " dispositivos. Se guarda el ID, no el indice ni el nombre."));
    } catch (const std::exception& error) {
        change_text(device_message_, utf8(std::string("No se pudieron enumerar dispositivos: ") + error.what() +
            ". La seleccion anterior se conserva; puede usar Predeterminado o reintentar."));
    }
}

void PreferencesDialog::select_point(std::size_t index) {
    if (index >= draft_.points.size()) return;
    if (point_->GetSelection() != static_cast<int>(index)) point_->SetSelection(static_cast<int>(index));
    syncing_selection_ = true;
    table_->SetItemState(-1, 0, wxLIST_STATE_SELECTED);
    table_->SetItemState(static_cast<long>(index), wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    table_->EnsureVisible(static_cast<long>(index));
    syncing_selection_ = false;
    load_association();
}

void PreferencesDialog::load_association() {
    const int point_index = point_->GetSelection();
    const int language_index = language_->GetSelection();
    if (point_index == wxNOT_FOUND || language_index == wxNOT_FOUND ||
        static_cast<std::size_t>(point_index) >= draft_.points.size() ||
        static_cast<std::size_t>(language_index) >= languages.size()) return;
    const auto p = static_cast<std::size_t>(point_index);
    const auto l = static_cast<std::size_t>(language_index);
    const auto& audio = draft_.points[p].audio;
    const auto found = audio.find(languages[l]);
    change_text(association_path_, found == audio.end() ? wxString{} : utf8(found->second.generic_u8string()));
    change_text(association_status_, utf8(check_messages_[p][l]));
}

void PreferencesDialog::browse_audio() {
    const int point_index = point_->GetSelection();
    const int language_index = language_->GetSelection();
    if (point_index == wxNOT_FOUND || language_index == wxNOT_FOUND) return;
    const auto p = static_cast<std::size_t>(point_index);
    const auto l = static_cast<std::size_t>(language_index);
    if (p >= draft_.points.size() || l >= languages.size()) return;
    const auto& point = draft_.points[p];
    auto initial_directory = root_ / "audio" / language_folders[l];
    wxString initial_name;
    const auto found = point.audio.find(languages[l]);
    if (found != point.audio.end() && !found->second.empty()) {
        initial_directory = (root_ / found->second).parent_path();
        initial_name = utf8(found->second.filename().u8string());
    }
    wxFileDialog dialog(this, utf8("Asociar " + label(languages[l]) + " | " + label(point.direction) +
                                  " " + std::to_string(point.number) + " | " + point.name),
                        utf8(initial_directory.u8string()), initial_name,
                        "Audio MP3 o WAV (*.mp3;*.wav)|*.mp3;*.wav",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return;
    try {
        const auto bytes = dialog.GetPath().ToUTF8();
        const auto selected = fs::u8path(std::string(bytes.data(), bytes.length())).lexically_normal();
        const auto inside_audio = relative_inside(selected, root_ / "audio");
        if (!inside_audio) {
            set_message(wxString::FromUTF8(
                u8"Archivo externo: no se ha cambiado nada. Col\u00f3quelo con un nombre nuevo en ") +
                utf8((root_ / "audio" / language_folders[l]).u8string()) +
                wxString::FromUTF8(u8" y vuelva a Buscar archivo. No se copia ni se sobrescribe el original."));
            return;
        }
        const auto relative = fs::path("audio") / *inside_audio;
        auto updated = draft_;
        updated.points[p].audio[languages[l]] = relative;
        validate_config(updated);
        draft_ = std::move(updated);
        edited_paths_[p][l] = true;
        set_message(utf8("Pendiente de aplicar: " + label(languages[l]) + " | " + relative.generic_u8string()));
        start_validation();
    } catch (const std::exception& error) {
        set_message(utf8(std::string("No se pudo asociar el archivo: ") + error.what()));
    }
}

void PreferencesDialog::clear_association() {
    const int point_index = point_->GetSelection();
    const int language_index = language_->GetSelection();
    if (point_index == wxNOT_FOUND || language_index == wxNOT_FOUND) return;
    const auto p = static_cast<std::size_t>(point_index);
    const auto l = static_cast<std::size_t>(language_index);
    if (p >= draft_.points.size() || l >= languages.size()) return;
    draft_.points[p].audio.erase(languages[l]);
    edited_paths_[p][l] = true;
    set_message(wxString::FromUTF8(
        u8"Asociaci\u00f3n quitada solo del borrador. El archivo original no se elimina. Guarde con Aplicar o descarte con Cancelar."));
    start_validation();
}

void PreferencesDialog::start_validation() {
    if (validation_) validation_->cancelled.store(true, std::memory_order_relaxed);
    validation_ = std::make_shared<AudioValidation>();
    validation_->results.resize(draft_.points.size());
    validation_complete_ = false;
    apply_button_->Enable(false);
    auto worker = std::make_unique<AudioCheckThread>(draft_, root_, validation_);
    bool started = false;
    if (worker->Create() == wxTHREAD_NO_ERROR) {
        auto* running = worker.release();
        if (running->Run() == wxTHREAD_NO_ERROR) started = true;
        else delete running;
    }
    if (!started) {
        std::lock_guard<std::mutex> lock(validation_->mutex);
        for (auto& row : validation_->results) {
            for (auto& result : row) result.message = "No se pudo iniciar la comprobacion. Cierre y vuelva a abrir Configurar.";
        }
        validation_->done = true;
    }
    validation_timer_.Start(100);
    refresh_validation();
    load_association();
}

void PreferencesDialog::refresh_validation() {
    if (!validation_) return;
    std::vector<std::array<AudioValidation::Result, 3>> results;
    std::size_t completed = 0;
    bool done = false;
    {
        std::lock_guard<std::mutex> lock(validation_->mutex);
        results = validation_->results;
        completed = validation_->completed;
        done = validation_->done;
    }
    std::size_t problems = 0;
    std::size_t warnings = 0;
    for (std::size_t p = 0; p < results.size(); ++p) {
        for (std::size_t l = 0; l < languages.size(); ++l) {
            const auto& result = results[p][l];
            checked_paths_[p][l] = result.valid;
            if (!result.valid) ++problems;
            if (result.warning) ++warnings;
            if (check_messages_[p][l] != result.message) {
                check_messages_[p][l] = result.message;
                table_->SetItem(static_cast<long>(p), static_cast<int>(l + 1), utf8(result.message));
            }
        }
    }
    const auto total = results.size() * languages.size();
    const auto summary = done
        ? std::to_string(total) + " asociaciones | " + std::to_string(problems) +
            " sin asociar o no accesibles | " + std::to_string(warnings) + " carpetas a revisar."
        : "Comprobando archivos en segundo plano: " + std::to_string(completed) + " de " + std::to_string(total) + ".";
    if (summary_->GetLabel() != utf8(summary)) {
        summary_->SetLabel(utf8(summary));
        summary_->SetToolTip(utf8(summary));
    }
    // Only the read-only result changes; point/language selections and edits stay untouched.
    const int p = point_->GetSelection();
    const int l = language_->GetSelection();
    if (p != wxNOT_FOUND && l != wxNOT_FOUND && static_cast<std::size_t>(p) < results.size() &&
        static_cast<std::size_t>(l) < languages.size()) {
        change_text(association_status_, utf8(check_messages_[static_cast<std::size_t>(p)][static_cast<std::size_t>(l)]));
    }
    validation_complete_ = done;
    if (apply_button_->IsEnabled() != done) apply_button_->Enable(done);
    if (done) validation_timer_.Stop();
}

void PreferencesDialog::apply() {
    if (!validation_complete_) {
        set_message("Espere a que termine la comprobacion de archivos. Puede seguir editando mientras tanto.");
        return;
    }
    double seconds = 0;
    if (!gap_->GetTextValue().ToDouble(&seconds) || !std::isfinite(seconds) || seconds < 0 || seconds > 60) {
        set_message("La pausa entre idiomas debe ser un numero entre 0 y 60 segundos.");
        return;
    }
    const int output = output_->GetSelection();
    if (output == wxNOT_FOUND || static_cast<std::size_t>(output) >= device_ids_.size()) {
        set_message("Seleccione un dispositivo o Predeterminado antes de aplicar.");
        return;
    }
    for (std::size_t p = 0; p < draft_.points.size(); ++p) {
        for (std::size_t l = 0; l < languages.size(); ++l) {
            const auto found = draft_.points[p].audio.find(languages[l]);
            if (edited_paths_[p][l] && found != draft_.points[p].audio.end() && !found->second.empty() && !checked_paths_[p][l]) {
                select_point(p);
                language_->SetSelection(static_cast<int>(l));
                load_association();
                set_message(utf8("No se puede guardar la nueva asociacion: " + label(draft_.points[p].direction) +
                    " " + std::to_string(draft_.points[p].number) + " | " + label(languages[l]) + ". " + check_messages_[p][l]));
                return;
            }
        }
    }
    try {
        // Merge into current preferences so a modal never writes back a stale playback selection.
        auto updated = controller_.config();
        updated.points = draft_.points;
        updated.preferences.gap_ms = static_cast<int>(std::lround(seconds * 1000.0));
        updated.preferences.output_device = device_ids_[static_cast<std::size_t>(output)];
        controller_.apply_config(std::move(updated));
        draft_ = controller_.config();
        for (auto& row : edited_paths_) row.fill(false);
        if (!controller_.warning().empty()) {
            set_message(utf8("Cambios activos en memoria. " + controller_.warning()));
            return;
        }
        EndModal(wxID_OK);
    } catch (const std::exception& error) {
        set_message(utf8(std::string("No se pudo aplicar la configuracion: ") + error.what()));
    }
}

} // namespace audioguia
