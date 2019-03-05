#ifndef KEYBOARDSTATE_H
#define KEYBOARDSTATE_H

#include "layer.h"
#include "key_states.h"
#include "key_action.h"
#include "keymap.h"


typedef struct {
    uint8_t keyId;
    uint8_t slotId;
    key_state_t *state;
} key_ref_t;

typedef struct {
    // timestamp of the enqueueing the key press (when it started to wait which role to emit)
    uint32_t enqueueTime;
    // related key info ref
    key_ref_t keyRef;
    // indicates whether a modifier key was activated either as a result of timeout
    // or as a result of accompanying action key press. This flag set to true means that the primary role of the
    // key should never be emitted anymore.
    bool activated;

} pending_key_t;

typedef struct  {
    pending_key_t modifiers[10];
    uint8_t modifierCount;

    pending_key_t actions[10];
    uint8_t actionCount;

    pending_key_t scheduledForImmediateExecution[4];
    uint8_t scheduledForImmediateExecutionAmount;

    layer_id_t activeLayer;

    uint32_t releasedActionKeyEnqueueTime;
    pending_key_t *longestPressedKey;
    uint8_t stateType;
    uint8_t lastUpdatedModifierFlags;
    uint32_t lastModifierReportUpdate;
} keyboard_state_t;

extern keyboard_state_t State;

void updateActiveKey(key_state_t *keyState, uint8_t slotId, uint8_t keyId);

void scheduleForImmediateExecution(pending_key_t *key);
void addModifier(pending_key_t *key);
void addAction(pending_key_t *newActiveKey);

void untrackModifier(uint8_t index);
void untrackActionAt(uint8_t index);

pending_key_t* modifier(uint8_t index);
pending_key_t* action(uint8_t index);
key_action_t *resolveAction(key_ref_t *ref);

uint8_t secondaryRole(key_ref_t *ref);
bool isTracked(key_ref_t *ref);
void updateLongestPressedKey();
void switchToState(uint8_t i);

#endif
