#include "jpg_decode.h"

#include <Arduino.h>
#include <FS.h>
#include <JPEGDEC.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "ESP_Panel_Conf.h"

static JPEGDEC s_jpeg;
static uint16_t *s_out_buf = NULL;
static int s_out_w = 0;
static int s_out_h = 0;

static int jpeg_draw_cb(JPEGDRAW *pDraw)
{
    if (!s_out_buf || s_out_w <= 0) {
        return 0;
    }
    uint16_t *dst = s_out_buf + (pDraw->y * s_out_w) + pDraw->x;
    for (int row = 0; row < pDraw->iHeight; ++row) {
        memcpy(dst + row * s_out_w, pDraw->pPixels + row * pDraw->iWidth, (size_t)pDraw->iWidth * 2);
    }
    return 1;
}

static int scale_to_denominator(int scale_flag)
{
    if (scale_flag & JPEG_SCALE_EIGHTH) {
        return 8;
    }
    if (scale_flag & JPEG_SCALE_QUARTER) {
        return 4;
    }
    if (scale_flag & JPEG_SCALE_HALF) {
        return 2;
    }
    return 1;
}

static int pick_jpeg_scale(int src_w, int src_h, int max_w, int max_h)
{
    if (src_w <= max_w && src_h <= max_h) {
        return 0;
    }
    const int dens[] = {2, 4, 8};
    for (unsigned i = 0; i < sizeof(dens) / sizeof(dens[0]); ++i) {
        const int den = dens[i];
        const int w = (src_w + den - 1) / den;
        const int h = (src_h + den - 1) / den;
        if (w <= max_w && h <= max_h) {
            if (den == 2) {
                return JPEG_SCALE_HALF;
            }
            if (den == 4) {
                return JPEG_SCALE_QUARTER;
            }
            return JPEG_SCALE_EIGHTH;
        }
    }
    return JPEG_SCALE_EIGHTH;
}

/** Decode baseline JPEG in RAM to RGB565. Does not free `jpeg`. */
static bool decode_jpeg_bytes(const uint8_t *jpeg, size_t jpeg_len, lv_img_dsc_t *out_dsc, uint8_t **out_pixel_buf)
{
    if (!jpeg || jpeg_len == 0 || !out_dsc || !out_pixel_buf) {
        return false;
    }
    *out_pixel_buf = NULL;
    memset(out_dsc, 0, sizeof(*out_dsc));

    if (s_jpeg.openRAM((uint8_t *)jpeg, (int)jpeg_len, jpeg_draw_cb) != 1) {
        return false;
    }

    if (s_jpeg.getJPEGType() == JPEG_MODE_PROGRESSIVE) {
        Serial.println("jpg: progressive JPEG not supported (re-save as baseline)");
        s_jpeg.close();
        return false;
    }

    const int src_w = s_jpeg.getWidth();
    const int src_h = s_jpeg.getHeight();
    if (src_w <= 0 || src_h <= 0) {
        s_jpeg.close();
        return false;
    }

    const int max_w = ESP_PANEL_LCD_H_RES;
    const int max_h = ESP_PANEL_LCD_V_RES;
    const int scale_flag = pick_jpeg_scale(src_w, src_h, max_w, max_h);
    const int den = scale_to_denominator(scale_flag);
    const int out_w = (src_w + den - 1) / den;
    const int out_h = (src_h + den - 1) / den;
    const size_t rgb565_bytes = (size_t)out_w * (size_t)out_h * 2;

    uint16_t *rgb565 = (uint16_t *)heap_caps_malloc(rgb565_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb565) {
        rgb565 = (uint16_t *)malloc(rgb565_bytes);
    }
    if (!rgb565) {
        s_jpeg.close();
        return false;
    }

    s_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    s_out_buf = rgb565;
    s_out_w = out_w;
    s_out_h = out_h;

    if (s_jpeg.decode(0, 0, scale_flag) != 1) {
        s_out_buf = NULL;
        s_out_w = 0;
        s_out_h = 0;
        s_jpeg.close();
        heap_caps_free(rgb565);
        return false;
    }

    s_out_buf = NULL;
    s_out_w = 0;
    s_out_h = 0;
    s_jpeg.close();

    out_dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
    out_dsc->header.w = (uint16_t)out_w;
    out_dsc->header.h = (uint16_t)out_h;
    out_dsc->data_size = rgb565_bytes;
    out_dsc->data = (const uint8_t *)rgb565;
    *out_pixel_buf = (uint8_t *)rgb565;
    return true;
}

bool jpg_decode_memory(const uint8_t *jpeg, size_t jpeg_len, lv_img_dsc_t *out_dsc, uint8_t **out_pixel_buf)
{
    return decode_jpeg_bytes(jpeg, jpeg_len, out_dsc, out_pixel_buf);
}

bool jpg_decode_file(const char *littlefs_path, lv_img_dsc_t *out_dsc, uint8_t **out_pixel_buf)
{
    if (!littlefs_path || !out_dsc || !out_pixel_buf) {
        return false;
    }
    *out_pixel_buf = NULL;
    memset(out_dsc, 0, sizeof(*out_dsc));

    File file = LittleFS.open(littlefs_path, "r");
    if (!file) {
        Serial.printf("jpg: cannot open %s\n", littlefs_path);
        return false;
    }

    const size_t file_size = file.size();
    if (file_size == 0 || file_size > 4 * 1024 * 1024) {
        Serial.printf("jpg: invalid size %u for %s\n", (unsigned)file_size, littlefs_path);
        file.close();
        return false;
    }

    uint8_t *file_buf = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!file_buf) {
        file_buf = (uint8_t *)malloc(file_size);
    }
    if (!file_buf) {
        Serial.println("jpg: file buffer alloc failed");
        file.close();
        return false;
    }

    if (file.read(file_buf, file_size) != file_size) {
        Serial.printf("jpg: read failed %s\n", littlefs_path);
        free(file_buf);
        file.close();
        return false;
    }
    file.close();

    if (!decode_jpeg_bytes(file_buf, file_size, out_dsc, out_pixel_buf)) {
        Serial.printf("jpg: decode failed %s\n", littlefs_path);
        free(file_buf);
        return false;
    }

    free(file_buf);
    Serial.printf("jpg: decoded %s -> %ux%u\n", littlefs_path, out_dsc->header.w, out_dsc->header.h);
    return true;
}

void jpg_decode_release(uint8_t *pixel_buf)
{
    if (pixel_buf) {
        heap_caps_free(pixel_buf);
    }
}
