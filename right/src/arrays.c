#include "arrays.h"
#include "keyboard_state.h"

int IndexOf(pending_key_t *keys, key_ref_t *ref, uint8_t size) {
    for (uint8_t i = 0; i < size; ++i) {
        if (keys[i].keyRef.slotId == ref->slotId && keys[i].keyRef.keyId == ref->keyId) {
            return (int) i;
        }
    }
    return -1;
}

void RemoveAt(pending_key_t *keys, uint8_t index, uint8_t size) {
    for (int i = index; i < size - 1; ++i) {
        keys[i] = keys[i + 1];
    }
}

void InsertAt(pending_key_t *keys, pending_key_t *newActiveKey, uint8_t index, uint8_t size) {
    for (int i = size - 1; i > index; --i) {
        keys[i] = keys[i - 1];
    }
    keys[index] = *newActiveKey;
}

bool Remove(pending_key_t *keys, key_ref_t *record, uint8_t size) {
    int index = IndexOf(keys, record, size);
    if (index >= 0) {
        RemoveAt(keys, (uint8_t) index, size);
        return true;
    }
    return false;
}
