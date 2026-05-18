#pragma once

#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t nav_gesture_mask_t;

#define NAV_GESTURE_MENU_DOWN     (1u << 0)
#define NAV_GESTURE_HOME_LEFT     (1u << 1)
#define NAV_GESTURE_HOME_RIGHT    (1u << 2)
#define NAV_GESTURE_IMAGE_LEFT    (1u << 3)
#define NAV_GESTURE_IMAGE_RIGHT   (1u << 4)
#define NAV_GESTURE_COMMENT_LEFT  (1u << 5)
#define NAV_GESTURE_COMMENT_RIGHT (1u << 6)

/** Attach swipe handlers to a screen or tile (not scrollable children). */
void nav_gestures_attach(lv_obj_t *obj, nav_gesture_mask_t mask);

/** Swipe down -> menu on the home tileview (Comment | Home | Image). */
void nav_gestures_attach_tileview(lv_obj_t *tileview);

/** Bubble pointer events from descendants to the tile (skips buttons). */
void nav_gestures_enable_tree_bubble(lv_obj_t *root);

/** Let pointer events bubble to parents (for image viewer overlays). */
void nav_gestures_enable_bubble(lv_obj_t *obj);

/** Top-right close control; returns to home. */
lv_obj_t *nav_create_close_btn(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
