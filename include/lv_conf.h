#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* Enable logging through Serial if needed. */
#define LV_USE_LOG 0

/* Use built-in allocator first. Can be switched to SPIRAM later. */
#define LV_MEM_CUSTOM 0

/* Keep core widgets enabled for quick bring-up. */
#define LV_USE_LABEL 1
#define LV_USE_BTN 1

/* Default font */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_20 0

/* Enable float formatting in lv_snprintf, required by UI labels using %f. */
#define LV_SPRINTF_USE_FLOAT 1

#endif /* LV_CONF_H */
