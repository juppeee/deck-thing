#include "link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "lvgl.h"
#include "src/osal/lv_os_private.h"
#include "ui.h"

#define MAX_FRAME   (1024 * 1024)

/* Rahmentypen */
#define PC_STATE    0x01
#define PC_COVER    0x02
#define PC_TOAST    0x03
#define PC_LIBRARY  0x04
#define PC_ARTIST   0x05
#define PC_IMAGE    0x06
#define PC_KEYS     0x07
#define PC_ICON     0x08
#define PC_AUDIO    0x09
#define PC_PAGES    0x0A
#define DEV_HELLO   0x10
#define DEV_CMD     0x11

#define MAX_PENDING_IMAGES 48
#define MAX_PENDING_ICONS  48

typedef struct {
    uint8_t * data;
    size_t len;
} blob_t;

static link_send_fn send_bytes;
static link_time_fn time_callback;
static char hello[192];
static lv_mutex_t inbox_mutex;
static lv_mutex_t send_mutex;

/* Ablage zwischen Transport- und LVGL-Thread (neuester Stand gewinnt, Bilder gehen nicht verloren) */
static struct {
    char * state;
    blob_t cover;
    char * toast;
    char * library;
    char * artist;
    char * keys;
    char * audio;
    char * pages;
    blob_t images[MAX_PENDING_IMAGES];
    int image_count;
    blob_t icons[MAX_PENDING_ICONS];
    int icon_count;
    int connected; /* -1 unverändert, 0 getrennt, 1 verbunden */
} inbox = { .connected = -1 };

/* Rahmen-Leser, gehört dem Transport-Thread */
static struct {
    uint8_t header[7];
    size_t header_fill;
    uint8_t * payload;
    size_t len;
    size_t fill;
} rx;

/* ---------- Senden ---------- */

static void send_frame(uint8_t type, const uint8_t * payload, size_t len)
{
    if(send_bytes == NULL) return;
    const uint8_t header[7] = { 0xA5, 0x5A, type,
                                (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24) };
    lv_mutex_lock(&send_mutex);
    send_bytes(header, sizeof(header));
    if(len > 0) send_bytes(payload, len);
    lv_mutex_unlock(&send_mutex);
}

static void send_command(const char * json)
{
    send_frame(DEV_CMD, (const uint8_t *)json, strlen(json));
}

void link_send_hello(void)
{
    send_frame(DEV_HELLO, (const uint8_t *)hello, strlen(hello));
}

/* ---------- Empfangen (Transport-Thread) ---------- */

static void replace_ptr(char ** slot, char * value)
{
    free(*slot);
    *slot = value;
}

static void queue_blob(blob_t * list, int * count, int max, uint8_t * data, size_t len)
{
    if(*count >= max) {
        free(data);
        return;
    }
    list[*count].data = data;
    list[*count].len = len;
    (*count)++;
}

/* übernimmt payload (freigeben, falls nicht abgelegt) */
static void handle_frame(uint8_t type, uint8_t * payload, size_t len)
{
    lv_mutex_lock(&inbox_mutex);
    switch(type) {
        case PC_STATE:   replace_ptr(&inbox.state, (char *)payload); break;
        case PC_TOAST:   replace_ptr(&inbox.toast, (char *)payload); break;
        case PC_LIBRARY: replace_ptr(&inbox.library, (char *)payload); break;
        case PC_ARTIST:  replace_ptr(&inbox.artist, (char *)payload); break;
        case PC_KEYS:    replace_ptr(&inbox.keys, (char *)payload); break;
        case PC_AUDIO:   replace_ptr(&inbox.audio, (char *)payload); break;
        case PC_PAGES:   replace_ptr(&inbox.pages, (char *)payload); break;
        case PC_COVER:
            free(inbox.cover.data);
            inbox.cover.data = payload;
            inbox.cover.len = len;
            break;
        case PC_IMAGE:
            if(len > 16) queue_blob(inbox.images, &inbox.image_count, MAX_PENDING_IMAGES, payload, len);
            else free(payload);
            break;
        case PC_ICON:
            if(len > 20) queue_blob(inbox.icons, &inbox.icon_count, MAX_PENDING_ICONS, payload, len);
            else free(payload);
            break;
        default:
            free(payload);
            break;
    }
    lv_mutex_unlock(&inbox_mutex);
}

static void reset_reader(void)
{
    free(rx.payload);
    memset(&rx, 0, sizeof(rx));
}

void link_feed(const uint8_t * data, size_t len)
{
    while(len > 0) {
        if(rx.payload == NULL) {
            uint8_t b = *data++;
            len--;
            /* auf den Rahmenanfang A5 5A synchronisieren */
            if(rx.header_fill == 0 && b != 0xA5) continue;
            if(rx.header_fill == 1 && b != 0x5A) {
                rx.header_fill = (b == 0xA5) ? 1 : 0;
                continue;
            }
            rx.header[rx.header_fill++] = b;
            if(rx.header_fill < sizeof(rx.header)) continue;
            rx.header_fill = 0;
            rx.len = rx.header[3] | (rx.header[4] << 8) | ((size_t)rx.header[5] << 16) | ((size_t)rx.header[6] << 24);
            if(rx.len > MAX_FRAME) continue;
            rx.payload = malloc(rx.len + 1); /* +1 für abschließende Null bei JSON */
            rx.fill = 0;
            if(rx.payload == NULL) continue;
        }
        else {
            size_t take = LV_MIN(len, rx.len - rx.fill);
            memcpy(rx.payload + rx.fill, data, take);
            rx.fill += take;
            data += take;
            len -= take;
        }
        if(rx.payload != NULL && rx.fill == rx.len) {
            rx.payload[rx.len] = 0;
            handle_frame(rx.header[2], rx.payload, rx.len);
            rx.payload = NULL;
        }
    }
}

void link_set_connected(bool connected)
{
    reset_reader();
    lv_mutex_lock(&inbox_mutex);
    inbox.connected = connected ? 1 : 0;
    lv_mutex_unlock(&inbox_mutex);
}

/* ---------- Übernehmen (LVGL-Thread) ---------- */

static const char * json_str(const cJSON * root, const char * key)
{
    const cJSON * item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static double json_num(const cJSON * root, const char * key, double fallback)
{
    const cJSON * item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static lv_color_t json_color(const cJSON * root, const char * key, uint32_t fallback)
{
    const char * value = json_str(root, key);
    return lv_color_hex(value[0] == '#' ? (uint32_t)strtoul(value + 1, NULL, 16) : fallback);
}

static void apply_state(const char * json)
{
    cJSON * root = cJSON_Parse(json);
    if(root == NULL) return;

    const cJSON * now = cJSON_GetObjectItemCaseSensitive(root, "time");
    if(time_callback != NULL && cJSON_IsNumber(now)) {
        time_callback((int64_t)now->valuedouble, (int32_t)json_num(root, "tz", 0));
    }

    if(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "session"))) {
        if(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "spotify_running")))
            ui_set_status_message("Gerade läuft nichts", "Starte einen Song in Spotify. Er erscheint dann hier.");
        else
            ui_set_status_message("Spotify ist nicht geöffnet", "Starte Spotify am PC. Der aktuelle Titel erscheint dann hier.");
        cJSON_Delete(root);
        return;
    }
    ui_set_status_message(NULL, NULL);

    ui_playback_t pb = { 0 };
    pb.title = json_str(root, "title");
    pb.artists = json_str(root, "artist");
    pb.context = json_str(root, "context");
    pb.pos_s = (float)json_num(root, "pos", 0);
    pb.dur_s = (float)json_num(root, "dur", 0);
    pb.playing = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "playing"));
    pb.shuffle = (uint8_t)json_num(root, "shuffle", 0);
    pb.repeat = (uint8_t)json_num(root, "repeat", 0);

    ui_artist_ref_t refs[6];
    const cJSON * refs_json = cJSON_GetObjectItemCaseSensitive(root, "artists_list");
    size_t ref_count = 0;
    for(int i = 0; cJSON_IsArray(refs_json) && i < cJSON_GetArraySize(refs_json) && ref_count < 6; i++) {
        const cJSON * r = cJSON_GetArrayItem(refs_json, i);
        refs[ref_count].id = json_str(r, "id");
        refs[ref_count].name = json_str(r, "name");
        ref_count++;
    }
    pb.artist_refs = ref_count ? refs : NULL;
    pb.artist_ref_count = ref_count;

    const cJSON * spotify = cJSON_GetObjectItemCaseSensitive(root, "spotify");
    pb.spotify_login = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(spotify, "login"));
    const cJSON * liked = cJSON_GetObjectItemCaseSensitive(root, "liked");
    pb.liked = cJSON_IsBool(liked) ? (cJSON_IsTrue(liked) ? 1 : 0) : -1; /* null = gerade unbekannt */

    pb.volume = (int8_t)json_num(root, "vol", -1);
    pb.muted = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "muted"));
    pb.color = json_color(root, "color", 0x333333);

    ui_set_playback(&pb);
    cJSON_Delete(root);
}

static void apply_toast(const char * json)
{
    cJSON * root = cJSON_Parse(json);
    if(root == NULL) return;
    ui_toast(json_str(root, "text"));
    cJSON_Delete(root);
}

static void apply_library(const char * json)
{
    cJSON * root = cJSON_Parse(json);
    if(root == NULL) return;
    const cJSON * items = cJSON_GetObjectItemCaseSensitive(root, "items");
    int count = cJSON_IsArray(items) ? cJSON_GetArraySize(items) : 0;
    ui_library_item_t * list = count > 0 ? calloc((size_t)count, sizeof(ui_library_item_t)) : NULL;
    size_t n = 0;
    for(int i = 0; i < count && list != NULL; i++) {
        const cJSON * it = cJSON_GetArrayItem(items, i);
        list[n].name = json_str(it, "name");
        list[n].sub = json_str(it, "sub");
        list[n].uri = json_str(it, "uri");
        list[n].image_id = json_str(it, "image_id");
        list[n].liked_songs = strcmp(json_str(it, "kind"), "liked") == 0;
        n++;
    }
    ui_set_library(list, n, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "login")));
    free(list);
    cJSON_Delete(root);
}

static void apply_artist(const char * json)
{
    cJSON * root = cJSON_Parse(json);
    if(root == NULL) return;
    const cJSON * albums = cJSON_GetObjectItemCaseSensitive(root, "albums");
    int count = cJSON_IsArray(albums) ? cJSON_GetArraySize(albums) : 0;
    ui_album_t * list = count > 0 ? calloc((size_t)count, sizeof(ui_album_t)) : NULL;
    size_t n = 0;
    for(int i = 0; i < count && list != NULL; i++) {
        const cJSON * al = cJSON_GetArrayItem(albums, i);
        list[n].name = json_str(al, "name");
        list[n].type = json_str(al, "type");
        list[n].year = json_str(al, "year");
        list[n].uri = json_str(al, "uri");
        list[n].image_id = json_str(al, "image_id");
        n++;
    }
    const cJSON * following = cJSON_GetObjectItemCaseSensitive(root, "following");
    ui_artist_t artist = {
        .id = json_str(root, "id"),
        .name = json_str(root, "name"),
        .image_id = json_str(root, "image_id"),
        .following = cJSON_IsBool(following) ? (cJSON_IsTrue(following) ? 1 : 0) : -1,
        .albums = list,
        .album_count = n,
    };
    ui_set_artist(&artist);
    free(list);
    cJSON_Delete(root);
}

static void apply_keys(const char * json)
{
    cJSON * root = cJSON_Parse(json);
    if(root == NULL) return;
    const cJSON * pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    const cJSON * page = cJSON_IsArray(pages) ? cJSON_GetArrayItem(pages, 0) : NULL; /* vorerst eine Seite */
    const cJSON * keys = page ? cJSON_GetObjectItemCaseSensitive(page, "keys") : NULL;
    int count = cJSON_IsArray(keys) ? cJSON_GetArraySize(keys) : 0;
    ui_key_t * list = count > 0 ? calloc((size_t)count, sizeof(ui_key_t)) : NULL;
    size_t n = 0;
    for(int i = 0; i < count && list != NULL; i++) {
        const cJSON * k = cJSON_GetArrayItem(keys, i);
        list[n].label = json_str(k, "label");
        list[n].icon = json_str(k, "icon");
        list[n].icon_id = json_str(k, "icon_id");
        list[n].icon_fill = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(k, "icon_fill"));
        list[n].color = json_color(k, "color", 0xFFFFFF);
        list[n].text_color = json_color(k, "text_color", 0xFFFFFF);
        list[n].empty = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(k, "empty"));
        n++;
    }
    ui_set_keys(page ? json_str(page, "name") : "", page && strcmp(json_str(page, "scroll"), "vertical") == 0, list, n);
    free(list);
    cJSON_Delete(root);
}

static void audio_channel(const cJSON * j, ui_audio_channel_t * ch)
{
    ch->id = json_str(j, "id");
    ch->name = json_str(j, "name");
    ch->sub = json_str(j, "sub");
    ch->icon_id = json_str(j, "icon_id");
    ch->color = json_color(j, "color", 0x6A6A6A);
    ch->volume = (int8_t)json_num(j, "vol", 0);
    ch->muted = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "muted"));
    ch->level = (uint8_t)json_num(j, "level", 0);
}

static void apply_audio(const char * json)
{
    cJSON * root = cJSON_Parse(json);
    if(root == NULL) return;
    ui_audio_channel_t out, mic;
    const cJSON * out_json = cJSON_GetObjectItemCaseSensitive(root, "out");
    const cJSON * mic_json = cJSON_GetObjectItemCaseSensitive(root, "mic");
    if(cJSON_IsObject(out_json)) audio_channel(out_json, &out);
    if(cJSON_IsObject(mic_json)) audio_channel(mic_json, &mic);

    const cJSON * apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
    int app_count = cJSON_IsArray(apps) ? cJSON_GetArraySize(apps) : 0;
    ui_audio_channel_t * app_list = app_count > 0 ? calloc((size_t)app_count, sizeof(ui_audio_channel_t)) : NULL;
    size_t n = 0;
    for(int i = 0; i < app_count && app_list != NULL; i++) audio_channel(cJSON_GetArrayItem(apps, i), &app_list[n++]);

    const cJSON * outputs = cJSON_GetObjectItemCaseSensitive(root, "outputs");
    int out_count = cJSON_IsArray(outputs) ? cJSON_GetArraySize(outputs) : 0;
    ui_audio_output_t * out_list = out_count > 0 ? calloc((size_t)out_count, sizeof(ui_audio_output_t)) : NULL;
    size_t m = 0;
    for(int i = 0; i < out_count && out_list != NULL; i++) {
        const cJSON * o = cJSON_GetArrayItem(outputs, i);
        out_list[m].id = json_str(o, "id");
        out_list[m].name = json_str(o, "name");
        out_list[m].active = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "active"));
        m++;
    }

    ui_audio_t audio = {
        .list_layout = strcmp(json_str(root, "layout"), "list") == 0,
        .out = cJSON_IsObject(out_json) ? &out : NULL,
        .mic = cJSON_IsObject(mic_json) ? &mic : NULL,
        .apps = app_list,
        .app_count = n,
        .outputs = out_list,
        .output_count = m,
    };
    ui_set_audio(&audio);
    free(app_list);
    free(out_list);
    cJSON_Delete(root);
}

static bool page_on(const cJSON * pages, const char * name)
{
    const cJSON * item = cJSON_GetObjectItemCaseSensitive(pages, name);
    return !cJSON_IsBool(item) || cJSON_IsTrue(item); /* fehlt = sichtbar */
}

static void apply_pages(const char * json)
{
    cJSON * root = cJSON_Parse(json);
    if(root == NULL) return;
    ui_set_pages(page_on(root, "music"), page_on(root, "playlists"), page_on(root, "keys"), page_on(root, "audio"));
    cJSON_Delete(root);
}

static void apply_timer(lv_timer_t * t)
{
    LV_UNUSED(t);
    lv_mutex_lock(&inbox_mutex);
    char * state = inbox.state;
    blob_t cover = inbox.cover;
    char * toast = inbox.toast;
    char * library = inbox.library;
    char * artist = inbox.artist;
    char * keys = inbox.keys;
    char * audio = inbox.audio;
    char * pages = inbox.pages;
    int image_count = inbox.image_count;
    blob_t images[MAX_PENDING_IMAGES];
    memcpy(images, inbox.images, sizeof(images[0]) * (size_t)image_count);
    int icon_count = inbox.icon_count;
    blob_t icons[MAX_PENDING_ICONS];
    memcpy(icons, inbox.icons, sizeof(icons[0]) * (size_t)icon_count);
    int connected = inbox.connected;
    memset(&inbox, 0, sizeof(inbox));
    inbox.connected = -1;
    lv_mutex_unlock(&inbox_mutex);

    if(connected >= 0) ui_set_connected(connected == 1);
    if(pages != NULL) apply_pages(pages);
    if(state != NULL) apply_state(state);
    if(cover.data != NULL && cover.len > 16) ui_set_cover_jpeg(cover.data + 16, cover.len - 16); /* 16 Zeichen Kennung vorweg */
    if(toast != NULL) apply_toast(toast);
    if(library != NULL) apply_library(library);
    if(artist != NULL) apply_artist(artist);
    if(keys != NULL) apply_keys(keys);
    if(audio != NULL) apply_audio(audio);
    for(int i = 0; i < image_count; i++) {
        char id[17];
        memcpy(id, images[i].data, 16);
        id[16] = '\0';
        ui_put_image(id, images[i].data + 16, images[i].len - 16);
        free(images[i].data);
    }
    /* Tastensymbol: 16 Zeichen Kennung | Breite u16 | Höhe u16 | RGB565A8 – nach apply_keys, damit die Tasten schon stehen */
    for(int i = 0; i < icon_count; i++) {
        char id[17];
        memcpy(id, icons[i].data, 16);
        id[16] = '\0';
        uint16_t w = (uint16_t)(icons[i].data[16] | (icons[i].data[17] << 8));
        uint16_t h = (uint16_t)(icons[i].data[18] | (icons[i].data[19] << 8));
        ui_put_key_icon(id, w, h, icons[i].data + 20, icons[i].len - 20);
        ui_put_audio_icon(id, w, h, icons[i].data + 20, icons[i].len - 20); /* gleiche Form, jede Seite nimmt ihre */
        free(icons[i].data);
    }

    free(state);
    free(cover.data);
    free(toast);
    free(library);
    free(artist);
    free(keys);
    free(audio);
    free(pages);
}

void link_init(link_send_fn send, link_time_fn on_time, const char * hello_json)
{
    send_bytes = send;
    time_callback = on_time;
    snprintf(hello, sizeof(hello), "%s", hello_json);
    lv_mutex_init(&inbox_mutex);
    lv_mutex_init(&send_mutex);
    ui_set_command_handler(send_command);
    lv_timer_create(apply_timer, 30, NULL);
}
