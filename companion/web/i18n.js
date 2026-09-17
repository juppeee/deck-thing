/*
 * Interface translation (German → English), same idea as in the Claude Session Browser:
 * HTML and scripts contain the German sentence. A pass over the finished tree replaces every text
 * node and the attributes title/placeholder/aria-label found in the table – including anything the
 * script inserts later (MutationObserver). Without a translation the German stays.
 *
 * - data-raw: never translate (song titles, key names, paths – a key called "Suche" stays "Suche").
 * - data-i18n-html: translate a sentence with <b>/<a> as one block, or it falls apart into fragments.
 * - Values inside a sentence: t("… {n} …", { n }) in the script.
 * tools/collect_i18n.py finds missing sentences (it runs in build.bat).
 */
(() => {
  const EN = {
/*EN-START*/
    "Deck Thing": "Deck Thing",
    "Deck Thing – Tasten": "Deck Thing – Keys",
    "Übersicht": "Overview",
    "Tasten": "Keys",
    "Spotify": "Spotify",
    "Einstellungen": "Settings",
    "Version –": "Version –",
    "Update verfügbar": "Update available",
    "Was gerade läuft und ob alles verbunden ist.": "What's playing and whether everything is connected.",
    "Gerät": "Device",
    "Extras verbinden": "Connect extras",
    "Tasten bearbeiten": "Edit keys",
    "Verbunden": "Connected",
    "mit Knauf": "with knob",
    "ohne Knauf": "without knob",
    "Kein Gerät verbunden": "No device connected",
    "Gerät per USB-Kabel anschließen.": "Connect the device with a USB cable.",
    "Spielt gerade": "Playing",
    "Pausiert": "Paused",
    "Extras verbunden": "Extras connected",
    "Extras verbunden als {name}": "Extras connected as {name}",
    "Ohne Extras (nicht angemeldet)": "Without extras (not signed in)",
    "Spotify ist offen": "Spotify is open",
    "Gerade läuft nichts.": "Nothing is playing right now.",
    "Spotify ist nicht geöffnet": "Spotify isn't open",
    "Desktop-App und Microsoft-Store-Version gehen beide.": "Both the desktop app and the Microsoft Store version work.",
    "1 Taste belegt": "1 key assigned",
    "{n} Tasten belegt": "{n} keys assigned",
    "{n} Felder": "{n} slots",
    "Tasten nicht gespeichert": "Keys not saved",
    "Du hast die Tastenbelegung geändert.": "You changed the key layout.",
    "Vor dem Wechseln speichern?": "Save before switching?",
    "Vor dem Beenden speichern?": "Save before quitting?",
    "Vor dem Aktualisieren speichern?": "Save before updating?",
    "Abbrechen": "Cancel",
    "Verwerfen": "Discard",
    "Speichern": "Save",
    "Titel, Cover und Steuerung laufen ohne Anmeldung. Für Like, deine Bibliothek, Smart Shuffle und Spotifys eigenen Lautstärkeregler verbindest du dein Konto einmal.": "Titles, covers and playback controls work without signing in. For likes, your library, Smart Shuffle and Spotify's own volume control, connect your account once.",
    "Verbunden als {name}": "Connected as {name}",
    "Like, Bibliothek, Smart Shuffle und Lautstärke sind aktiv.": "Likes, library, Smart Shuffle and volume are active.",
    "Trennen": "Disconnect",
    "<b>Spotify-Entwicklerseite öffnen</b> und mit deinem Konto anmelden. Voraussetzung ist Spotify Premium.": "<b>Open the Spotify developer site</b> and sign in with your account. Spotify Premium is required.",
    "developer.spotify.com öffnen": "Open developer.spotify.com",
    "<b>„Create app“</b> klicken. Name und Beschreibung sind egal, z. B. „Deck Thing“.": "Click <b>“Create app”</b>. Name and description don't matter, e.g. “Deck Thing”.",
    "Diese <b>Redirect URI</b> eintragen und auf „Add“ klicken:": "Enter this <b>Redirect URI</b> and click “Add”:",
    "Kopieren": "Copy",
    "Bei „Which API/SDKs“ <b>Web API</b> ankreuzen, Bedingungen annehmen, speichern.": "Under “Which API/SDKs” tick <b>Web API</b>, accept the terms, save.",
    "<b>Client ID</b> von der Seite der App kopieren und hier einfügen:": "Copy the <b>Client ID</b> from the app's page and paste it here:",
    "32 Zeichen, z. B. 3f9a…": "32 characters, e.g. 3f9a…",
    "Mit Spotify verbinden": "Connect to Spotify",
    "Warte auf die Anmeldung im Browser …": "Waiting for sign-in in the browser …",
    "Ein Client-Secret wird nicht gebraucht. Die Zugangsdaten bleiben auf diesem PC.": "No client secret needed. The credentials stay on this PC.",
    "Sieht richtig aus.": "Looks right.",
    "{n} von 32 Zeichen – nur 0–9 und a–f.": "{n} of 32 characters – only 0–9 and a–f.",
    "Kopiert": "Copied",
    "Kopieren ging nicht – bitte markieren und Strg+C": "Couldn't copy – please select it and press Ctrl+C",
    "Das hat nicht geklappt": "That didn't work",
    "Spotify ist verbunden": "Spotify is connected",
    "Spotify trennen?": "Disconnect Spotify?",
    "Like, Bibliothek, Smart Shuffle und Spotifys Lautstärkeregler gehen danach nicht mehr. Titel und Steuerung laufen weiter.": "Likes, library, Smart Shuffle and Spotify's volume control stop working. Titles and playback controls keep running.",
    "Entwicklungsmodus: Autostart und Updates gibt es nur in der installierten App.": "Development mode: autostart and updates are only available in the installed app.",
    "Im Browser geöffnet: Einstellungen gibt es nur im App-Fenster.": "Opened in a browser: settings are only available in the app window.",
    "Sprache der Oberfläche": "Interface language",
    "„Automatisch“ richtet sich nach Windows: deutsche Oberfläche auf deutschen Systemen, sonst Englisch.": "“Automatic” follows Windows: German on German systems, English otherwise.",
    "Automatisch": "Automatic",
    "Mit Windows starten": "Start with Windows",
    "Startet unsichtbar im Infobereich, damit das Gerät sofort Daten bekommt.": "Starts hidden in the notification area so the device gets data right away.",
    "Beim Schließen im Infobereich weiterlaufen": "Keep running in the notification area when closed",
    "Das Gerät bleibt verbunden, auch wenn das Fenster zu ist. Beenden über das Symbol unten rechts.": "The device stays connected even with the window closed. Quit from the icon in the bottom right.",
    "Datenordner": "Data folder",
    "Öffnen": "Open",
    "Updates": "Updates",
    "Suchen": "Check",
    "Jetzt aktualisieren": "Update now",
    "Über": "About",
    "Updates kommen, sobald das Projekt auf GitHub veröffentlicht ist.": "Updates arrive once the project is published on GitHub.",
    "Suche …": "Checking …",
    "Keine Verbindung zum Update-Server.": "Can't reach the update server.",
    "Version {v} ist da.": "Version {v} is available.",
    "Du hast die neueste Version.": "You have the latest version.",
    "Lade herunter …": "Downloading …",
    "Update fehlgeschlagen.": "Update failed.",
    "Installiere – die App startet gleich neu.": "Installing – the app will restart in a moment.",
    "Inspiriert vom eingestellten Spotify Car Thing und von <a href=\"#\" data-url=\"https://github.com/ItsRiprod/DeskThing\">DeskThing</a> von ItsRiprod, das die Original-Hardware weiter nutzbar macht.<br> Gebaut mit <a href=\"#\" data-url=\"https://lvgl.io\">LVGL</a> (MIT), <a href=\"#\" data-url=\"https://pywebview.flowrl.com\">pywebview</a> (BSD), <a href=\"#\" data-url=\"https://docs.aiohttp.org\">aiohttp</a> (Apache 2.0), <a href=\"#\" data-url=\"https://python-pillow.org\">Pillow</a> (MIT-CMU), <a href=\"#\" data-url=\"https://github.com/AndreMiras/pycaw\">pycaw</a> (MIT) und <a href=\"#\" data-url=\"https://github.com/moses-palmer/pystray\">pystray</a> (LGPL). Symbole: <a href=\"#\" data-url=\"https://lucide.dev\">Lucide</a> (ISC) und <a href=\"#\" data-url=\"https://fonts.google.com/icons\">Material Icons</a> (Apache 2.0). Schrift: <a href=\"#\" data-url=\"https://github.com/erikdkennedy/figtree\">Figtree</a> (OFL). Emojis auf den Tasten zeichnet Windows mit Segoe UI Emoji. Spotify ist eine Marke von Spotify AB; dieses Projekt steht in keiner Verbindung zu Spotify.": "Inspired by the discontinued Spotify Car Thing and by <a href=\"#\" data-url=\"https://github.com/ItsRiprod/DeskThing\">DeskThing</a> by ItsRiprod, which keeps the original hardware useful.<br> Built with <a href=\"#\" data-url=\"https://lvgl.io\">LVGL</a> (MIT), <a href=\"#\" data-url=\"https://pywebview.flowrl.com\">pywebview</a> (BSD), <a href=\"#\" data-url=\"https://docs.aiohttp.org\">aiohttp</a> (Apache 2.0), <a href=\"#\" data-url=\"https://python-pillow.org\">Pillow</a> (MIT-CMU), <a href=\"#\" data-url=\"https://github.com/AndreMiras/pycaw\">pycaw</a> (MIT) and <a href=\"#\" data-url=\"https://github.com/moses-palmer/pystray\">pystray</a> (LGPL). Icons: <a href=\"#\" data-url=\"https://lucide.dev\">Lucide</a> (ISC) and <a href=\"#\" data-url=\"https://fonts.google.com/icons\">Material Icons</a> (Apache 2.0). Font: <a href=\"#\" data-url=\"https://github.com/erikdkennedy/figtree\">Figtree</a> (OFL). Emojis on the keys are drawn by Windows with Segoe UI Emoji. Spotify is a trademark of Spotify AB; this project is not affiliated with Spotify.",
    "Feld anklicken zum Bearbeiten, Felder ziehen zum Tauschen. „Speichern“ schickt die Belegung sofort ans Gerät.": "Click a slot to edit it, drag slots to swap them. “Save” sends the layout to the device right away.",
    "Name der Seite": "Page name",
    "Wohin weitere Tasten kommen, wenn 8 nicht reichen": "Where more keys go when 8 aren't enough",
    "→ Seiten nach rechts": "→ Pages to the right",
    "↓ Reihen nach unten": "↓ Rows below",
    "+ Seite": "+ Page",
    "+ Reihe": "+ Row",
    "Letzte Seite entfernen": "Remove last page",
    "Letzte Reihe entfernen": "Remove last row",
    "Letzte Seite entfernen?": "Remove last page?",
    "Letzte Reihe entfernen?": "Remove last row?",
    "Dort ist noch 1 Taste belegt. Sie wird mit entfernt.": "1 key is still assigned there. It will be removed too.",
    "Dort sind noch {n} Tasten belegt. Sie werden mit entfernt.": "{n} keys are still assigned there. They will be removed too.",
    "Entfernen": "Remove",
    "So sieht die Tastenseite auf dem Gerät aus – sichtbar sind 4 × 2, der Rest wird gewischt.": "This is how the key page looks on the device – 4 × 2 are visible, swipe for the rest.",
    "Taste 1": "Key 1",
    "Taste {n}": "Key {n}",
    "Dieses Feld ist leer.": "This slot is empty.",
    "Taste anlegen": "Create key",
    "Neue Taste": "New key",
    "Name": "Name",
    "z. B. Discord stumm": "e.g. Discord mute",
    "Symbol": "Icon",
    "Emoji": "Emoji",
    "Eigenes Bild": "Own image",
    "Emoji einfügen": "Paste an emoji",
    "Mit <b>Win + .</b> öffnet Windows die Emoji-Auswahl.": "<b>Win + .</b> opens the Windows emoji picker.",
    "Hier geht nur ein Emoji – Text gehört ins Feld „Name“": "Only one emoji fits here – text goes into “Name”",
    "Bild auswählen …": "Choose image …",
    "PNG, JPG, WebP, GIF oder ICO.": "PNG, JPG, WebP, GIF or ICO.",
    "Bild konnte nicht übernommen werden": "Couldn't use this image",
    "Größe": "Size",
    "Als Symbol": "As icon",
    "Ganze Taste": "Whole key",
    "Form": "Shape",
    "Original": "Original",
    "Abgerundet": "Rounded",
    "Rund": "Round",
    "Symbolfarbe": "Icon color",
    "Schriftfarbe": "Text color",
    "Aktion": "Action",
    "Freie Taste (F13–F24)": "Free key (F13–F24)",
    "Tastenkombination": "Key combination",
    "Programm starten": "Start program",
    "Skript ausführen": "Run script",
    "Link öffnen": "Open link",
    "Medien": "Media",
    "Diese Tasten kennt Windows, auf keiner Tastatur gibt es sie – sie kommen also nichts anderem in die Quere. Im Programm (OBS, Discord, …) ins Feld fürs Tastenkürzel klicken und dann die Taste am Gerät drücken. Grau = schon vergeben.": "Windows knows these keys, but no keyboard has them – so they never get in the way. In the program (OBS, Discord, …) click the shortcut field, then press the key on the device. Grey = already taken.",
    "schon bei „{name}“": "already used by “{name}”",
    "frei": "free",
    "Kombination": "Combination",
    "z. B. ctrl+shift+m oder f13": "e.g. ctrl+shift+m or f13",
    "Aufnehmen": "Record",
    "Tasten drücken …": "Press keys …",
    "„{key}“ lässt sich nicht belegen": "“{key}” can't be assigned",
    "„Aufnehmen“ drücken, dann die Tasten. Möglich sind ctrl, shift, alt, win, a–z, 0–9, f1–f24, esc, enter, tab, space, Pfeile, volumemute …": "Press “Record”, then the keys. Possible: ctrl, shift, alt, win, a–z, 0–9, f1–f24, esc, enter, tab, space, arrows, volumemute …",
    "Programm (Pfad zur .exe oder Name)": "Program (path to the .exe or name)",
    "Durchsuchen …": "Browse …",
    "Parameter (optional)": "Arguments (optional)",
    "Skript (.ps1, .bat, .cmd oder .py)": "Script (.ps1, .bat, .cmd or .py)",
    "Link": "Link",
    "Befehl": "Command",
    "Play/Pause": "Play/Pause",
    "Nächster Titel": "Next track",
    "Vorheriger Titel": "Previous track",
    "Lauter": "Volume up",
    "Leiser": "Volume down",
    "Ton aus/an": "Mute/unmute",
    "Feld leeren": "Clear slot",
    "Aktion testen": "Test action",
    "Ausgeführt": "Done",
    "Hat nicht geklappt": "Didn't work",
    "Gespeichert – das Gerät zeigt die neue Belegung": "Saved – the device shows the new layout",
    "Speichern fehlgeschlagen": "Saving failed",
    "Änderungen verwerfen?": "Discard changes?",
    "Alles seit dem letzten Speichern geht verloren.": "Everything since the last save will be lost.",
    "Seiten am Gerät": "Pages on the device",
    "Nur eingeschaltete Seiten stehen oben am Display. Wer das Gerät nur für Tasten und Audio nutzt, blendet die Medien aus.": "Only pages that are switched on appear at the top of the display. If you only use the device for keys and audio, hide media.",
    "Playlists": "Playlists",
    "Audio": "Audio",
    "Mindestens eine Seite bleibt an": "At least one page stays on",
    "Darstellung der Audio-Seite": "Audio page layout",
    "Mischpult: senkrechte Regler nebeneinander, seitlich wischen. Liste: eine Zeile pro App, nach unten scrollen.": "Mixer: vertical faders side by side, swipe sideways. List: one row per app, scroll down.",
    "Mischpult": "Mixer",
    "Liste": "List",
    "Eigene Farbe": "Custom color",
    "Farbton": "Hue",
    "Helligkeit und Sättigung": "Brightness and saturation",
    "Übernehmen": "Apply",
/*EN-END*/
  };

  const ATTRS = ["title", "placeholder", "aria-label"];
  const texts = new WeakMap();   // text node → { de, shown }
  const attrs = new WeakMap();   // Element → { attr: { de, shown } }
  const blocks = new WeakMap();  // data-i18n-html → { de, shown }
  let lang = "de";

  const norm = (s) => s.replace(/\s+/g, " ").trim();
  const has = (key) => Object.prototype.hasOwnProperty.call(EN, key);

  function t(text, values) {
    let out = lang === "en" && has(text) ? EN[text] : text;
    if (values) out = out.replace(/\{(\w+)\}/g, (m, k) => (k in values ? values[k] : m));
    return out;
  }

  /* keep the whitespace around the text so spacing in the layout stays the same */
  function translated(raw) {
    const key = norm(raw);
    if (lang !== "en" || !key || !has(key)) return raw;
    return raw.match(/^\s*/)[0] + EN[key] + raw.match(/\s*$/)[0];
  }

  const skipped = (el) => !el || !!el.closest("[data-raw], script, style, [data-i18n-html]");

  function doText(node) {
    if (skipped(node.parentElement)) return;
    let rec = texts.get(node);
    if (!rec || node.nodeValue !== rec.shown) rec = { de: node.nodeValue };  /* set anew by the script */
    rec.shown = translated(rec.de);
    if (node.nodeValue !== rec.shown) node.nodeValue = rec.shown;
    texts.set(node, rec);
  }

  function doElement(el) {
    if (el.closest("[data-raw]")) return;
    if (el.hasAttribute("data-i18n-html")) {
      let rec = blocks.get(el);
      if (!rec || el.innerHTML !== rec.shown) rec = { de: el.innerHTML };
      const key = norm(rec.de);
      const html = lang === "en" && has(key) ? EN[key] : rec.de;
      if (el.innerHTML !== html) el.innerHTML = html;
      rec.shown = el.innerHTML;  /* the way the browser returns it */
      blocks.set(el, rec);
    }
    if (el.parentElement && skipped(el.parentElement) && !el.hasAttribute("data-i18n-html")) return;
    const recs = attrs.get(el) || {};
    for (const name of ATTRS) {
      if (!el.hasAttribute(name)) continue;
      const value = el.getAttribute(name);
      let rec = recs[name];
      if (!rec || value !== rec.shown) rec = { de: value };
      rec.shown = translated(rec.de);
      if (value !== rec.shown) el.setAttribute(name, rec.shown);
      recs[name] = rec;
    }
    attrs.set(el, recs);
  }

  function walk(root) {
    if (root.nodeType === Node.TEXT_NODE) return doText(root);
    if (root.nodeType !== Node.ELEMENT_NODE) return;
    doElement(root);
    const it = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT);
    for (let n = it.nextNode(); n; n = it.nextNode()) {
      if (n.nodeType === Node.TEXT_NODE) doText(n); else doElement(n);
    }
  }

  new MutationObserver((records) => {
    for (const r of records) {
      if (r.type === "characterData") doText(r.target);
      else if (r.type === "attributes") doElement(r.target);
      else r.addedNodes.forEach(walk);
    }
  }).observe(document.documentElement, { subtree: true, childList: true, characterData: true, attributes: true, attributeFilter: ATTRS });

  function set(next) {
    lang = next === "en" ? "en" : "de";
    document.documentElement.lang = lang;
    walk(document.documentElement);
    document.documentElement.style.visibility = "";
    /* texts with values (t("Taste {n}")) are set again by the script itself */
    window.dispatchEvent(new CustomEvent("languagechange-app", { detail: lang }));
    return lang;
  }

  async function refresh() {
    try {
      const res = await fetch("/api/lang", { cache: "no-store" });
      return set((await res.json()).lang);
    } catch (e) {
      return set(lang);
    }
  }

  /* show nothing until the language is known, or German flashes up (briefly at most) */
  document.documentElement.style.visibility = "hidden";
  setTimeout(() => { document.documentElement.style.visibility = ""; }, 800);
  refresh();

  window.t = t;
  window.I18N = { t, set, refresh, get lang() { return lang; } };
})();
