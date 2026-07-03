#include "button.h"

#include "hal/common.h"
#include "pins_arduino.h"

void IRAM_ATTR button_isr() {
    Button &button = Button::getInstance();
    detachInterrupt(USER_BUTTON);
    button.initDebouncing(true);
    button.polling = true;
}

Button &Button::getInstance() {
    return instance;
}

void Button::begin() {
    pinMode(USER_BUTTON,
            USER_BUTTON_ACTIVE_LEVEL ? INPUT_PULLDOWN : INPUT_PULLUP);
    attach();
}

void Button::update() {
    if (!polling) {
        return;
    }

    // Cache current time - single millis() call
    const uint32_t currentMillis = millis();

    const bool buttonState = isButtonPressed();
    const uint32_t elapsedMillis = currentMillis - lastButtonChangeMillis;

    // Detect button release (pressed -> not pressed)
    if (!buttonState && lastButtonState) {
        if (elapsedMillis >= minDebounceTimeMs) {
            pressCount++;
            lastButtonChangeMillis = currentMillis;
        }
        lastButtonState = false;
        return;
    }

    // Detect button idle (not pressed for a while)
    if (!buttonState) {
        if (elapsedMillis < multiPressMaxDelaySeconds * 1e3) {
            return;
        }
        if (pressCount >= 1) {
            invokeMultiPressCallbacks(pressCount);
            pressCount = 0;
        }
        polling = false;
        attach();
        return;
    }

    // Detect long press (held down)
    if (elapsedMillis >= longPressSeconds * 1e3 && pressCount == 0) {
        invokeLongPressCallbacks();
        polling = false;
        attach();
    }

    lastButtonState = true;
}

void Button::initDebouncing(bool state) {
    circularBuffer = state ? 0xffff : 0x0000;
    lastButtonState = state;
    const uint32_t now = millis();
    lastButtonChangeMillis = now;
    lastSampleMillis = now;
    pressCount = 0;
}

bool Button::isButtonPressed() {
    circularBuffer = (circularBuffer << 1)
                     | (digitalRead(USER_BUTTON) == USER_BUTTON_ACTIVE_LEVEL);

    const uint8_t popCount = __builtin_popcount(circularBuffer);

    return popCount > 12;  // Require at least 13 out of 16 samples to be high
}

void Button::onLongPress(std::function<void()> callback) {
    longPressCallbacks.push_back(callback);
}

void Button::onMultiPress(std::function<void(size_t)> callback) {
    multiPressCallbacks.push_back(callback);
}

void Button::attach() {
    attachInterrupt(digitalPinToInterrupt(USER_BUTTON),
                    button_isr,
                    USER_BUTTON_ACTIVE_LEVEL ? RISING : FALLING);
}

void Button::invokeLongPressCallbacks() {
    for (const auto &callback : longPressCallbacks) {
        callback();
    }
}

void Button::invokeMultiPressCallbacks(size_t pressCount) {
    for (const auto &callback : multiPressCallbacks) {
        callback(pressCount);
    }
}

Button Button::instance;
