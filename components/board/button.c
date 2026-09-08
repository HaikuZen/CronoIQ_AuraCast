#include "button.h"
#include "multi_button.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"

#define KEY_PIN   18
#define BOOT_PIN  0
#define ACTIVE_LOW 0

static Button s_key;
static Button s_boot;
static board_btn_cb_t s_cb;
static void *s_ctx;

static uint8_t read_pin(uint8_t id)
{
    switch (id) {
    case BOARD_BTN_KEY:  return gpio_get_level(KEY_PIN);
    case BOARD_BTN_BOOT: return gpio_get_level(BOOT_PIN);
    default:             return 1;
    }
}

static void emit(board_btn_t btn, board_btn_evt_t evt)
{
    if (s_cb) {
        s_cb(btn, evt, s_ctx);
    }
}

static void key_single(Button *b)  { (void)b; emit(BOARD_BTN_KEY, BOARD_BTN_EVT_SINGLE); }
static void key_double(Button *b)  { (void)b; emit(BOARD_BTN_KEY, BOARD_BTN_EVT_DOUBLE); }
static void key_long(Button *b)    { (void)b; emit(BOARD_BTN_KEY, BOARD_BTN_EVT_LONG); }
static void boot_single(Button *b) { (void)b; emit(BOARD_BTN_BOOT, BOARD_BTN_EVT_SINGLE); }
static void boot_double(Button *b) { (void)b; emit(BOARD_BTN_BOOT, BOARD_BTN_EVT_DOUBLE); }
static void boot_long(Button *b)   { (void)b; emit(BOARD_BTN_BOOT, BOARD_BTN_EVT_LONG); }

static void tick_cb(void *arg)
{
    (void)arg;
    button_ticks();
}

void board_button_set_callback(board_btn_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
}

void board_button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << KEY_PIN) | (1ULL << BOOT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    button_init(&s_key, read_pin, ACTIVE_LOW, BOARD_BTN_KEY);
    button_attach(&s_key, BTN_SINGLE_CLICK, key_single);
    button_attach(&s_key, BTN_DOUBLE_CLICK, key_double);
    button_attach(&s_key, BTN_LONG_PRESS_START, key_long);
    button_start(&s_key);

    button_init(&s_boot, read_pin, ACTIVE_LOW, BOARD_BTN_BOOT);
    button_attach(&s_boot, BTN_SINGLE_CLICK, boot_single);
    button_attach(&s_boot, BTN_DOUBLE_CLICK, boot_double);
    button_attach(&s_boot, BTN_LONG_PRESS_START, boot_long);
    button_start(&s_boot);

    const esp_timer_create_args_t targs = {
        .callback = tick_cb,
        .name = "btn_tick",
    };
    esp_timer_handle_t th;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &th));
    ESP_ERROR_CHECK(esp_timer_start_periodic(th, 5 * 1000));  // 5 ms
}
