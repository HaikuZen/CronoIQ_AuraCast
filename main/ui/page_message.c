#include "ui_internal.h"

#include "app_voice.h"

static lv_obj_t *s_msg;
static lv_obj_t *s_ww_state;
static lv_obj_t *s_ww_hint;
static bool s_wake;

lv_obj_t *page_message_create(lv_obj_t *parent)
{
    s_msg = ui_label(parent, &lv_font_montserrat_48, "Hello!");
    lv_obj_align(s_msg, LV_ALIGN_TOP_MID, 0, 60);

    s_ww_state = ui_label(parent, &lv_font_montserrat_20, "");
    lv_obj_set_width(s_ww_state, 380);
    lv_obj_set_style_text_align(s_ww_state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ww_state, LV_ALIGN_BOTTOM_MID, 0, -60);

    s_ww_hint = ui_label(parent, &lv_font_montserrat_16, "");
    lv_obj_set_width(s_ww_hint, 380);
    lv_obj_set_style_text_align(s_ww_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ww_hint, LV_ALIGN_BOTTOM_MID, 0, -28);

    return parent;
}

void page_message_set_wake(bool on)
{
    s_wake = on;
}

void page_message_update(void)
{
    if (s_wake) {
        lv_label_set_text(s_msg, "Here we go!");
        lv_obj_align(s_msg, LV_ALIGN_CENTER, 0, -10);
        lv_obj_add_flag(s_ww_state, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ww_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(s_msg, "Hello!");
    lv_obj_align(s_msg, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_clear_flag(s_ww_state, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ww_hint, LV_OBJ_FLAG_HIDDEN);

    if (app_voice_ww_enrolled()) {
        lv_label_set_text(s_ww_state, LV_SYMBOL_OK "  Wake-word \"Ehi Ciccio\" attiva");
        lv_label_set_text(s_ww_hint, "Tieni premuto il tasto per registrarla di nuovo");
    } else {
        lv_label_set_text(s_ww_state, LV_SYMBOL_WARNING "  Wake-word non registrata");
        lv_label_set_text(s_ww_hint, "Tieni premuto il tasto per registrare \"Ehi Ciccio\"");
    }
}
