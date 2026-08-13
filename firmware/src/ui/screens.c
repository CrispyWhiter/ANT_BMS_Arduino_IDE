

#include <string.h>

#include "screens.h"

objects_t objects;

static lv_obj_t *create_value_label(lv_obj_t *parent) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, 0, 0);
    lv_obj_set_size(label, 1, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text_static(label, "--");
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    return label;
}

void create_screen_main() {
    memset(&objects, 0, sizeof(objects));

    lv_obj_t *screen = lv_obj_create(NULL);
    objects.main = screen;
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_size(screen, 320, 240);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    objects.label_soc = create_value_label(screen);
    objects.label_range_value = create_value_label(screen);
    objects.label_capacity_value = create_value_label(screen);
    objects.label_total_voltage_value = create_value_label(screen);
    objects.label_current_value = create_value_label(screen);
    objects.label_power_value = create_value_label(screen);
    objects.label_temperature_value = create_value_label(screen);
    objects.label_delta_value = create_value_label(screen);
    objects.label_cycle_value = create_value_label(screen);
}

void create_screens() {
    lv_disp_t *display = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_basic_init(display);
    lv_disp_set_theme(display, theme);
    create_screen_main();
}
