# ROSE UI Viewer

Preview **and edit** ROSE Online's interface files (the RmlUi `.html` / `.css` under `Data\ui\themes`) without logging into the game.

It uses the same UI library the game does (RmlUi, with Lua), so layout, sprites, decorators, templates and your `<script>` code behave like they do in-game.

## Getting started

1. Unzip anywhere you can write to (Desktop, Documents — not Program Files). Settings, panel layout and sample data are kept next to the .exe.
2. Run `RoseUiViewer.exe`.
3. Click **Choose your ROSE Online folder** and pick the folder that has `Data` in it. You can also drag the folder, or any window's `.html` file, onto the viewer.
4. Pick a window from **Open a window...** (e.g. `hud.html`). Open as many as you like.

The screen has the preview in the middle and panels around it. Panels can be dragged, docked, tabbed or closed (**View** menu brings them back; **View → Reset panel layout** restores the default).

## Editing visually

1. Switch to **Select (V)** on the toolbar.
2. Click an element in a window. It gets a brass outline with handles, and the **Elements** panel jumps to it.
3. **Drag** it to move it, drag a **handle** to resize it, or use the **arrow keys** to nudge it (Shift = 10dp).
4. In **Properties**, change anything: drag numbers, pick colours, choose from lists, or type values (Enter saves).

Every change is written into the window's own `<style>` as a normal rule (in dp), e.g.
`#mana { position: relative; left: 40dp; top: 25dp; }` — if a rule with that selector is already in the `<style>`, it's edited in place; otherwise it goes in a section marked `ROSE UI Viewer edits`.

- **Apply changes to** picks what the rule targets: this element (`#id`) or everything with one of its classes. If the element has no id, you're asked to give it one (added to its tag).
- Each property shows **where its current value comes from** (`file:line`). Click it to jump there in the code. Right-click it to change the value **in that rule** instead (handy when a more specific rule wins).
- `id` and `class` can be edited at the top of Properties; that changes the tag itself.
- The window itself (its `<body>`) only moves around the preview; its in-game position is the game's.
- **Ctrl+Z / Ctrl+Y** undo and redo changes to files (also on the toolbar arrows and the Edit menu).

## Editing code

The **Code** panel shows the window's file with syntax colouring. **Open a file of this window...** lists everything it uses: its stylesheets, templates and scripts.

- With **Live preview** on, the preview updates as you type (after a short pause) — before you save.
- **Ctrl+S** saves. Tabs with unsaved changes are marked; the viewer asks before closing if anything isn't saved.
- **Show in code** (Properties) and the `file:line` buttons jump to the exact tag or rule.
- Files changed outside (in VS Code, Notepad++...) are picked up automatically.

## Controls

| | |
|---|---|
| **V** | Switch between Interact (use the windows like in game) and Select (edit) |
| **Esc** | Deselect |
| **Arrow keys** | Nudge the selected element (Shift = 10dp) |
| **Alt + drag** | Move a whole window around the preview (in Interact) |
| **Ctrl+S** | Save code |
| **Ctrl+Z / Ctrl+Y** | Undo / redo file changes |
| **F5** | Reload everything |
| **F8** | RmlUi's own inspector |
| **Ctrl + / Ctrl − / Ctrl 0** | Interface size (the game's dp scale) |
| Background | Plain colours, green screen, or **Screenshot**: put an in-game screenshot named `background.png` (or `.jpg`) next to the .exe |

## Sample data (what the game would fill in)

In-game, the client's C++ code supplies the values your windows show (name, HP, party members, inventory...). The viewer makes stand-in values instead:

- The **Sample data** panel lists every data model the open windows use. Tick boxes, drag numbers (percentages get a 0–100 slider), type text, and add/remove list entries with **+ / −**. The windows update instantly.
- The values live in the `mock` folder next to the .exe (`mock\character.json`, `mock\party.json`...) and are saved as you change them.
- The first time a window is opened, the viewer reads its markup and fills these files with every variable it uses (lists get three entries). Variables you add later are added without touching values you've changed.

## What it can't do

- **Item icons.** `<item-slot>` icons come from the game's data files. A slot holding an item gets the class `viewer-has-item` instead, and you can show a specific sprite with `item-sprite="..."` while testing.
- **Game commands.** `onclick="close"`, `onclick="select ..."` and the like are handled by the client. The viewer lists them in the Log panel when clicked; `close` hides the window.
- **Data callbacks.** `data-event-click="enter_avatar(...)"` calls are logged, not carried out. Assignments in data events (`index = index + 1`) do work.
- **Gauges** fill to `gvalue`/`gmax` with a child `<gauge-fill>` element, which may not match the game's own gauge drawing exactly.
- **Live game state** such as real-time timers fed by the client.

## Building from source

Windows builds are made by GitHub Actions (`.github/workflows/build.yml`): every push builds `RoseUiViewer-windows.zip` (under the run's *Artifacts*), and pushing a tag like `v1.0` also publishes it as a release.

Locally: `cmake -S . -B build && cmake --build build --config Release` with Visual Studio 2022 and CMake 3.20+. Dependencies (RmlUi, Lua 5.4, FreeType, GLFW, Dear ImGui, nlohmann/json, stb) are downloaded automatically.
