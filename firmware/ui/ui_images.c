/**
 * Small picture store for playlist and artist pictures the PC app sends as JPEG.
 * Pictures are found by a 16-char id; when the store is full, the least recently used
 * picture goes – after it has been detached from every card.
 *
 * Every JPEG is decoded to RGB565 once when it arrives. Otherwise LVGL's TJPGD decodes again on
 * every redraw, and swiping through the playlists means several pictures per frame.
 */
#include <string.h>

#include "ui_internal.h"

#define IMAGE_SLOTS   32
#define ID_LEN        16
#define IMAGE_BUDGET  (3500 * 1024) /* decoded: a 264 px card ≈ 140 kB; PSRAM shares this with two frame buffers */

typedef struct {
    char id[ID_LEN + 1];
    lv_draw_buf_t * buf;
    uint32_t last_used;
} image_slot_t;

static image_slot_t slots[IMAGE_SLOTS];
static uint32_t use_counter;

lv_draw_buf_t * ui_jpeg_decode(const uint8_t * jpeg, size_t len)
{
    uint16_t w, h;
    if(!ui_jpeg_size(jpeg, len, &w, &h) || w == 0 || h == 0) return NULL;

    lv_image_dsc_t src = {
        .header.magic = LV_IMAGE_HEADER_MAGIC,
        .header.cf = LV_COLOR_FORMAT_RAW,
        .header.w = w,
        .header.h = h,
        .data_size = (uint32_t)len,
        .data = jpeg,
    };
    lv_image_decoder_dsc_t dec;
    if(lv_image_decoder_open(&dec, &src, NULL) != LV_RESULT_OK) return NULL;

    lv_draw_buf_t * out = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if(out == NULL) {
        lv_image_decoder_close(&dec);
        return NULL;
    }

    /* TJPGD delivers the picture tile by tile (one MCU per call) as BGR888 */
    const lv_area_t full = { 0, 0, w - 1, h - 1 };
    lv_area_t tile = { .y1 = LV_COORD_MIN };
    bool ok = false;
    while(lv_image_decoder_get_area(&dec, &full, &tile) == LV_RESULT_OK) {
        const lv_draw_buf_t * part = dec.decoded;
        int32_t tw = lv_area_get_width(&tile);
        int32_t th = lv_area_get_height(&tile);
        for(int32_t y = 0; y < th; y++) {
            const uint8_t * s = part->data + y * part->header.stride;
            uint16_t * d = (uint16_t *)(out->data + (tile.y1 + y) * out->header.stride + tile.x1 * 2);
            for(int32_t x = 0; x < tw; x++, s += 3) {
                *d++ = (uint16_t)(((s[2] & 0xF8) << 8) | ((s[1] & 0xFC) << 3) | (s[0] >> 3));
            }
        }
        if(tile.x2 >= w - 1 && tile.y2 >= h - 1) {
            ok = true;
            break;
        }
    }
    lv_image_decoder_close(&dec);
    if(!ok) {
        lv_draw_buf_destroy(out);
        return NULL;
    }
    return out;
}

void ui_image_free(lv_draw_buf_t * buf)
{
    if(buf == NULL) return;
    lv_image_cache_drop(buf);
    lv_draw_buf_destroy(buf);
}

const lv_image_dsc_t * ui_image_get(const char * id)
{
    if(id == NULL || id[0] == '\0') return NULL;
    for(int i = 0; i < IMAGE_SLOTS; i++) {
        if(slots[i].buf != NULL && strcmp(slots[i].id, id) == 0) {
            slots[i].last_used = ++use_counter;
            return (const lv_image_dsc_t *)slots[i].buf;
        }
    }
    return NULL;
}

static void release(image_slot_t * slot)
{
    /* detach from the cards first, then free – or a card points at freed memory */
    ui_library_image_dropped(slot->id);
    ui_artist_image_dropped(slot->id);
    ui_image_free(slot->buf);
    slot->buf = NULL;
}

static size_t used_bytes(void)
{
    size_t sum = 0;
    for(int i = 0; i < IMAGE_SLOTS; i++) {
        if(slots[i].buf != NULL) sum += slots[i].buf->data_size;
    }
    return sum;
}

static image_slot_t * oldest_slot(void)
{
    image_slot_t * oldest = NULL;
    for(int i = 0; i < IMAGE_SLOTS; i++) {
        if(slots[i].buf != NULL && (oldest == NULL || slots[i].last_used < oldest->last_used)) oldest = &slots[i];
    }
    return oldest;
}

static image_slot_t * pick_slot(const char * id, size_t need)
{
    for(int i = 0; i < IMAGE_SLOTS; i++) {
        if(slots[i].buf != NULL && strcmp(slots[i].id, id) == 0) release(&slots[i]); /* replace the same picture */
    }
    while(used_bytes() + need > IMAGE_BUDGET && oldest_slot() != NULL) release(oldest_slot());
    for(int i = 0; i < IMAGE_SLOTS; i++) {
        if(slots[i].buf == NULL) return &slots[i];
    }
    image_slot_t * oldest = oldest_slot();
    release(oldest);
    return oldest;
}

void ui_put_image(const char * id, const uint8_t * jpeg, size_t len)
{
    if(id == NULL || strlen(id) != ID_LEN) return;
    lv_draw_buf_t * buf = ui_jpeg_decode(jpeg, len);
    if(buf == NULL) return;

    image_slot_t * slot = pick_slot(id, buf->data_size);
    memcpy(slot->id, id, ID_LEN);
    slot->id[ID_LEN] = '\0';
    slot->buf = buf;
    slot->last_used = ++use_counter;

    ui_library_image_arrived(slot->id);
    ui_artist_image_arrived(slot->id);
}
