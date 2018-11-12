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
    // modifier refs that were pressed at some point while
    // a key that might be involved into a sec role thing is pressed (be that a modifier or an action).
    key_ref_t relatedModifierRefs[4];
    // count of such actions
    uint8_t relatedModifierCount;

} pending_key_t;

typedef struct  {
    pending_key_t modifiers[5];
    uint8_t modifierCount;

    pending_key_t actions[5];
    uint8_t actionCount;

    pending_key_t scheduledForImmediateExecution[5];
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
