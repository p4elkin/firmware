#include "bridge_protocol_handler.h"
#include "test_led.h"
#include "main.h"
#include "i2c_addresses.h"
#include "i2c.h"
#include "iso_jumper.h"

void SetError(uint8_t error);
void SetGenericError();
void SetResponseByte(uint8_t response);

void SetError(uint8_t error) {
    BridgeTxBuffer[0] = error;
}

void SetGenericError()
{
    SetError(PROTOCOL_RESPONSE_GENERIC_ERROR);
}

// Set a single byte as the response.
void SetResponseByte(uint8_t response)
{
    BridgeTxBuffer[1] = response;
}

void BridgeProtocolHandler()
{
    uint8_t commandId = BridgeRxBuffer[0];
    switch (commandId) {
        case BRIDGE_COMMAND_GET_KEY_STATES:
            BridgeTxSize = KEYBOARD_MATRIX_COLS_NUM*KEYBOARD_MATRIX_ROWS_NUM;
            memcpy(BridgeTxBuffer, keyMatrix.keyStates, BridgeTxSize);
            break;
        case BRIDGE_COMMAND_SET_LED:
            TEST_LED_OFF();
            BridgeTxSize = 0;
            TEST_LED_SET(BridgeRxBuffer[1]);
            break;
        case BRIDGE_COMMAND_GET_ISO_JUMPER_STATE:
            BridgeTxBuffer[0] = IsoJumperState;
            break;
    }
}