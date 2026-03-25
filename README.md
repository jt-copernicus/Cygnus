# Cygnus
Cygnus is a minimalistic, floating window manager for X11 - work in progress

What's working so far (I'm up to v 0.8-3 by now [3.24.25]):

* Cygnus Window Manager (minimalist, floating, keyboard + mouse driven)
* Autostart apps (configurable via text file)
* Multiple workspaces (2, navigable via panel applet or keyboard bindings)
* Run dialogue - native (bound to ctrl+d)
* Terminal Launch (x-terminal-emulator bound to Alt+Enter)
* Panel - native (applications list and system tray)
* Panel applets (supported. non-native)
* Root menu - native (primitive-configurable via text file)
* Custom Key Bindings (configurable via text file)
* File Manager - native (double click works, navigation, file and folder creation work.  search works _in the same directory_ [more like a filter, really] as of 3/24)
* Custom Icons for FM (configurable via text file)
* Automounter - native (runs on start from session file- works)
* Screenshot utility - native (command only, no gui - full screen and area select - saves automatically to pictures folder [~/Pictures])
* Image Viewer - native (keyboard driven - arrows to navigate or zoom).

* text editor - super small - no syntax hl (completed as of 3/24 - can open various formats, including .txt, .sh, .py.  can save as well.  can highlight and copy/cut/paste)
* systray clock - minimal - completed as of 3/24, as a separate applet that can run on startup via session file
* calculator - basic math functions with keyboard and mouse input support. -- math works as of 3/24.  still pending to implement memory functions (m+, m-, mr).

* media player that doesn't load half of QT or half of GTK - got audio to play as of 3/24 (wav and ogg at least -- i want to remain gpl compliant. video not working as of yet).

I don't want to publish the individual components separately, as they are part of a single cohesive experience.  I am using this WM exclusively on a daily basis, but I'd like to at least complete the pending items before it goes live.
