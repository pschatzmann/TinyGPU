#pragma once

// This project doesn't use LVGL's Kconfig/template lv_conf.h - it only
// needs to enable a couple of larger built-in Montserrat font sizes for
// better legibility on the physical panel than the default 14px. Every
// other LVGL option not set here falls back to the library's own default
// (see lv_conf_internal.h), since lv_conf.h only needs to override what
// it actually cares about.
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
