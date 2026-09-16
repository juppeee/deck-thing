"""Spotify-Anmeldung und Web-API.

Anmeldung per Authorization Code mit PKCE: kein Client-Secret, nur die Client-ID
der eigenen Entwickler-App. Seit Februar 2026 braucht der Besitzer der App
Spotify Premium, und eine App darf höchstens 5 Nutzer haben.

Zugangsdaten liegen in ~/.deck-thing/spotify.json, nie im Projektordner.
"""

from __future__ import annotations

import asyncio
import base64
import hashlib
import json
import logging
import secrets
import time
from pathlib import Path
from urllib.parse import urlencode

import aiohttp

from paths import DATA_DIR

API = "https://api.spotify.com/v1"
ACCOUNTS = "https://accounts.spotify.com"
REDIRECT_URI = "http://127.0.0.1:8765/callback"  # "localhost" lässt Spotify seit 2025 nicht mehr zu
SCOPES = " ".join([
    "user-read-playback-state",
    "user-modify-playback-state",
    "user-read-currently-playing",
    "user-library-read",
    "user-library-modify",
    "playlist-read-private",
    "playlist-read-collaborative",
    "user-follow-read",    # folge ich dem Künstler? (GET /me/library/contains mit Künstler-URI)
    "user-follow-modify",  # Künstler folgen/entfolgen (PUT/DELETE /me/library)
])
CONFIG_FILE = DATA_DIR / "spotify.json"

log = logging.getLogger("bridge.spotify")


class SpotifyError(Exception):
    def __init__(self, status: int, detail: str) -> None:
        super().__init__(f"Spotify {status}: {detail}")
        self.status = status


class SpotifyAPI:
    def __init__(self) -> None:
        self._cfg: dict = self._load()
        self._pending: dict = {}
        self._session: aiohttp.ClientSession | None = None
        self._refresh_lock = asyncio.Lock()
        self._cooldown_until = 0.0

    # ---------- Konfiguration ----------

    @staticmethod
    def _load() -> dict:
        try:
            return json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}

    def _save(self) -> None:
        CONFIG_FILE.parent.mkdir(parents=True, exist_ok=True)
        CONFIG_FILE.write_text(json.dumps(self._cfg, indent=2), encoding="utf-8")

    @property
    def client_id(self) -> str:
        return self._cfg.get("client_id", "")

    @property
    def logged_in(self) -> bool:
        return bool(self._cfg.get("refresh_token"))

    def has_scope(self, scope: str) -> bool:
        return scope in self._cfg.get("scope", "").split()

    def logout(self) -> None:
        for key in ("access_token", "refresh_token", "expires_at", "me", "scope"):
            self._cfg.pop(key, None)
        self._save()

    async def close(self) -> None:
        if self._session is not None:
            await self._session.close()

    async def _http(self) -> aiohttp.ClientSession:
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=10))
        return self._session

    # ---------- Anmeldung (PKCE) ----------

    def login_url(self, client_id: str) -> str:
        self._cfg["client_id"] = client_id.strip()
        self._save()
        verifier = secrets.token_urlsafe(64)[:100]
        challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
        state = secrets.token_urlsafe(16)
        self._pending = {"verifier": verifier, "state": state}
        return f"{ACCOUNTS}/authorize?" + urlencode({
            "client_id": self.client_id,
            "response_type": "code",
            "redirect_uri": REDIRECT_URI,
            "code_challenge_method": "S256",
            "code_challenge": challenge,
            "scope": SCOPES,
            "state": state,
        })

    async def exchange_code(self, code: str, state: str) -> None:
        if not self._pending or state != self._pending.get("state"):
            raise SpotifyError(400, "Anmeldung passt nicht zur Anfrage, bitte neu starten")
        await self._token_request({
            "grant_type": "authorization_code",
            "code": code,
            "redirect_uri": REDIRECT_URI,
            "client_id": self.client_id,
            "code_verifier": self._pending["verifier"],
        })
        self._pending = {}

    async def _token_request(self, data: dict) -> None:
        session = await self._http()
        async with session.post(f"{ACCOUNTS}/api/token", data=data) as resp:
            body = await resp.json(content_type=None)
            if resp.status != 200:
                raise SpotifyError(resp.status, str(body))
        self._cfg["access_token"] = body["access_token"]
        self._cfg["expires_at"] = time.time() + int(body.get("expires_in", 3600))
        if body.get("scope"):  # erteilte Berechtigungen merken; ältere Anmeldungen haben weniger
            self._cfg["scope"] = body["scope"]
        if body.get("refresh_token"):  # Spotify rotiert den Refresh-Token manchmal
            self._cfg["refresh_token"] = body["refresh_token"]
        self._save()

    async def _access_token(self) -> str | None:
        if not self.logged_in:
            return None
        async with self._refresh_lock:
            if time.time() > self._cfg.get("expires_at", 0) - 60:
                try:
                    await self._token_request({
                        "grant_type": "refresh_token",
                        "refresh_token": self._cfg["refresh_token"],
                        "client_id": self.client_id,
                    })
                except SpotifyError as err:
                    if err.status in (400, 401):  # Zugang widerrufen
                        log.warning("Spotify-Anmeldung abgelaufen, bitte neu verbinden")
                        self.logout()
                    raise
        return self._cfg.get("access_token")

    # ---------- API ----------

    async def _request(self, method: str, path: str, *, params: dict | None = None,
                       json_body: dict | None = None, retry: bool = True):
        if time.monotonic() < self._cooldown_until:
            raise SpotifyError(429, "Ratenlimit, kurz warten")
        token = await self._access_token()
        if token is None:
            raise SpotifyError(401, "nicht angemeldet")
        session = await self._http()
        async with session.request(method, API + path, params=params, json=json_body,
                                   headers={"Authorization": f"Bearer {token}"}) as resp:
            if resp.status == 401 and retry:
                self._cfg["expires_at"] = 0
                return await self._request(method, path, params=params, json_body=json_body, retry=False)
            if resp.status == 429:
                wait = int(resp.headers.get("Retry-After", "5"))
                self._cooldown_until = time.monotonic() + wait
                raise SpotifyError(429, f"Ratenlimit, {wait} s warten")
            text = await resp.text()
            if resp.status >= 400:
                raise SpotifyError(resp.status, text[:300])
            return json.loads(text) if text.strip() else None

    async def player(self) -> dict | None:
        """Wiedergabestand; None, wenn gerade kein Gerät aktiv ist (204)."""
        return await self._request("GET", "/me/player")

    async def set_volume(self, percent: int) -> None:
        await self._request("PUT", "/me/player/volume", params={"volume_percent": max(0, min(100, int(percent)))})

    async def is_saved(self, uri: str) -> bool:
        result = await self._request("GET", "/me/library/contains", params={"uris": uri})
        return bool(result and result[0])

    async def set_saved(self, uri: str, saved: bool) -> None:
        await self._request("PUT" if saved else "DELETE", "/me/library", params={"uris": uri})

    async def playlists(self, limit: int = 20) -> list[dict]:
        data = await self._request("GET", "/me/playlists", params={"limit": limit})
        items = []
        for pl in (data or {}).get("items", []):
            if not pl:
                continue
            images = pl.get("images") or []
            items.append({
                "name": pl.get("name", ""),
                "sub": (pl.get("owner") or {}).get("display_name") or "Playlist",
                "image": images[0]["url"] if images else None,
                "uri": pl.get("uri"),
            })
        return items

    async def play_context(self, uri: str) -> None:
        await self._request("PUT", "/me/player/play", json_body={"context_uri": uri})

    async def artist(self, artist_id: str) -> dict:
        """Einzelner Künstler (Name, Bilder). Top-Songs und Follower gibt es seit Februar 2026 nicht mehr."""
        return await self._request("GET", f"/artists/{artist_id}") or {}

    async def artist_albums(self, artist_id: str) -> list[dict]:
        params = {"include_groups": "album,single", "limit": 20}
        try:
            data = await self._request("GET", f"/artists/{artist_id}/albums", params=params) or {}
        except SpotifyError as err:
            if err.status != 400:
                raise
            params["limit"] = 10  # falls Spotify die Obergrenze gesenkt hat
            data = await self._request("GET", f"/artists/{artist_id}/albums", params=params) or {}
        types = {"album": "Album", "single": "Single", "compilation": "Compilation"}
        albums = []
        for al in data.get("items", []):
            if not al:
                continue
            images = al.get("images") or []
            image = (images[1] if len(images) > 1 else images[0])["url"] if images else None
            albums.append({
                "name": al.get("name", ""),
                "type": types.get(al.get("album_type", ""), "Album"),
                "year": (al.get("release_date") or "")[:4],
                "image": image,
                "uri": al.get("uri"),
            })
        return albums

    async def playlist_name(self, playlist_id: str) -> str:
        data = await self._request("GET", f"/playlists/{playlist_id}", params={"fields": "name"}) or {}
        return data.get("name", "")

    async def me(self) -> dict:
        """Eigenes Profil; id und display_name gibt es auch nach dem Umbau im Februar 2026 noch."""
        if "me" not in self._cfg:
            data = await self._request("GET", "/me") or {}
            self._cfg["me"] = {"id": data.get("id", ""), "name": data.get("display_name") or data.get("id", "")}
            self._save()
        return self._cfg["me"]

    async def play_liked(self) -> None:
        """Lieblingssongs abspielen. Spotify führt sie nicht als Playlist; der Kontext
        spotify:user:<id>:collection wird vom Player akzeptiert. Klappt das nicht,
        die neuesten 50 gelikten Songs als Liste starten."""
        me = await self.me()
        try:
            await self.play_context(f"spotify:user:{me['id']}:collection")
            return
        except SpotifyError as err:
            log.info("Lieblingssongs als Kontext abgelehnt (%s), spiele die neuesten 50", err)
        data = await self._request("GET", "/me/tracks", params={"limit": 50}) or {}
        uris = [it["track"]["uri"] for it in data.get("items", []) if it.get("track")]
        if uris:
            await self._request("PUT", "/me/player/play", json_body={"uris": uris})
