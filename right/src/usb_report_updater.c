#include <math.h>
#include <stdlib.h>
#include "key_action.h"
#include "led_display.h"
#include "layer.h"
#include "usb_interfaces/usb_interface_mouse.h"
#include "keymap.h"
#include "peripherals/test_led.h"
#include "slave_drivers/is31fl3731_driver.h"
#include "slave_drivers/uhk_module_driver.h"
#include "macros.h"
#include "right_key_matrix.h"
#include "layer.h"
#include "usb_report_updater.h"
#include "timer.h"
#include "config_parser/parse_keymap.h"
#include "keyboard_state.h"
#include "usb_commands/usb_command_get_debug_buffer.h"
#include "arduino_hid/ConsumerAPI.h"
#include "arrays.h"
#include "keyboard_state.h"
#include "debug.h"

static uint32_t mouseUsbReportUpdateTime = 0;
static uint32_t mouseElapsedTime;

uint16_t DoubleTapSwitchLayerTimeout = 300;
static uint16_t DoubleTapSwitchLayerReleaseTimeout = 200;

static bool activeMouseStates[ACTIVE_MOUSE_STATES_COUNT];
bool TestUsbStack = false;

volatile uint8_t UsbReportUpdateSemaphore = 0;

mouse_kinetic_state_t MouseMoveState = {
    .isScroll = false,
    .upState = SerializedMouseAction_MoveUp,
    .downState = SerializedMouseAction_MoveDown,
    .leftState = SerializedMouseAction_MoveLeft,
    .rightState = SerializedMouseAction_MoveRight,
    .intMultiplier = 25,
    .initialSpeed = 5,
    .acceleration = 35,
    .deceleratedSpeed = 10,
    .baseSpeed = 40,
    .acceleratedSpeed = 80,
};

mouse_kinetic_state_t MouseScrollState = {
    .isScroll = true,
    .upState = SerializedMouseAction_ScrollDown,
    .downState = SerializedMouseAction_ScrollUp,
    .leftState = SerializedMouseAction_ScrollLeft,
    .rightState = SerializedMouseAction_ScrollRight,
    .intMultiplier = 1,
    .initialSpeed = 20,
    .acceleration = 20,
    .deceleratedSpeed = 10,
    .baseSpeed = 20,
    .acceleratedSpeed = 50,
};

static void applyKeyAction(key_state_t *keyState, key_action_t *action);

void addModifiersToReport(int flags);

static void processMouseKineticState(mouse_kinetic_state_t *kineticState)
{
    float initialSpeed = kineticState->intMultiplier * kineticState->initialSpeed;
    float acceleration = kineticState->intMultiplier * kineticState->acceleration;
    float deceleratedSpeed = kineticState->intMultiplier * kineticState->deceleratedSpeed;
    float baseSpeed = kineticState->intMultiplier * kineticState->baseSpeed;
    float acceleratedSpeed = kineticState->intMultiplier * kineticState->acceleratedSpeed;

    if (!kineticState->wasMoveAction && !activeMouseStates[SerializedMouseAction_Decelerate]) {
        kineticState->currentSpeed = initialSpeed;
    }

    bool isMoveAction = activeMouseStates[kineticState->upState] ||
                        activeMouseStates[kineticState->downState] ||
                        activeMouseStates[kineticState->leftState] ||
                        activeMouseStates[kineticState->rightState];

    mouse_speed_t mouseSpeed = MouseSpeed_Normal;
    if (activeMouseStates[SerializedMouseAction_Accelerate]) {
        kineticState->targetSpeed = acceleratedSpeed;
        mouseSpeed = MouseSpeed_Accelerated;
    } else if (activeMouseStates[SerializedMouseAction_Decelerate]) {
        kineticState->targetSpeed = deceleratedSpeed;
        mouseSpeed = MouseSpeed_Decelerated;
    } else if (isMoveAction) {
        kineticState->targetSpeed = baseSpeed;
    }

    if (mouseSpeed == MouseSpeed_Accelerated || (kineticState->wasMoveAction && isMoveAction && (kineticState->prevMouseSpeed != mouseSpeed))) {
        kineticState->currentSpeed = kineticState->targetSpeed;
    }

    if (isMoveAction) {
        if (kineticState->currentSpeed < kineticState->targetSpeed) {
            kineticState->currentSpeed += acceleration * (float)mouseElapsedTime / 1000.0f;
            if (kineticState->currentSpeed > kineticState->targetSpeed) {
                kineticState->currentSpeed = kineticState->targetSpeed;
            }
        } else {
            kineticState->currentSpeed -= acceleration * (float)mouseElapsedTime / 1000.0f;
            if (kineticState->currentSpeed < kineticState->targetSpeed) {
                kineticState->currentSpeed = kineticState->targetSpeed;
            }
        }

        float distance = kineticState->currentSpeed * (float)mouseElapsedTime / 1000.0f;


        if (kineticState->isScroll && !kineticState->wasMoveAction) {
            kineticState->xSum = 0;
            kineticState->ySum = 0;
        }


        // Update horizontal state

        bool horizontalMovement = true;
        if (activeMouseStates[kineticState->leftState]) {
            kineticState->xSum -= distance;
        } else if (activeMouseStates[kineticState->rightState]) {
            kineticState->xSum += distance;
        } else {
            horizontalMovement = false;
        }

        float xSumInt;
        float xSumFrac = modff(kineticState->xSum, &xSumInt);
        kineticState->xSum = xSumFrac;
        kineticState->xOut = xSumInt;

        if (kineticState->isScroll && !kineticState->wasMoveAction && kineticState->xOut == 0 && horizontalMovement) {
            kineticState->xOut = kineticState->xSum ? copysignf(1.0, kineticState->xSum) : 0;
            kineticState->xSum = 0;
        }

        // Update vertical state

        bool verticalMovement = true;
        if (activeMouseStates[kineticState->upState]) {
            kineticState->ySum -= distance;
        } else if (activeMouseStates[kineticState->downState]) {
            kineticState->ySum += distance;
        } else {
            verticalMovement = false;
        }

        float ySumInt;
        float ySumFrac = modff(kineticState->ySum, &ySumInt);
        kineticState->ySum = ySumFrac;
        kineticState->yOut = ySumInt;

        if (kineticState->isScroll && !kineticState->wasMoveAction && kineticState->yOut == 0 && verticalMovement) {
            kineticState->yOut = kineticState->ySum ? copysignf(1.0, kineticState->ySum) : 0;
            kineticState->ySum = 0;
        }
    } else {
        kineticState->currentSpeed = 0;
    }

    kineticState->prevMouseSpeed = mouseSpeed;
    kineticState->wasMoveAction = isMoveAction;
}

static void processMouseActions()
{
    mouseElapsedTime = Timer_GetElapsedTimeAndSetCurrent(&mouseUsbReportUpdateTime);

    processMouseKineticState(&MouseMoveState);
    ActiveUsbMouseReport->x = MouseMoveState.xOut;
    ActiveUsbMouseReport->y = MouseMoveState.yOut;
    MouseMoveState.xOut = 0;
    MouseMoveState.yOut = 0;

    processMouseKineticState(&MouseScrollState);
    ActiveUsbMouseReport->wheelX = MouseScrollState.xOut;
    ActiveUsbMouseReport->wheelY = MouseScrollState.yOut;
    MouseScrollState.xOut = 0;
    MouseScrollState.yOut = 0;

//  The following line makes the firmware crash for some reason:
//  SetDebugBufferFloat(60, mouseScrollState.currentSpeed);
//  TODO: Figure out why.
//  Oddly, the following line (which is the inlined version of the above) works:
//  *(float*)(DebugBuffer + 60) = mouseScrollState.currentSpeed;
//  The value parameter of SetDebugBufferFloat() seems to be the culprit because
//  if it's not used within the function it doesn't crash anymore.

    if (activeMouseStates[SerializedMouseAction_LeftClick]) {
        ActiveUsbMouseReport->buttons |= MouseButton_Left;
    }
    if (activeMouseStates[SerializedMouseAction_MiddleClick]) {
        ActiveUsbMouseReport->buttons |= MouseButton_Middle;
    }
    if (activeMouseStates[SerializedMouseAction_RightClick]) {
        ActiveUsbMouseReport->buttons |= MouseButton_Right;
    }
}

static layer_id_t previousLayer = LayerId_Base;

static void handleSwitchLayerAction(key_state_t *keyState, key_action_t *action)
{
    static key_state_t *doubleTapSwitchLayerKey;
    static uint32_t doubleTapSwitchLayerStartTime;
    static uint32_t doubleTapSwitchLayerTriggerTime;
    static bool isLayerDoubleTapToggled;

    if (doubleTapSwitchLayerKey && doubleTapSwitchLayerKey != keyState && !keyState->previous) {
        doubleTapSwitchLayerKey = NULL;
    }

    if (action->type != KeyActionType_SwitchLayer) {
        return;
    }

    if (!keyState->previous && isLayerDoubleTapToggled && ToggledLayer == action->switchLayer.layer) {
        ToggledLayer = LayerId_Base;
        isLayerDoubleTapToggled = false;
    }

    if (keyState->previous && doubleTapSwitchLayerKey == keyState &&
        Timer_GetElapsedTime(&doubleTapSwitchLayerTriggerTime) > DoubleTapSwitchLayerReleaseTimeout)
    {
        ToggledLayer = LayerId_Base;
    }

    if (!keyState->previous && previousLayer == LayerId_Base && action->switchLayer.mode == SwitchLayerMode_HoldAndDoubleTapToggle) {
        if (doubleTapSwitchLayerKey && Timer_GetElapsedTimeAndSetCurrent(&doubleTapSwitchLayerStartTime) < DoubleTapSwitchLayerTimeout) {
            ToggledLayer = action->switchLayer.layer;
            isLayerDoubleTapToggled = true;
            doubleTapSwitchLayerTriggerTime = CurrentTime;
        } else {
            doubleTapSwitchLayerKey = keyState;
        }
        doubleTapSwitchLayerStartTime = CurrentTime;
    }
}

static uint8_t basicScancodeIndex = 0;
static uint8_t mediaScancodeIndex = 0;
static uint8_t systemScancodeIndex = 0;
void applyKeyAction(key_state_t *keyState, key_action_t *action) {
    handleSwitchLayerAction(keyState, action);

    switch (action->type) {
        case KeyActionType_Keystroke:
            addModifiersToReport(action->keystroke.modifiers);
            switch (action->keystroke.keystrokeType) {
                case KeystrokeType_Basic:
                    if (basicScancodeIndex >= USB_BASIC_KEYBOARD_MAX_KEYS || action->keystroke.scancode == 0) {
                        break;
                    }
                    ActiveUsbBasicKeyboardReport->scancodes[basicScancodeIndex++] = action->keystroke.scancode;
                    break;
                case KeystrokeType_Media:
                    if (mediaScancodeIndex >= USB_MEDIA_KEYBOARD_MAX_KEYS) {
                        break;
                    }
                    ActiveUsbMediaKeyboardReport->scancodes[mediaScancodeIndex++] = action->keystroke.scancode;
                    break;
                case KeystrokeType_System:
                    if (systemScancodeIndex >= USB_SYSTEM_KEYBOARD_MAX_KEYS) {
                        break;
                    }
                    ActiveUsbSystemKeyboardReport->scancodes[systemScancodeIndex++] = action->keystroke.scancode;
                    break;
            }
            break;
        case KeyActionType_Mouse:
            activeMouseStates[action->mouseAction] = true;
            break;
        case KeyActionType_SwitchLayer:
            // Handled by handleSwitchLayerAction()
            break;
        case KeyActionType_SwitchKeymap:
            SwitchKeymapById(action->switchKeymap.keymapId);
            break;
        case KeyActionType_PlayMacro:
            Macros_StartMacro(action->playMacro.macroId);
            break;
    }
}


static void mitigateBouncing(key_state_t *keyState) {
    uint8_t debounceTimeOut = (keyState->previous ? DebounceTimePress : DebounceTimeRelease);
    if (keyState->debouncing) {
        if ((uint8_t)(CurrentTime - keyState->timestamp) > debounceTimeOut) {
            keyState->debouncing = false;
        } else {
            keyState->current = keyState->previous;
        }
    } else if (keyState->previous != keyState->current) {
        keyState->timestamp = CurrentTime;
        keyState->debouncing = true;
    }
}


void sendKeyboardEvents() {
    bool HasUsbBasicKeyboardReportChanged = memcmp(ActiveUsbBasicKeyboardReport, GetInactiveUsbBasicKeyboardReport(), sizeof(usb_basic_keyboard_report_t)) != 0;
    bool HasUsbMediaKeyboardReportChanged = memcmp(ActiveUsbMediaKeyboardReport, GetInactiveUsbMediaKeyboardReport(), sizeof(usb_media_keyboard_report_t)) != 0;
    bool HasUsbSystemKeyboardReportChanged = memcmp(ActiveUsbSystemKeyboardReport, GetInactiveUsbSystemKeyboardReport(), sizeof(usb_system_keyboard_report_t)) != 0;

    if (HasUsbBasicKeyboardReportChanged) {
        usb_status_t status = UsbBasicKeyboardAction();
        if (status == kStatus_USB_Success) {
            UsbReportUpdateSemaphore |= 1 << USB_BASIC_KEYBOARD_INTERFACE_INDEX;
        }
    }

    if (HasUsbMediaKeyboardReportChanged) {
        usb_status_t status = UsbMediaKeyboardAction();
        if (status == kStatus_USB_Success) {
            UsbReportUpdateSemaphore |= 1 << USB_MEDIA_KEYBOARD_INTERFACE_INDEX;
        }
    }

    if (HasUsbSystemKeyboardReportChanged) {
        usb_status_t status = UsbSystemKeyboardAction();
        if (status == kStatus_USB_Success) {
            UsbReportUpdateSemaphore |= 1 << USB_SYSTEM_KEYBOARD_INTERFACE_INDEX;
        }
    }
}

void resetKeyboardReports() {
    ResetActiveUsbBasicKeyboardReport();
    ResetActiveUsbMediaKeyboardReport();
    ResetActiveUsbSystemKeyboardReport();
}


uint32_t UsbReportUpdateCounter;


static const int SEC_ROLE_KICKIN_THRESHOLD = 250;

static bool execModifierActions() {
    int executedModifierActionCount = 0;

    for (uint8_t i = 0; i < State.actionCount; ++i) {
        pending_key_t *actionKey = action(i);
        key_action_t *action = resolveAction(&actionKey->keyRef);
        if (actionKey->keyRef.state->current && action->type == KeyActionType_Keystroke && !action->keystroke.scancode) {
            addModifiersToReport(action->keystroke.modifiers);
            executedModifierActionCount++;
        }
    }

    return executedModifierActionCount > 0;
}

static void executeActions() {
    if (State.scheduledForImmediateExecutionAmount > 0) {
        for (int i = State.scheduledForImmediateExecutionAmount - 1; i >= 0; --i) {
            pending_key_t key = State.scheduledForImmediateExecution[i]; ;
            applyKeyAction(key.keyRef.state, resolveAction(&key.keyRef));
        }

        // easier to understand on the example:
        // we held shift and then pressed key with secondary role
        // shift was released first, but right after - the other key too.
        // they were held together, so shift should've been accounted when the other key
        // was released. Here we prepend the modifier states from less than 100ms ago to the
        // immediately executed actions (the ones that are executed as a result of key release).
        if (CurrentTime - State.lastModifierReportUpdate < 100) {
            addModifiersToReport(State.lastUpdatedModifierFlags);
        }

        // like e.g. to type 'A' of 'S'
        execModifierActions();
        sendKeyboardEvents();
//        resetKeyboardReports();
    }

    for (int i = State.actionCount - 1; i >= 0; --i) {
        pending_key_t* actionKey = action(i);

        key_state_t *keyState = actionKey->keyRef.state;
        if (keyState->current || keyState->suppressed) {
            applyKeyAction(keyState, resolveAction(&actionKey->keyRef));
        }

        if (!keyState->current || keyState->suppressed) {
            actionKey->keyRef.state->suppressed = false;
            untrackActionAt(i);
        }
    }
}

static bool secondaryRoleTimeoutElapsed(pending_key_t *modifier) {
    return (CurrentTime - modifier->enqueueTime) > SEC_ROLE_KICKIN_THRESHOLD;
}

void handleFreeTypeState() {
    bool mayStartListeningToSecondaryRoleActivation = false;
    if (State.longestPressedKey != NULL) {
        mayStartListeningToSecondaryRoleActivation = State.modifierCount > 0 && secondaryRole(&State.longestPressedKey->keyRef);
    }
    if (mayStartListeningToSecondaryRoleActivation) {
        switchToState(1);
    } else {
        for (uint8_t i = 0; i < State.modifierCount; ++i) {
            if (modifier(i)->keyRef.state->current) {
                addAction(modifier(i));
            }
        }
        State.modifierCount = 0;
        executeActions();
    }
}

void handleSecondaryRoleReleaseAwaitState() {
    // apply all the released modifiers right away
    //
    // issues:
    // - if there are normal modifiers released recently - they should be also applied (they were on while the tracked
    //   keys were kept pressed;
    bool shouldTriggerSecondaryRoleActivationMode = false;
    for (int i = State.modifierCount - 1; i >= 0; --i) {
        pending_key_t *pendingModifier = modifier(i);
        key_state_t *keyState = pendingModifier->keyRef.state;
        if (!keyState->current) {
            if (State.modifierCount > 1) {
                // FIXME - this a workaround which is considered in the state == 2, doing this is roughly equivalent to pushing the mod into the action array
                State.releasedActionKeyEnqueueTime = pendingModifier->enqueueTime;
                shouldTriggerSecondaryRoleActivationMode = true;
            } else {
                if (!secondaryRoleTimeoutElapsed(pendingModifier)) {
                    scheduleForImmediateExecution(pendingModifier);
                }
                untrackModifier(i);
            }
        }
    }

    // see if there are any modifiers still pending
    // also see if any action is released (even the modifier would do)
    // this will indicate that the proper 'secondary role active' mode can turn on
    if (State.modifierCount > 0) {
        for (uint8_t i = 0; i < State.actionCount && !shouldTriggerSecondaryRoleActivationMode; ++i) {
            if (!action(i)->keyRef.state->current) {
                shouldTriggerSecondaryRoleActivationMode = true;
            }
        }
    }

    // detect if any modifier becomes active
    //
    // the previous conditions have to be met
    // or any of the modifiers may pressed long enough
    bool activatedModifierDetected = false;
    for (uint8_t i = 0; i < State.modifierCount; ++i) {
        if (modifier(i)->keyRef.state->current) {
            if (shouldTriggerSecondaryRoleActivationMode || secondaryRoleTimeoutElapsed(modifier(i))) {
                activatedModifierDetected = true;
                break;
            }
        }
    }

    // if can activate sec role mode - do it
    if (activatedModifierDetected) {
        sendDebugChar(HID_KEYBOARD_SC_P);
        // turn all released pending actions on
        switchToState(2);
        // if there are no modifiers pending anymore - return to simple mode
    } else if (State.modifierCount == 0) {
        executeActions();
        switchToState(0);
    }
}

void handleActiveSecondaryRoleState() {

    // detect the latest released action
    // should this be done in the previous stage?
    for (uint8_t i = 0; i < State.actionCount; ++i) {
        pending_key_t *key = action(i);
        if (!key->keyRef.state->current) {
            scheduleForImmediateExecution(key);
        }
    }

    // check whether we still can stay in the sec role active mode
    bool activeModifierDetected = false;
    for (int i = State.modifierCount - 1; i >= 0; --i) {
        pending_key_t *pendingModifier = modifier(i);
        if (!pendingModifier->keyRef.state->current) {
            continue;
        }
        bool timeoutElapsed = secondaryRoleTimeoutElapsed(pendingModifier);
        // if either the modifier has been already activated
        if (pendingModifier->activated ||
            // or it is ready to be activated - timeout elapsed and the modifier was released before the action key
            (timeoutElapsed || pendingModifier->enqueueTime < State.releasedActionKeyEnqueueTime)){
            // if that is the case - apply the modifier right away

            uint8_t secRole = secondaryRole(&pendingModifier->keyRef);
            bool isActionLayerSwitch = IS_SECONDARY_ROLE_LAYER_SWITCHER(secRole);
            if (isActionLayerSwitch) {
                State.activeLayer = SECONDARY_ROLE_LAYER_TO_LAYER_ID(secRole);
            } else if (IS_SECONDARY_ROLE_MODIFIER(secRole)) {
                addModifiersToReport(SECONDARY_ROLE_MODIFIER_TO_HID_MODIFIER(secRole));
            }

            pendingModifier->activated = true;
            activeModifierDetected = true;
        }
    }

    // emit primary roles of the all the modifiers that have been released
    for (int i = State.modifierCount - 1; i >= 0; --i) {
        pending_key_t *pendingModifier = modifier(i);
        bool timeoutElapsed = secondaryRoleTimeoutElapsed(pendingModifier);
        if (!pendingModifier->keyRef.state->current) {
            if (!timeoutElapsed && !pendingModifier->activated) {
                scheduleForImmediateExecution(pendingModifier);
            }
            untrackModifier(i);
        }
    }

    // apply all the pending actions, at this moment none of the actions should be in released state, so remove those
    // before executing the actions - sort them based on enqueue time(?)
    if (!activeModifierDetected) {
        State.stateType = State.actionCount > 0 ? 1 : 0;
    }

    executeActions();
}

void addModifiersToReport(int flags) {
    ActiveUsbBasicKeyboardReport->modifiers |= flags;
    if (flags) {
        State.lastUpdatedModifierFlags = ActiveUsbBasicKeyboardReport->modifiers;
        State.lastModifierReportUpdate = CurrentTime;
    }
}

static void updateActiveUsbReports(void)
{
    if (MacroPlaying) {
        Macros_ContinueMacro();
        memcpy(ActiveUsbMouseReport, &MacroMouseReport, sizeof MacroMouseReport);
        memcpy(ActiveUsbBasicKeyboardReport, &MacroBasicKeyboardReport, sizeof MacroBasicKeyboardReport);
        memcpy(ActiveUsbMediaKeyboardReport, &MacroMediaKeyboardReport, sizeof MacroMediaKeyboardReport);
        memcpy(ActiveUsbSystemKeyboardReport, &MacroSystemKeyboardReport, sizeof MacroSystemKeyboardReport);
        return;
    }

    memset(activeMouseStates, 0, ACTIVE_MOUSE_STATES_COUNT);

    basicScancodeIndex = 0;
    mediaScancodeIndex = 0;
    systemScancodeIndex = 0;

    State.releasedActionKeyEnqueueTime = 0;
    State.longestPressedKey = NULL;
    State.scheduledForImmediateExecutionAmount = 0;

    for (uint8_t slotId = 0; slotId < SLOT_COUNT; slotId++) {
        for (uint8_t keyId = 0; keyId < MAX_KEY_COUNT_PER_MODULE; keyId++) {
            key_state_t *keyState = &KeyStates[slotId][keyId];

            mitigateBouncing(keyState);

            if (keyState->current && !keyState->previous) {
                if (SleepModeActive) {
                    WakeUpHost();
                }
            }

            if (keyState->current) {
                updateActiveKey(keyState, slotId, keyId);
            }

            keyState->previous = keyState->current;
        }
    }

    State.activeLayer = GetActiveLayer();
    updateLongestPressedKey();

    // free mode - none of the modifiers is pressed yet, merely wait for them and push through all the
    // actions in the meantime
    if (State.stateType == 0) {
        handleFreeTypeState();
    }

    // await mode - a modifier key is held and we buffer the action keys to see if any of them will be released
    // before the modifier key is released.
    if (State.stateType == 1) {
        handleSecondaryRoleReleaseAwaitState();
    }

    // secondary role mode - execute all the actions write-through, track held sec role keys, bail out as soon as
    // there is none
    if (State.stateType == 2) {
        handleActiveSecondaryRoleState();
    }

    LedDisplay_SetLayer(State.activeLayer);

    processMouseActions();
    previousLayer = State.activeLayer;
}

void UpdateUsbReports(void)
{
    static uint32_t lastUpdateTime;

    for (uint8_t keyId = 0; keyId < RIGHT_KEY_MATRIX_KEY_COUNT; keyId++) {
        KeyStates[SlotId_RightKeyboardHalf][keyId].current = RightKeyMatrix.keyStates[keyId];
    }

    if (UsbReportUpdateSemaphore && !SleepModeActive) {
        if (Timer_GetElapsedTime(&lastUpdateTime) < USB_SEMAPHORE_TIMEOUT) {
            return;
        } else {
            UsbReportUpdateSemaphore = 0;
        }
    }

    lastUpdateTime = CurrentTime;
    UsbReportUpdateCounter++;

    resetKeyboardReports();
    ResetActiveUsbMouseReport();

    updateActiveUsbReports();

    bool HasUsbMouseReportChanged = memcmp(ActiveUsbMouseReport, GetInactiveUsbMouseReport(), sizeof(usb_mouse_report_t)) != 0;

    sendKeyboardEvents();

    // Send out the mouse position and wheel values continuously if the report is not zeros, but only send the mouse button states when they change.
    if (HasUsbMouseReportChanged || ActiveUsbMouseReport->x || ActiveUsbMouseReport->y ||
        ActiveUsbMouseReport->wheelX || ActiveUsbMouseReport->wheelY) {
        usb_status_t status = UsbMouseAction();
        if (status == kStatus_USB_Success) {
            UsbReportUpdateSemaphore |= 1 << USB_MOUSE_INTERFACE_INDEX;
        }
    }
}

