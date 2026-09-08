#include "ui_internal.h"

static lv_obj_t *s_msg;

lv_obj_t *page_message_create(lv_obj_t *parent)
{
    s_msg = ui_label(parent, &lv_font_montserrat_48, "Hello!");
    lv_obj_center(s_msg);
    return parent;
}

void page_message_update(void)
{
    // Static placeholder page — nothing to refresh yet.
}
