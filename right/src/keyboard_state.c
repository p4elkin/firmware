#include <stdlib.h>
#include "keyboard_state.h"
#include "key_action.h"
#include "led_display.h"
#include "keymap.h"
#include "timer.h"
#include "arrays.h"

keyboard_state_t State = {
        .stateType = 0,
        .modifierCount = 0,
        .actionCount = 0,
        .longestPressedKey = NULL,
        .activeLayer = LayerId_Base,
        .releasedActionKeyEnqueueTime = 0
};

key_action_t *resolveAction(key_ref_t *ref) {
    return &CurrentKeymap[State.activeLayer][ref->slotId][ref->keyId];
}

uint8_t secondaryRole(key_ref_t *ref) {
    key_action_t *action = resolveAction(ref);
    return action->type == KeyActionType_Keystroke ? action->keystroke.secondaryRole : 0;
}

void addAction(pending_key_t *newActiveKey) {
    InsertAt(State.actions, newActiveKey, State.actionCount, State.actionCount);
    ++State.actionCount;
}

void addModifier(pending_key_t *key) {
    InsertAt(State.modifiers, key, State.modifierCount, State.modifierCount);
    ++State.modifierCount;
}

void untrackModifier(uint8_t index) {
    RemoveAt(State.modifiers, index, State.modifierCount--);
}

void switchToState(uint8_t i) {
    State.stateType = i;
}

pending_key_t *modifier(uint8_t index) {
    return &State.modifiers[index];
}

pending_key_t *action(uint8_t index) {
    return &State.actions[index];
}

bool isTracked(key_ref_t *ref) {
    bool registeredAsModifier = IndexOf(State.modifiers, ref, State.modifierCount) >= 0;
    bool registeredAsAction = IndexOf(State.actions, ref, State.actionCount) >= 0;
    return registeredAsAction || registeredAsModifier;
}

void updateLongestPressedKey() {
    // detect the longest held key, might affect the further algorithm
    for (uint8_t i = 0; i < State.modifierCount; ++i) {
        if (!State.longestPressedKey || State.longestPressedKey->enqueueTime > State.modifiers[i].enqueueTime) {
            State.longestPressedKey = &State.modifiers[i];
        }
    }

    for (uint8_t i = 0; i < State.actionCount; ++i) {
        if (!State.longestPressedKey || State.longestPressedKey->enqueueTime > State.actions[i].enqueueTime) {
            key_action_t *a = resolveAction(&action(i)->keyRef);
            if (a->type == KeyActionType_Keystroke && a->keystroke.scancode) {
                State.longestPressedKey = &State.actions[i];
            }
        }
    }
}

void untrackActionAt(uint8_t index) {
    RemoveAt(State.actions, index, State.actionCount--);
}

void updateActiveKey(key_state_t *keyState, uint8_t slotId, uint8_t keyId) {
    key_ref_t ref = {
            .keyId = keyId,
            .slotId = slotId,
            .state = keyState
    };

    pending_key_t key = {
            .activated = false,
            .enqueueTime = CurrentTime,
            .keyRef = ref
    };

    bool hasSecondaryRole = secondaryRole(&ref);

    // distribute previously untracked keys between action and modifier queue
    if (!isTracked(&ref)) {
        if (hasSecondaryRole) {
            addModifier(&key);
        } else {
            addAction(&key);
        }
    }
}

void scheduleForImmediateExecution(pending_key_t *key) {
    State.scheduledForImmediateExecution[State.scheduledForImmediateExecutionAmount++ % 5] = *key;
    State.releasedActionKeyEnqueueTime = key->enqueueTime;
    key->keyRef.state->timestamp = CurrentTime;
}
