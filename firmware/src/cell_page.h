#pragma once

#include <lvgl.h>

#include "bms_model.h"

namespace CellPage {

void create();

void update(const BmsData &data);

lv_obj_t *screen();

void showFirstGroup();

bool showNextGroup();

}
