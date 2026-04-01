# Cygnus Window Manager

Cygnus is a minimalistic floating window manager for X11 written in pure C using the Xlib library. It prioritizes simplicity, lightweight performance, and an aesthetic focus on thin borders without window decorations.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Includes stb_image.h and stb_image_write.h by Sean Barret, which are used under the MIT License.  License text included within header files, and in a separate file.

## Features
- **Built-in Panel:** Integrated taskbar with workspace selector, minimized windows area, and system tray. The panel is always visible and cannot be covered by windows.
- **Dual Workspaces:** Two independent workspaces to organize windows.
- **Root Menu:** Configurable root menu accessible via right-click on the desktop.
- **Application Runner:** Built-in application runner (Ctrl+d or from menu).
- **Minimalist:** No window decorations, only thin colored borders.
- **Mouse Focus:** Windows are activated when the cursor enters the window and unfocused when it leaves.
- **Native Utilities:**
  - `cygnus-fm`   : Integrated lightweight file manager with icon support.
  - `cygnus-media`: Simple audio player (video isn't fully supported) (SDL2/FFmpeg).
  - `cygnus-cam`  : Lightweight webcam application for photos and preview.
  - `cygnus-paint`: Simple paint application (C++/SFML).
  - `cygnus-view` : Fast image viewer.
  - `cygnus-edit` : Minimalist text editor.
  - `cygnus-calc` : Basic calculator.
  - `cygnus-shot` : Screenshot utility.
  - `cygnus-open` : File picker used by other utilities.
  - `cygnus-mount`: Auto mounter for external drives.
  - `cygnus-clock`: Digital 12hr am/pm clock applet for panel.
- **Floating-Only:** Windows are placed according to client requests or moved via mouse (Alt + Drag).
- **Keyboard Driven:** Comprehensive set of keyboard shortcuts for window and WM control.
- **Configurable:** User-defined startup session script, dynamic keyboard bindings, and root menu.
- **Lightweight:** Single-process event loop with minimal dependencies.

## Key Bindings
| Key Binding                 | Action                                |
| --------------------------- | ------------------------------------- |
| **Ctrl + Alt + Left/Right** | Switch between Workspaces 1 and 2     |
| **Alt + f**                 | Launch File Manager                   |
| **PrintScreen**             | Take screenshot                       |
| **Alt + q** or **Alt + F4** | Close focused window                  | 
| **Alt + x**                 | Maximize focused window               |
| **Alt + r**                 | Restore focused window                |
| **Alt + n**                 | Minimize focused window               |
| **Alt + Tab**               | Cycle through windows                 |
| **F11**                     | Toggle fullscreen                     |
| **F5**                      | Refresh borders                       |
| **Alt + Mouse Drag**        | Move window                           |
| **Ctrl + Mouse Drag**       | Resize window from corner             |
| **Ctrl + d**                | Launch built-in application runner    |
| **Alt + Return**            | Launch Terminal (x-terminal-emulator) |
| **Ctrl + Shift + q**        | Exit Window Manager                   |

## Mouse Interaction
- **Right Click (Desktop):** Open root menu.
- **Left Click (Panel):** Switch workspaces or restore minimized windows.

## Configuration
Cygnus uses configuration files in `~/.cygnus-wm/`:

### Session Script (`~/.cygnus-wm/session`)
Executed at startup. Use this to set backgrounds, etc.
```bash
#!/bin/bash
feh --bg-scale /path/to/wallpaper.jpg &
# System tray applets will automatically dock in the built-in panel:

#auto mounter
cygnus-mount &
#systray clock
cygnus-clock &

nm-applet &
volumeicon &
```

### Key Bindings (`~/.cygnus-wm/keys`)
Define additional shortcuts without recompiling.
Format: `MODIFIER(S) KEYSYM COMMAND`
Modifiers: `Control`, `Alt`, `Shift`, `Super` (or `Mod1`, `Mod4`).
Internal command: `run` (triggers built-in runner).
Example:
```text
Control f firefox
Control p pcmanfm
```

### Root Menu (`~/.cygnus-wm/menu`)
Configure the desktop right-click menu.
Format:
- `[exec] (Label) {command}`: Run a command.
- `[exit]`: Quit Cygnus.
- `[restart]`: Restart Cygnus.

Example:
```text
[exec] (Terminal) {x-terminal-emulator}
[exec] (Firefox) {firefox}
[exec] (Run Dialog) {run}
[restart]
[exit]
```

### Icon Theme (`~/.cygnus-wm/icons`)
Configure the icon theme for the file manager.
Format: `icontheme=THEME_NAME`
If the theme exists in `/usr/share/icons`, it will be used (mapped to internal colors).
Example:
```text
icontheme=Adwaita
```

Sample, pre-filled session, menu, keys, and icons files are placed in ~/.cygnus-wm/ by the installer so you get a usable session from the start.

## Installation
### Dependencies
- `libx11-dev`
- `libsdl2-dev`, `libsdl2-ttf-dev`
- `libavformat-dev`, `libavcodec-dev`, `libswscale-dev`, `libswresample-dev`
- `gcc`, `make`, `pkg-config`

### Quick Setup (Debian/Ubuntu)
The `install.sh` script installs dependencies, builds the project, installs it system-wide, and sets up the initial configuration:
```bash
chmod +x install.sh
sudo ./install.sh
```

### Manual Build and Install
```bash
mkdir -p cygnus-paint
mv main.cpp cygnus-paint/
mv cygnus-paint.1 cygnus-paint/
mv pmakefile cygnus-paint/Makefile
make
sudo make install
```
*The makefile expects cygnus-paint in its own directory.  I keep it separate because it's the only program written in C++ instead of C.

## Usage
Run `man cygnus` for full documentation. Each utility also has its own man page (e.g., `man cygnus-fm`).
To start Cygnus from `~/.xinitrc`, add:
```bash
exec dbus-run-session -- cygnus
```
Ensure `~/.cygnus-wm/session` exists and is executable for startup items.

## Known Bugs
* No scrolling on cygnus-fm.  If there are more files than can be seen in the active window, you won't be able to see them, as it currently doesn't support scrolling (working on it--ETA Apr EOM).
* Sometimes cygnus-paint won't 'redo.'  It'll undo just fine, though (working on it--ETA Apr EOM).
* Restarting the WM sometimes kills and doesn't bring back some panel applets, in which case you can either summon them back individually, or exit the WM and re open it (no ETA).
* cygnus-media is intended to have video support, but I haven't got it to work consistently.  It plays audio (wav, ogg) just fine (no ETA).
