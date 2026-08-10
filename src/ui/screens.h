

#ifndef BMS_LVGL_SCREENS_H
#define BMS_LVGL_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *label_soc;
    lv_obj_t *label_range_value;
    lv_obj_t *label_capacity_value;
    lv_obj_t *label_total_voltage_value;
    lv_obj_t *label_current_value;
    lv_obj_t *label_power_value;
    lv_obj_t *label_temperature_value;
    lv_obj_t *label_delta_value;
    lv_obj_t *label_cycle_value;
} objects_t;

extern objects_t objects;

void create_screen_main();
void create_screens();

#ifdef __cplusplus
}
#endif

#endif
