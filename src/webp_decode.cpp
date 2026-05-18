#include "webp_decode.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <webp/decode.h>

#include "ESP_Panel_Conf.h"

static void rgba_to_rgb565(const uint8_t *rgba, int width, int height, uint16_t *rgb565)
{
    const int count = width * height;
    for (int i = 0; i < count; ++i) {
        const uint8_t *p = rgba + i * 4;
        lv_color_t c = lv_color_make(p[0], p[1], p[2]);
        rgb565[i] = c.full;
    }
}

bool webp_decode_file(const char *littlefs_path, lv_img_dsc_t *out_dsc, uint8_t **out_pixel_buf)
{
    if (!littlefs_path || !out_dsc || !out_pixel_buf) {
        return false;
    }
    *out_pixel_buf = NULL;
    memset(out_dsc, 0, sizeof(*out_dsc));

    File file = LittleFS.open(littlefs_path, "r");
    if (!file) {
        Serial.printf("webp: cannot open %s\n", littlefs_path);
        return false;
    }

    const size_t file_size = file.size();
    if (file_size == 0 || file_size > 4 * 1024 * 1024) {
        Serial.printf("webp: invalid size %u for %s\n", (unsigned)file_size, littlefs_path);
        file.close();
        return false;
    }

    uint8_t *file_buf = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!file_buf) {
        file_buf = (uint8_t *)malloc(file_size);
    }
    if (!file_buf) {
        Serial.println("webp: file buffer alloc failed");
        file.close();
        return false;
    }

    if (file.read(file_buf, file_size) != file_size) {
        Serial.printf("webp: read failed %s\n", littlefs_path);
        free(file_buf);
        file.close();
        return false;
    }
    file.close();

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) {
        free(file_buf);
        return false;
    }

    if (WebPGetFeatures(file_buf, file_size, &config.input) != VP8_STATUS_OK) {
        Serial.printf("webp: invalid image %s\n", littlefs_path);
        free(file_buf);
        return false;
    }

    const int src_w = config.input.width;
    const int src_h = config.input.height;
    const int max_w = ESP_PANEL_LCD_H_RES;
    const int max_h = ESP_PANEL_LCD_V_RES;

    int out_w = src_w;
    int out_h = src_h;
    if (src_w > max_w || src_h > max_h) {
        const float scale_w = (float)max_w / (float)src_w;
        const float scale_h = (float)max_h / (float)src_h;
        const float scale = scale_w < scale_h ? scale_w : scale_h;
        out_w = (int)((float)src_w * scale);
        out_h = (int)((float)src_h * scale);
        if (out_w < 1) {
            out_w = 1;
        }
        if (out_h < 1) {
            out_h = 1;
        }
    }

    const size_t rgba_size = (size_t)out_w * (size_t)out_h * 4;
    uint8_t *rgba = (uint8_t *)heap_caps_malloc(rgba_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgba) {
        rgba = (uint8_t *)malloc(rgba_size);
    }
    if (!rgba) {
        Serial.println("webp: RGBA buffer alloc failed");
        free(file_buf);
        return false;
    }

    config.options.use_scaling = (out_w != src_w || out_h != src_h) ? 1 : 0;
    config.options.scaled_width = out_w;
    config.options.scaled_height = out_h;
    config.output.colorspace = MODE_RGBA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = rgba;
    config.output.u.RGBA.stride = out_w * 4;
    config.output.u.RGBA.size = rgba_size;

    const VP8StatusCode status = WebPDecode(file_buf, file_size, &config);
    free(file_buf);

    if (status != VP8_STATUS_OK) {
        Serial.printf("webp: decode failed (%d) %s\n", (int)status, littlefs_path);
        free(rgba);
        return false;
    }

    const size_t rgb565_bytes = (size_t)out_w * (size_t)out_h * 2;
    uint16_t *rgb565 = (uint16_t *)heap_caps_malloc(rgb565_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb565) {
        rgb565 = (uint16_t *)malloc(rgb565_bytes);
    }
    if (!rgb565) {
        Serial.println("webp: RGB565 buffer alloc failed");
        free(rgba);
        return false;
    }

    rgba_to_rgb565(rgba, out_w, out_h, rgb565);
    free(rgba);

    out_dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
    out_dsc->header.w = (uint16_t)out_w;
    out_dsc->header.h = (uint16_t)out_h;
    out_dsc->data_size = rgb565_bytes;
    out_dsc->data = (const uint8_t *)rgb565;
    *out_pixel_buf = (uint8_t *)rgb565;
    return true;
}

void webp_decode_release(uint8_t *pixel_buf)
{
    if (pixel_buf) {
        free(pixel_buf);
    }
}
