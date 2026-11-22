# okaru-emoji

**English** · [Español](#español)

The emoji picker of [OKARU](https://github.com/MrAndyVF/okaru) (郁): emojis (with recents and skin tones), GIFs from
[KLIPY](https://klipy.com), kaomojis and symbols. It opens centered as a layer-shell surface (**Super + .** opens and
closes it), so clicking an item pastes it into the window you were typing in and the picker stays open. Enter pastes
and closes; Ctrl+Enter copies and closes.

## Install

It ships in OKARU's pacman repository `[okaru]` (set up by OKARU's installer):

```sh
sudo pacman -S okaru-emoji
```

## Build

It builds against OKARU's shared core, provided by the `okaru-core` package. The GIF tab needs a KLIPY API key, passed
at build time (it is never stored in the repository; without it the tab is hidden):

```sh
meson setup build -Dklipy_key="$(cat ~/.config/okaru-dev/klipy.key)" && meson compile -C build
meson test -C build
# Run it with the data and translations from this repository:
OKARU_APPS_DIR=$PWD/apps ./build/okaru-emoji
```

Data (`emoji.json`, `kaomoji.json`, `symbols.json`) and translations (English and Spanish) live in `apps/emoji/`.

## License

MIT.

---

## Español

El selector de emojis de [OKARU](https://github.com/MrAndyVF/okaru) (郁): emojis (con recientes y tonos de piel), GIF de
[KLIPY](https://klipy.com), kaomojis y símbolos. Se abre centrado como superficie layer-shell (**Super + .** lo abre y
lo cierra), así que al hacer clic en algo se pega en la ventana donde estabas escribiendo y el selector sigue abierto.
Enter pega y cierra; Ctrl+Enter copia y cierra.

### Instalar

Viene en el repositorio pacman de OKARU, `[okaru]` (lo configura el instalador de OKARU):

```sh
sudo pacman -S okaru-emoji
```

### Compilar

Se compila contra el núcleo compartido de OKARU, que trae el paquete `okaru-core`. La pestaña de GIF necesita una clave
de la API de KLIPY, que se pasa al compilar (nunca se guarda en el repositorio; sin ella la pestaña no aparece):

```sh
meson setup build -Dklipy_key="$(cat ~/.config/okaru-dev/klipy.key)" && meson compile -C build
meson test -C build
# Ejecutarlo con los datos y las traducciones de este repositorio:
OKARU_APPS_DIR=$PWD/apps ./build/okaru-emoji
```

Los datos (`emoji.json`, `kaomoji.json`, `symbols.json`) y las traducciones (inglés y español) están en `apps/emoji/`.

### Licencia

MIT.
