#include <stdlib.h>
#include "debug.h"
#include "usb_composite_device.h"
#include "usb_device_hid.h"
#include <stdbool.h>

bool debug = false;

usb_basic_keyboard_report_t customReport[2];
static uint8_t codes[] = {HID_KEYBOARD_SC_0_AND_CLOSING_PARENTHESIS, HID_KEYBOARD_SC_1_AND_EXCLAMATION,
                          HID_KEYBOARD_SC_2_AND_AT, HID_KEYBOARD_SC_3_AND_HASHMARK, HID_KEYBOARD_SC_4_AND_DOLLAR,
                          HID_KEYBOARD_SC_5_AND_PERCENTAGE, HID_KEYBOARD_SC_6_AND_CARET,
                          HID_KEYBOARD_SC_7_AND_AMPERSAND, HID_KEYBOARD_SC_8_AND_ASTERISK,
                          HID_KEYBOARD_SC_9_AND_OPENING_PARENTHESIS};

void sendDebugChar(uint8_t keyCode) {
    if (debug) {
        customReport->scancodes[0] = keyCode;
        USB_DeviceHidSend(UsbCompositeDevice.basicKeyboardHandle, USB_BASIC_KEYBOARD_ENDPOINT_INDEX,
                          (uint8_t *) customReport, USB_BASIC_KEYBOARD_REPORT_LENGTH);
    }
}

void sendDebugInt(uint8_t integer) {
    sendDebugChar(codes[integer]);
}