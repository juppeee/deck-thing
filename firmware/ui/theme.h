/**
 * Colours, fonts and icons of the device interface.
 * Values from the HTML draft (Spotify look); can be swapped out later.
 */
#pragma once

#include "lvgl.h"

/* ---------- Colours ---------- */
#define COL_INK      lv_color_hex(0x121212) /* background */
#define COL_SURFACE  lv_color_hex(0x282828) /* surfaces, cards */
#define COL_TEXT     lv_color_hex(0xFFFFFF)
#define COL_MUTED    lv_color_hex(0xB3B3B3)
#define COL_TRACK    lv_color_hex(0x4D4D4D) /* unplayed part of the progress */
#define COL_GREEN    lv_color_hex(0x1ED760) /* active, volume, like */

/* ---------- Fonts (Figtree, OFL) ---------- */
LV_FONT_DECLARE(fig_xb_42)        /* title */
LV_FONT_DECLARE(fig_b_40)         /* large headings */
LV_FONT_DECLARE(fig_md_30)        /* artists */
LV_FONT_DECLARE(fig_sb_28)        /* card names, volume number */
LV_FONT_DECLARE(fig_sb_24)        /* tabs, clock, messages */
LV_FONT_DECLARE(fig_md_22)        /* context, secondary text */
LV_FONT_DECLARE(fig_sb_20)        /* small cards, buttons */
LV_FONT_DECLARE(fig_md_17)        /* sub lines of small cards */
LV_FONT_DECLARE(fig_sb_clock_200) /* standby clock (digits only) */

/* ---------- Icons ---------- */
/* Material Icons (Apache 2.0): filled icons like play, pause, next, liked */
LV_FONT_DECLARE(mi_28)
LV_FONT_DECLARE(mi_44)
LV_FONT_DECLARE(mi_64)
/* Lucide (ISC): thin line icons, closer to the Spotify look for shuffle, repeat, plus, volume */
LV_FONT_DECLARE(lc_36)
LV_FONT_DECLARE(lc_28)
LV_FONT_DECLARE(lc_16)
LV_FONT_DECLARE(lc_48) /* key icons of the key page */

#define ICON_LC_SHUFFLE      "\xEE\x85\x9E" /* U+E15E shuffle */
#define ICON_LC_REPEAT       "\xEE\x85\x86" /* U+E146 repeat */
#define ICON_LC_REPEAT_ONE   "\xEE\x87\xBD" /* U+E1FD repeat-1 */
#define ICON_LC_CIRCLE_PLUS  "\xEE\x82\x81" /* U+E081 circle-plus */
#define ICON_LC_VOLUME       "\xEE\x86\xAB" /* U+E1AB volume-2 */
#define ICON_LC_VOLUME_OFF   "\xEE\x86\xAC" /* U+E1AC volume-x */
#define ICON_LC_SPARKLE      "\xEE\x91\xBE" /* U+E47E sparkle (Smart Shuffle) */
/* audio page (lc_28) */
#define ICON_LC_MIC          "\xEE\x84\x98" /* U+E118 mic */
#define ICON_LC_MIC_OFF      "\xEE\x84\x99" /* U+E119 mic-off */
#define ICON_LC_SPEAKER      "\xEE\x85\xA6" /* U+E166 speaker */
#define ICON_LC_CHEVRON_DOWN "\xEE\x81\xAD" /* U+E06D chevron-down */
#define ICON_LC_CHECK        "\xEE\x81\xAC" /* U+E06C check */
#define ICON_LC_ARROWS       "\xEE\x89\x8A" /* U+E24A arrow-left-right */

/* code points in the Unicode private use area, as UTF-8 */
#define ICON_PLAY            "\xEE\x80\xB7" /* U+E037 play_arrow */
#define ICON_PAUSE           "\xEE\x80\xB4" /* U+E034 pause */
#define ICON_PREV            "\xEE\x81\x85" /* U+E045 skip_previous */
#define ICON_NEXT            "\xEE\x81\x84" /* U+E044 skip_next */
#define ICON_SHUFFLE         "\xEE\x81\x83" /* U+E043 shuffle */
#define ICON_REPEAT          "\xEE\x81\x80" /* U+E040 repeat */
#define ICON_REPEAT_ONE      "\xEE\x81\x81" /* U+E041 repeat_one */
#define ICON_HEART           "\xEE\xA1\xBD" /* U+E87D favorite */
#define ICON_ADD_CIRCLE      "\xEE\x85\x88" /* U+E148 add_circle_outline */
#define ICON_CHECK_CIRCLE    "\xEE\xA1\xAC" /* U+E86C check_circle */
#define ICON_VOLUME          "\xEE\x81\x90" /* U+E050 volume_up */
#define ICON_VOLUME_OFF      "\xEE\x81\x8F" /* U+E04F volume_off */
#define ICON_MUSIC           "\xEE\x90\x85" /* U+E405 music_note */
#define ICON_LIBRARY         "\xEE\x80\xB0" /* U+E030 library_music */
#define ICON_GRID            "\xEE\xA6\xB0" /* U+E9B0 grid_view */
#define ICON_USB             "\xEE\x87\xA0" /* U+E1E0 usb */
#define ICON_BLUETOOTH       "\xEE\x86\xA7" /* U+E1A7 bluetooth */
#define ICON_LINK_OFF        "\xEE\x85\xAF" /* U+E16F link_off */
#define ICON_BACK            "\xEE\x97\x84" /* U+E5C4 arrow_back */
#define ICON_SPARKLE         "\xEE\x99\x9F" /* U+E65F auto_awesome (Smart Shuffle) */
#define ICON_KNOB            "\xEE\xA0\xB6" /* U+E836 radio_button_unchecked */
#define ICON_TUNE            "\xEE\x90\xA9" /* U+E429 tune */

/* ---------- Dimensions (display 800×480) ---------- */
#define SCREEN_W   800
#define SCREEN_H   480
#define TOPBAR_H   64
