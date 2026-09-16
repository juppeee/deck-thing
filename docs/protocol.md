# Device protocol (v1)

The companion app and the device exchange frames. The format is the same on
every transport — TCP to the simulator today, USB and Bluetooth on the real
board later.

```
A5 5A | type (1 byte) | length (4 bytes, little endian) | payload
```

JSON payloads are UTF-8. Image ids are 16 hex characters and are sent before
the image itself, so a list can reference images that are still on their way.

## Companion → device

| Type | Name | Payload |
|---|---|---|
| `01` | State | JSON: title, artist, context, position, duration, playing, shuffle (0 off, 1 on, 2 Smart Shuffle), repeat (0 off, 1 list, 2 track), liked, volume, cover id, background colour |
| `02` | Cover | 16-char id + JPEG (240 × 240) |
| `03` | Toast | JSON: `{"text": …}` |
| `04` | Library | JSON: playlists with name, subtitle, URI and image id |
| `05` | Artist | JSON: name, image id, following, albums |
| `06` | Image | 16-char id + JPEG (library cards, artist photos, album covers) |
| `07` | Keys | JSON: pages with name, scroll direction and keys (label, Lucide icon name, colours, optional icon id, empty slots) |
| `08` | Key icon | 16-char id + width (u16) + height (u16) + RGB565A8 pixels (all colour values first, then all alpha values) |

## Device → companion

| Type | Name | Payload |
|---|---|---|
| `10` | Hello | JSON: protocol version, firmware version, `has_knob`, display width and height |
| `11` | Command | JSON: `{"cmd": …, "value": …}` — e.g. `play_pause`, `next`, `seek`, `volume`, `like`, `library`, `artist`, `keys`, `key_press` |

## Why the device knows so little

The device is a display and an input surface. It has no idea what a key does,
where a playlist image comes from or how to talk to Spotify — the companion app
decides all of that and sends ready-made pictures and text. That keeps the
firmware small and lets new features arrive through app updates.
