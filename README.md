# ROSE UI Viewer

Preview and edit ROSE Online's interface files (the RmlUi `.html` / `.css` under `Data\ui\themes`) without logging into the game. Save a file in your editor and the window updates on screen straight away.

It uses the same UI library the game does (RmlUi, with Lua), so layout, sprites, decorators, templates and your `<script>` code behave like they do in-game.

## Getting started

1. Unzip anywhere you can write to (Desktop, Documents — not Program Files). Settings and sample data are kept next to the .exe.
2. Run `RoseUiViewer.exe`.
3. Click **Game folder** and pick your ROSE Online folder (the one that has `Data` in it). You can also drag the folder, or any window's `.html` file, onto the viewer.
4. Pick a window from **Open a window...** (e.g. `hud.html`). Open as many as you like; they show side by side.
5. Edit the file in your usual editor and save. The viewer reloads by itself.

## Controls

| | |
|---|---|
| **F5** / Reload | Reload everything now |
| **F8** / Inspector | RmlUi's inspector: click an element to see its box, classes and the CSS rules applied to it. Its log lists missing files, CSS mistakes and Lua errors |
| **F2** | Hide/show the toolbar |
| **Ctrl + / Ctrl − / Ctrl 0** | Interface size (the game's dp scale) |
| **Alt + drag** | Move any window (windows with a title bar can also be dragged normally) |
| Background | Plain colours, green screen, or **Screenshot**: put an in-game screenshot named `background.png` (or `.jpg`) next to the .exe to see your windows over the game |

## Sample data (what the game would fill in)

In-game, the client's C++ code supplies the values your windows show (name, HP, party members, inventory...). The viewer makes stand-in values instead:

- Each data model gets a file in the `mock` folder next to the .exe, e.g. `mock\character.json`, `mock\party.json`.
- The first time a window is opened, the viewer reads its markup and fills these files with every variable it uses, with sample values (lists get three entries).
- Edit the numbers and text in those files to try things out — low HP, a full party, long names. Saving a file updates the windows. Variables you add to your markup later are added to the files without touching values you've changed.
- **Sample data** on the toolbar opens that folder.

## What it can't do

- **Item icons.** `<item-slot>` icons come from the game's data files. A slot holding an item gets the class `viewer-has-item` instead, and you can show a specific sprite with `item-sprite="..."` while testing.
- **Game commands.** `onclick="close"`, `onclick="select ..."` and the like are handled by the client. The viewer lists them in the inspector's log when clicked; `close` hides the window.
- **Data callbacks.** `data-event-click="enter_avatar(...)"` calls are logged, not carried out. Assignments in data events (`index = index + 1`) do work.
- **Gauges** fill to `gvalue`/`gmax` with a child `<gauge-fill>` element, which may not match the game's own gauge drawing exactly.
- **Live game state** such as real-time timers fed by the client.

## Building from source

Windows builds are made by GitHub Actions (`.github/workflows/build.yml`): every push builds `RoseUiViewer-windows.zip` (under the run's *Artifacts*), and pushing a tag like `v1.0` also publishes it as a release.

Locally: `cmake -S . -B build && cmake --build build --config Release` with Visual Studio 2022 and CMake 3.20+. Dependencies (RmlUi, Lua 5.4, FreeType, GLFW, nlohmann/json, stb) are downloaded automatically.
