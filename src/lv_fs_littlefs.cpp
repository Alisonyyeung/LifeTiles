#include "lv_fs_littlefs.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <lvgl.h>

#include "image_storage.h"

static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);

    const char *rel = path;
    if (rel[0] >= 'A' && rel[0] <= 'Z' && rel[1] == ':') {
        rel += 2;
    }

    char full[128];
    if (rel[0] == '/') {
        snprintf(full, sizeof(full), "%s", rel);
    } else {
        snprintf(full, sizeof(full), "/%s", rel);
    }

    const char *fmode = (mode == LV_FS_MODE_WR) ? "w" : "r";
    File *file = new File(LittleFS.open(full, fmode));
    if (!file || !*file) {
        delete file;
        return NULL;
    }
    return file;
}

static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p)
{
    LV_UNUSED(drv);
    File *file = static_cast<File *>(file_p);
    if (file) {
        file->close();
        delete file;
    }
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    LV_UNUSED(drv);
    File *file = static_cast<File *>(file_p);
    if (!file || !*file) {
        return LV_FS_RES_FS_ERR;
    }
    *br = file->read(static_cast<uint8_t *>(buf), btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    File *file = static_cast<File *>(file_p);
    if (!file || !*file) {
        return LV_FS_RES_FS_ERR;
    }

    if (whence == LV_FS_SEEK_SET) {
        file->seek(pos, SeekSet);
    } else if (whence == LV_FS_SEEK_CUR) {
        file->seek(pos, SeekCur);
    } else if (whence == LV_FS_SEEK_END) {
        file->seek(pos, SeekEnd);
    }
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    LV_UNUSED(drv);
    File *file = static_cast<File *>(file_p);
    if (!file || !*file) {
        return LV_FS_RES_FS_ERR;
    }
    *pos_p = file->position();
    return LV_FS_RES_OK;
}

void lv_fs_littlefs_init(void)
{
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = IMAGE_FS_LETTER;
    drv.open_cb = fs_open;
    drv.close_cb = fs_close;
    drv.read_cb = fs_read;
    drv.seek_cb = fs_seek;
    drv.tell_cb = fs_tell;
    lv_fs_drv_register(&drv);
}
