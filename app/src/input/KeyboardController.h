#pragma once
#include "domain/Models.h"

namespace audioguia {

class AppController;
class MainFrame;

enum class KeyboardAction { none, point, stop, pause, direction, mode, volume_up, volume_down };
enum class NumpadOperator { none, divide, multiply, add, subtract };
struct KeyboardCommand {
    KeyboardAction action{KeyboardAction::none};
    int point{0};
};
struct KeyPress {
    int digit{-1};
    bool numpad_enter{false};
    bool repeat{false};
    bool num_lock{true};
    bool editing{false};
    bool app_has_focus{true};
    bool modified{false};
    NumpadOperator operation{NumpadOperator::none};
};

inline KeyboardCommand translate_key(const KeyboardBindings& bindings, const KeyPress& key) {
    if (!key.app_has_focus || key.editing || key.modified || key.repeat) return {};
    switch (key.operation) {
    case NumpadOperator::divide: return {KeyboardAction::direction, 0};
    case NumpadOperator::multiply: return {KeyboardAction::mode, 0};
    case NumpadOperator::add: return {KeyboardAction::volume_up, 0};
    case NumpadOperator::subtract: return {KeyboardAction::volume_down, 0};
    case NumpadOperator::none: break;
    }
    if (key.numpad_enter && bindings.enter_pauses) return {KeyboardAction::pause, 0};
    if (!key.num_lock || key.digit < 0) return {};
    if (key.digit == bindings.stop_key) return {KeyboardAction::stop, 0};
    for (std::size_t i = 0; i < bindings.point_keys.size(); ++i) {
        if (key.digit == bindings.point_keys[i]) return {KeyboardAction::point, static_cast<int>(i + 1)};
    }
    return {};
}

class KeyboardController {
public:
    KeyboardController(AppController& controller, MainFrame& main)
        : controller_(controller), main_(main) {}
    bool handle_native_message(void* message);
    static bool editing_focus();
private:
    AppController& controller_;
    MainFrame& main_;
};

} // namespace audioguia
