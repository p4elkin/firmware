//
// Created by Aleksandr Pchelintcev on 2018-11-11.
//
#ifndef RIGHT_ARRAYS_H
#define RIGHT_ARRAYS_H

#include <stdlib.h>
#include "keyboard_state.h"

int IndexOf(pending_key_t *keys, key_ref_t *ref, uint8_t size);
void InsertAt(pending_key_t *keys, pending_key_t *newActiveKey, uint8_t index, uint8_t size);
void RemoveAt(pending_key_t *keys, uint8_t index, uint8_t size);
bool Remove(pending_key_t *keys, key_ref_t *record, uint8_t size);

#endif //RIGHT_ARRAYS_H
