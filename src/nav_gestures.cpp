#include "nav_gestures.h"

#include <Arduino.h>
#include <stdlib.h>

#include "app_theme.h"
#include "menu_screen.h"
#include "screen_nav.h"

#define SWIPE_AXIS_MIN_PX  72
#define SWIPE_DOM_RATIO    2

typedef struct {
    nav_gesture_mask_t mask;
    lv_point_t start;
} nav_gesture_ctx_t;

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static void on_close_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    screen_nav_show_home();
}

static bool swipe_h(int dx, int dy)
{
    return iabs(dx) > SWIPE_AXIS_MIN_PX && iabs(dx) > iabs(dy) * SWIPE_DOM_RATIO;
}

static bool swipe_v(int dx, int dy)
{
    return dy > SWIPE_AXIS_MIN_PX && dy > iabs(dx) * SWIPE_DOM_RATIO;
}

static void handle_gesture(nav_gesture_ctx_t *ctx, int dx, int dy)
{
    if ((ctx->mask & NAV_GESTURE_MENU_DOWN) && swipe_v(dx, dy)) {
        if (!menu_screen_is_active()) {
            menu_screen_show();
        }
        return;
    }
    if (((ctx->mask & NAV_GESTURE_HOME_LEFT) && dx < 0 && swipe_h(dx, dy)) || ((ctx->mask & NAV_GESTURE_HOME_RIGHT) && dx > 0 && swipe_h(dx, dy))) {
        screen_nav_show_home();
        return;
    }
    if (((ctx->mask & NAV_GESTURE_IMAGE_LEFT) && dx < 0 && swipe_h(dx, dy)) || ((ctx->mask & NAV_GESTURE_IMAGE_RIGHT) && dx > 0 && swipe_h(dx, dy))) {
        screen_nav_show_image();
        return;
    }
    if (((ctx->mask & NAV_GESTURE_COMMENT_LEFT) && dx < 0 && swipe_h(dx, dy)) || ((ctx->mask & NAV_GESTURE_COMMENT_RIGHT) && dx > 0 && swipe_h(dx, dy))) {
        screen_nav_show_comment();
    }
}

static void nav_gesture_delete(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        lv_mem_free(lv_event_get_user_data(e));
    }
}

static void nav_gesture_event(lv_event_t *e)
{
    nav_gesture_ctx_t *ctx = (nav_gesture_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev) {
            lv_indev_get_point(indev, &ctx->start);
        }
        return;
    }

    if (code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) {
            return;
        }
        const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_BOTTOM && (ctx->mask & NAV_GESTURE_MENU_DOWN)) {
            if (!menu_screen_is_active()) {
                menu_screen_show();
            }
            return;
        }
        if ((dir == LV_DIR_LEFT && (ctx->mask & NAV_GESTURE_HOME_LEFT)) || (dir == LV_DIR_RIGHT && (ctx->mask & NAV_GESTURE_HOME_RIGHT))) {
            screen_nav_show_home();
            return;
        }
        if ((dir == LV_DIR_LEFT && (ctx->mask & NAV_GESTURE_IMAGE_LEFT)) || (dir == LV_DIR_RIGHT && (ctx->mask & NAV_GESTURE_IMAGE_RIGHT))) {
            screen_nav_show_image();
            return;
        }
        if ((dir == LV_DIR_LEFT && (ctx->mask & NAV_GESTURE_COMMENT_LEFT)) || (dir == LV_DIR_RIGHT && (ctx->mask & NAV_GESTURE_COMMENT_RIGHT))) {
            screen_nav_show_comment();
        }
        return;
    }

    if (code != LV_EVENT_RELEASED) {
        return;
    }

    lv_point_t end;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_indev_get_point(indev, &end);
    handle_gesture(ctx, end.x - ctx->start.x, end.y - ctx->start.y);
}

void nav_gestures_enable_bubble(lv_obj_t *obj)
{
    if (obj) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

void nav_gestures_enable_tree_bubble(lv_obj_t *root)
{
    if (!root) {
        return;
    }
    const uint32_t n = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(root, i);
        if (lv_obj_check_type(ch, &lv_btn_class)) {
            continue;
        }
        nav_gestures_enable_bubble(ch);
        nav_gestures_enable_tree_bubble(ch);
    }
}

void nav_gestures_attach_tileview(lv_obj_t *tileview)
{
    nav_gestures_attach(tileview, NAV_GESTURE_MENU_DOWN);
}

void nav_gestures_attach(lv_obj_t *obj, nav_gesture_mask_t mask)
{
    if (!obj || mask == 0) {
        return;
    }
    nav_gesture_ctx_t *ctx = (nav_gesture_ctx_t *)lv_mem_alloc(sizeof(nav_gesture_ctx_t));
    if (!ctx) {
        return;
    }
    ctx->mask = mask;
    lv_obj_add_event_cb(obj, nav_gesture_event, LV_EVENT_ALL, ctx);
    lv_obj_add_event_cb(obj, nav_gesture_delete, LV_EVENT_DELETE, ctx);
}

lv_obj_t *nav_create_close_btn(lv_obj_t *parent)
{
    if (!parent) {
        return NULL;
    }
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 48, 48);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_obj_set_style_radius(btn, 24, LV_PART_MAIN);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl);
    app_theme_style_icon_btn(btn, lbl);
    lv_obj_add_event_cb(btn, on_close_clicked, LV_EVENT_CLICKED, NULL);
    return btn;
}
