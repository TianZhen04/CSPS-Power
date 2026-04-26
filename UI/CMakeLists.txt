SET(SOURCES screens/ui_Screen1.c
    screens/ui_Screen2.c
    screens/ui_Screen3.c
    screens/ui_Screen4.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_546246141.c
    images/ui_img_808252844.c
    images/ui_img_1177623285.c
    images/ui_img_1259645767.c
    images/ui_img_252039257.c
    fonts/ui_font_bold16.c
    fonts/ui_font_bold20.c
    fonts/ui_font_bold24.c
    fonts/ui_font_fuhao.c
    fonts/ui_font_jetBrains24.c)

add_library(ui ${SOURCES})
