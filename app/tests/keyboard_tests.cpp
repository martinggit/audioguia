#include "input/KeyboardController.h"
#include <iostream>
#include <stdexcept>

using namespace audioguia;

int main() {
    int checks = 0;
    const auto check = [&checks](bool condition) {
        ++checks;
        if (!condition) throw std::runtime_error("keyboard check " + std::to_string(checks));
    };
    try {
        KeyboardBindings bindings;
        for (int digit = 1; digit <= 8; ++digit) {
            KeyPress key;
            key.digit = digit;
            const auto command = translate_key(bindings, key);
            check(command.action == KeyboardAction::point && command.point == digit);
            key.repeat = true;
            check(translate_key(bindings, key).action == KeyboardAction::none);
            key.repeat = false;
            key.editing = true;
            check(translate_key(bindings, key).action == KeyboardAction::none);
            key.editing = false;
            key.app_has_focus = false;
            check(translate_key(bindings, key).action == KeyboardAction::none);
            key.app_has_focus = true;
            key.num_lock = false;
            check(translate_key(bindings, key).action == KeyboardAction::none);
            key.num_lock = true;
            key.modified = true;
            check(translate_key(bindings, key).action == KeyboardAction::none);
        }
        KeyPress stop;
        stop.digit = 0;
        check(translate_key(bindings, stop).action == KeyboardAction::stop);
        KeyPress enter;
        enter.numpad_enter = true;
        check(translate_key(bindings, enter).action == KeyboardAction::pause);
        enter.num_lock = false;
        check(translate_key(bindings, enter).action == KeyboardAction::pause);
        enter.repeat = true;
        check(translate_key(bindings, enter).action == KeyboardAction::none);
        check(translate_key(bindings, KeyPress{}).action == KeyboardAction::none);
        bindings.point_keys[0] = 9;
        KeyPress remapped;
        remapped.digit = 9;
        check(translate_key(bindings, remapped).point == 1);
        bindings.enter_pauses = false;
        enter.repeat = false;
        check(translate_key(bindings, enter).action == KeyboardAction::none);
        std::cout << "keyboard_tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
