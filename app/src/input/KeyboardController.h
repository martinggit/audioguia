#pragma once
#include "domain/Models.h"

namespace audioguia {

class AppController;
class MainFrame;

enum class KeyboardAction { none, point, stop, pause, direction, mode, volume_up, volume_down, language };
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
    bool numpad_decimal{false};
    Mode mode{Mode::simple};
};

inline KeyboardCommand translate_key(const KeyboardBindings& bindings, const KeyPress& key) {
    if (!key.app_has_focus || key.editing || key.modified || key.repeat) return {};
    if (key.numpad_decimal && key.num_lock && key.mode == Mode::simple)
        return {KeyboardAction::language, 0};
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

// Interception scan codes from the dedicated Genius. Num Lock and modifiers on
// the other keyboard must not change the meaning of these physical buttons.
inline KeyboardCommand translate_dedicated_scan(const KeyboardBindings& bindings, int scan, Mode mode) {
    KeyPress key;
    key.mode = mode;
    switch (scan) {
    case 82: key.digit = 0; break;
    case 79: key.digit = 1; break;
    case 80: key.digit = 2; break;
    case 81: key.digit = 3; break;
    case 75: key.digit = 4; break;
    case 76: key.digit = 5; break;
    case 77: key.digit = 6; break;
    case 71: key.digit = 7; break;
    case 72: key.digit = 8; break;
    case 73: key.digit = 9; break;
    case 284: key.numpad_enter = true; break;
    case 309: key.operation = NumpadOperator::divide; break;
    case 55: key.operation = NumpadOperator::multiply; break;
    case 74: key.operation = NumpadOperator::subtract; break;
    case 78: key.operation = NumpadOperator::add; break;
    case 83: key.numpad_decimal = true; break;
    default: return {};
    }
    return translate_key(bindings, key);
}

class KeyboardController {
public:
    KeyboardController(AppController& controller, MainFrame& main)
        : controller_(controller), main_(main) {}
    bool handle_native_message(void* message);
private:
    void execute(const KeyboardCommand& command);
    AppController& controller_;
    MainFrame& main_;
};

} // namespace audioguia
