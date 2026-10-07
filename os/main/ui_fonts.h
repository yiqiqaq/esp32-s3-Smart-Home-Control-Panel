#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Subset fonts generated from Noto Sans SC (plus DejaVu for the snow, house
 * and moon glyphs) with lv_font_conv; the CJK set is extracted from every
 * Chinese string in the firmware sources. Regenerate after adding new text. */
LV_FONT_DECLARE(panel_14);
LV_FONT_DECLARE(panel_18);
LV_FONT_DECLARE(panel_30);

#ifdef __cplusplus
}
#endif
