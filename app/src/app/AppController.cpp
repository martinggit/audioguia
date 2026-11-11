#include "app/AppController.h"
#include "domain/PlaybackPlanner.h"
#include <algorithm>

namespace audioguia {

AppController::AppController(std::filesystem::path root, std::unique_ptr<IAudioPlayer> player)
    : repository_(std::move(root)), player_(std::move(player)) {
    try {
        config_ = repository_.load();
    } catch (const std::exception& error) {
        config_ = default_config();
        config_loaded_ = false;
        warning_ = "Configuracion invalida. Se usan valores de ejemplo SOLO en memoria. "
                   "Corrija app/config/audioguia.json y reinicie. " + std::string(error.what());
    }
}

void AppController::play(Direction direction, int number) {
    if (number < 1 || number > (direction == Direction::up ? 8 : 6)) return;
    player_->stop();
    ++session_;
    active_.reset();
    state_ = {session_, PlaybackStatus::preparing, 0, false, {}};
    config_.preferences.direction = direction;
    try {
        active_ = PlaybackPlanner::build(config_, direction, number, repository_.root());
        player_->play(session_, *active_, config_.preferences.output_device, config_.preferences.volume);
    } catch (const std::exception& error) {
        state_.status = PlaybackStatus::error;
        state_.message = error.what();
    }
    save();
}

void AppController::stop() {
    ++session_;
    player_->stop();
    active_.reset();
    state_ = {session_, PlaybackStatus::stopped, 0, false, {}};
}

void AppController::toggle_pause() {
    if (state_.status == PlaybackStatus::playing || state_.status == PlaybackStatus::paused) {
        player_->toggle_pause();
    }
}

void AppController::poll() {
    for (auto& event : player_->poll_events()) {
        if (event.session == session_) state_ = std::move(event);
    }
}

void AppController::apply_config(AppConfig config) {
    validate_config(config);
    if (config.preferences.output_device != config_.preferences.output_device) stop();
    player_->set_volume(config.preferences.volume);
    config_ = std::move(config);
    save();
}

void AppController::set_preferences(Preferences preferences) {
    auto updated = config_;
    updated.preferences = std::move(preferences);
    apply_config(std::move(updated));
}

std::vector<OutputDevice> AppController::devices() { return player_->devices(); }

void AppController::save() {
    if (!config_loaded_) return;
    try {
        repository_.save(config_);
        warning_.clear();
    } catch (const std::exception& error) {
        warning_ = "No se pudo guardar. Los cambios siguen activos solo en esta sesion: " + std::string(error.what());
    }
}

std::string AppController::selection_text() const {
    const auto& preferences = config_.preferences;
    auto text = label(preferences.direction) + " | " + label(preferences.mode) + " | ";
    const auto languages = preferences.mode == Mode::simple
        ? std::vector<Language>{preferences.simple_language}
        : preferences.mode == Mode::sequential
            ? std::vector<Language>{Language::es, Language::en, Language::pt}
            : preferences.custom_languages;
    for (std::size_t i = 0; i < languages.size(); ++i) {
        if (i) text += " > ";
        text += short_label(languages[i]);
    }
    return text;
}

std::string AppController::playback_title() const {
    return active_ ? active_->point_name : "Seleccione un punto del recorrido";
}

std::string AppController::playback_detail() const {
    if (!active_ || active_->tracks.empty()) return "Sin reproduccion automatica";
    const auto index = std::min(state_.track_index, active_->tracks.size() - 1);
    auto text = label(active_->direction) + " | " + label(active_->mode) + " | ";
    const auto next = state_.next_track_index.value_or(index + 1);
    if (state_.between_languages && next < active_->tracks.size()) {
        text += "Entre idiomas. Siguiente: " + label(active_->tracks[next].language);
        text += " | " + std::to_string(next + 1);
    } else {
        text += label(active_->tracks[index].language) + " | " + std::to_string(index + 1);
    }
    return text + " de " + std::to_string(active_->tracks.size());
}

std::string AppController::status_text() const {
    switch (state_.status) {
    case PlaybackStatus::stopped: return "DETENIDO";
    case PlaybackStatus::preparing: return "VALIDANDO AUDIOS";
    case PlaybackStatus::playing: return state_.between_languages ? "PAUSA ENTRE IDIOMAS" : "REPRODUCIENDO";
    case PlaybackStatus::paused: return "PAUSADO";
    case PlaybackStatus::error: return "ERROR";
    }
    return "ERROR";
}

} // namespace audioguia
