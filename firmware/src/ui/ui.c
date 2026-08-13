

#include "ui.h"
#include "screens.h"

void ui_init() {
    create_screens();
    lv_scr_load(objects.main);
}
