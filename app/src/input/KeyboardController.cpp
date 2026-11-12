#include "input/KeyboardController.h"
#include "app/AppController.h"
#include "ui/MainFrame.h"
#include <windows.h>
#include <algorithm>

namespace audioguia {

bool KeyboardController::handle_native_message(void* message) {
    const auto& native = *static_cast<const MSG*>(message);
    static const UINT dedicated_message = ::RegisterWindowMessageW(L"TolkeyenAudioguia.DedicatedKey.v1");
    if (dedicated_message && native.message == dedicated_message &&
        native.hwnd == reinterpret_cast<HWND>(main_.GetHandle())) {
        if (main_.accepts_dedicated_session(native.wParam) && native.lParam >= 0 && native.lParam <= 511 &&
            !main_.input_suspended() && main_.IsEnabled()) {
            try {
                execute(translate_dedicated_scan(controller_.config().preferences.keyboard,
                    static_cast<int>(native.lParam), controller_.config().preferences.mode));
            } catch (const std::exception& error) {
                main_.report_input_error(error.what());
            }
        }
        return true;
    }
    // Ordinary keyboard input is reserved for editing/native navigation.
    // Playback shortcuts are accepted only from the owned dedicated bridge.
    return false;
}

void KeyboardController::execute(const KeyboardCommand& command) {
    switch (command.action) {
    case KeyboardAction::point: controller_.play(controller_.config().preferences.direction, command.point); break;
    case KeyboardAction::stop: controller_.stop(); break;
    case KeyboardAction::pause: controller_.toggle_pause(); break;
    case KeyboardAction::direction:
    case KeyboardAction::mode:
    case KeyboardAction::language:
    case KeyboardAction::volume_up:
    case KeyboardAction::volume_down: {
        auto preferences = controller_.config().preferences;
        if (command.action == KeyboardAction::direction) {
            preferences.direction = preferences.direction == Direction::up ? Direction::down : Direction::up;
        } else if (command.action == KeyboardAction::mode) {
            preferences.mode = preferences.mode == Mode::simple ? Mode::sequential
                : preferences.mode == Mode::sequential ? Mode::custom : Mode::simple;
        } else if (command.action == KeyboardAction::language) {
            preferences.simple_language = preferences.simple_language == Language::es ? Language::en
                : preferences.simple_language == Language::en ? Language::pt : Language::es;
        } else {
            const float delta = command.action == KeyboardAction::volume_up ? 0.05F : -0.05F;
            preferences.volume = std::clamp(preferences.volume + delta, 0.0F, 1.0F);
        }
        controller_.set_preferences(std::move(preferences));
        break;
    }
    case KeyboardAction::none: break;
    }
}

} // namespace audioguia
