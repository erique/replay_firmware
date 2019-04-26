
#include "../board.h"
#include "../hardware/io.h"
#include "../hardware/irq.h"

#include "../messaging.h"
#include "../hardware/timer.h"

uint8_t pin_fpga_init_l = TRUE;
uint8_t pin_fpga_done = FALSE;

void IO_DriveLow_OD_(uint32_t pin, const char* pin_name)
{
    printf("%s  : %08x (%s)\r\n", __FUNCTION__, pin, pin_name);
    digitalWrite(pin, LOW);
}

void IO_DriveHigh_OD_(uint32_t pin, const char* pin_name)
{
    printf("%s : %08x (%s)\r\n", __FUNCTION__, pin, pin_name);
    digitalWrite(pin, HIGH);
}

static uint8_t IO_Input_(uint32_t pin)  // return pin state high == TRUE / low == FALSE
{
    if (pin == PIN_CARD_DETECT) {
        return FALSE;

    } else if (pin == PIN_CODER_FITTED_L) {
        return TRUE;

    } else if (pin == PIN_MENU_BUTTON) {
        return TRUE;

    } else if (pin == PIN_FPGA_DONE) {
        return pin_fpga_done;

    } else if (pin == PIN_FPGA_INIT_L) {
        return pin_fpga_init_l;

    } else {
        return digitalRead(pin);
    }
}

uint8_t IO_Input_H_(uint32_t pin, const char* pin_name)  // returns true if pin high
{
    uint8_t v = IO_Input_(pin);
    printf("%s : %08x (%s) => %s\r\n", __FUNCTION__, pin, pin_name, v ? "TRUE" : "FALSE");
    return v;
}

uint8_t IO_Input_L_(uint32_t pin, const char* pin_name)  // returns true if pin low
{
    uint8_t v = !IO_Input_(pin);
    printf("%s : %08x (%s) => %s\r\n", __FUNCTION__, pin, pin_name, v ? "TRUE" : "FALSE");
    return v;
}

void ISR_VerticalBlank();       // a bit hacky - just assume this is implemented _somewhere_
static volatile uint32_t vbl_counter = 0;
static void* ISR_io(void* param)
{
    (void)param;
    return NULL;
}

void IO_Init(void)
{
}

void IO_ClearOutputData_(uint32_t pins, const char* pin_names)
{
    printf("%s : %08x (%s)\r\n", __FUNCTION__, pins, pin_names);
}
void IO_SetOutputData_(uint32_t pins, const char* pin_names)
{
    printf("%s : %08x (%s)\r\n", __FUNCTION__, pins, pin_names);
}

void IO_WaitVBL(void)
{
}
