// KEY (GPIO18) and BOOT (GPIO0) buttons via the multi_button library.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_BTN_KEY = 0,   // GPIO18
    BOARD_BTN_BOOT,      // GPIO0
} board_btn_t;

typedef enum {
    BOARD_BTN_EVT_SINGLE = 0,
    BOARD_BTN_EVT_DOUBLE,
    BOARD_BTN_EVT_LONG,
} board_btn_evt_t;

typedef void (*board_btn_cb_t)(board_btn_t btn, board_btn_evt_t evt, void *ctx);

// Starts a 5 ms scan timer. Call once.
void board_button_init(void);

// Register a single callback invoked (from a FreeRTOS timer task context) for
// every button event.
void board_button_set_callback(board_btn_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
