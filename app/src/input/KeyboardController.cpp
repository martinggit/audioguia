#include "input/KeyboardController.h"
#include "app/AppController.h"
#include "ui/MainFrame.h"
#include "ui/SequenceEditor.h"
#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <windows.h>
#include <algorithm>

namespace audioguia {

bool KeyboardController::editing_focus() {
    for (auto* window = wxWindow::FindFocus(); window; window = window->GetParent()) {
        auto* text = dynamic_cast<wxTextCtrl*>(window);
        if ((text && text->IsEditable()) || dynamic_cast<wxChoice*>(window) ||
            dynamic_cast<wxSpinCtrl*>(window) || dynamic_cast<wxSpinCtrlDouble*>(window) ||
            dynamic_cast<wxSlider*>(window) || dynamic_cast<SequenceEditor*>(window)) {
            return true;
        }
        if (window->IsTopLevel()) break;
    }
    return false;
}

bool KeyboardController::handle_native_message(void* message) {
    const auto& native = *static_cast<const MSG*>(message);
    if (native.message != WM_KEYDOWN) return false;
    const auto foreground = ::GetAncestor(::GetForegroundWindow(), GA_ROOT);
    const bool main_active = foreground == reinterpret_cast<HWND>(main_.GetHandle());
    if (!main_active || main_.input_suspended() ||
        ::GetAncestor(native.hwnd, GA_ROOT) != foreground) return false;
    KeyPress key;
    switch (native.wParam) {
    case VK_DIVIDE: key.operation = NumpadOperator::divide; break;
    case VK_MULTIPLY: key.operation = NumpadOperator::multiply; break;
    case VK_ADD: key.operation = NumpadOperator::add; break;
    case VK_SUBTRACT: key.operation = NumpadOperator::subtract; break;
    default: break;
    }
    key.digit = native.wParam >= VK_NUMPAD0 && native.wParam <= VK_NUMPAD9
        ? static_cast<int>(native.wParam - VK_NUMPAD0) : -1;
    key.numpad_enter = native.wParam == VK_RETURN && (native.lParam & (1L << 24)) != 0;
    key.repeat = (native.lParam & (1L << 30)) != 0;
    key.num_lock = (::GetKeyState(VK_NUMLOCK) & 1) != 0;
    key.editing = editing_focus();
    key.modified = (::GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
                   (::GetKeyState(VK_MENU) & 0x8000) != 0 ||
                   (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    auto command = translate_key(controller_.config().preferences.keyboard, key);
    if (command.action == KeyboardAction::none) {
        // Consume recognized repeats too, so native focused buttons cannot retrigger.
        if (!key.repeat) return false;
        key.repeat = false;
        return translate_key(controller_.config().preferences.keyboard, key).action != KeyboardAction::none;
    }
    switch (command.action) {
    case KeyboardAction::point: controller_.play(controller_.config().preferences.direction, command.point); break;
    case KeyboardAction::stop: controller_.stop(); break;
    case KeyboardAction::pause: controller_.toggle_pause(); break;
    case KeyboardAction::direction:
    case KeyboardAction::mode:
    case KeyboardAction::volume_up:
    case KeyboardAction::volume_down: {
        auto preferences = controller_.config().preferences;
        if (command.action == KeyboardAction::direction) {
            preferences.direction = preferences.direction == Direction::up ? Direction::down : Direction::up;
        } else if (command.action == KeyboardAction::mode) {
            preferences.mode = preferences.mode == Mode::simple ? Mode::sequential
                : preferences.mode == Mode::sequential ? Mode::custom : Mode::simple;
        } else {
            const float delta = command.action == KeyboardAction::volume_up ? 0.05F : -0.05F;
            preferences.volume = std::clamp(preferences.volume + delta, 0.0F, 1.0F);
        }
        controller_.set_preferences(std::move(preferences));
        break;
    }
    case KeyboardAction::none: break;
    }
    return true;
}

} // namespace audioguia
