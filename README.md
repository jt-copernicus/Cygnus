# Cygnus
Cygnus is a minimalistic, floating window manager for X11 - work in progress

What's working so far (I'm up to v 0.8-3 by now [3.24.25]):

* Cygnus Window Manager (minimalist, floating, keyboard + mouse driven) - works since 3/20
* Autostart apps (configurable via text file) - works since 3/20
* Multiple workspaces (2, navigable via panel applet or keyboard bindings)- works since 3/21
* Run dialogue - native (bound to ctrl+d) - works since 3/22
* Terminal Launch (x-terminal-emulator bound to Alt+Enter) - works since 3/20
* Panel - native (applications list and system tray) works since 3/21
* Panel applets (supported. non-native) works since 3/21
* Root menu - native (primitive-configurable via text file) works since 3/22
* Custom Key Bindings (configurable via text file) - works since 3/21
* File Manager - native (double click works, navigation, file and folder creation work.  search works _in the same directory_ [more like a filter, really] as of 3/24)
* Open-File dialogue - native - used by image viewer and music player when launched without parameters.  working as of 3/25.
* Custom Icons for FM (configurable via text file) - confirmed working as of 3/25.
* Automounter - native (runs on start from session file- works) - works since 3/23 for simple media, at least (usb/dvd), complex media, like cell phones, that mount two units instead of just one, don't work.
* Screenshot utility - native (command only, no gui - full screen and area select - saves automatically to pictures folder [~/Pictures]) - works as  of 3/24
* Image Viewer - native (keyboard driven - arrows to navigate or zoom). works as of 3/22

* text editor - super small - no syntax hl (completed as of 3/24 - can open various formats, including .txt, .sh, .py.  can save as well.  can highlight and copy/cut/paste)
* systray clock - minimal - completed as of 3/24, as a separate applet that can run on startup via session file
* calculator - basic math functions with keyboard and mouse input support. -- completed as of 3/25.

* media player that doesn't load half of QT or half of GTK - got audio to play as of 3/24 (wav and ogg at least -- i want to remain gpl compliant. video not working as of yet). as of 3/25 i got some choppy, sped-up video, to play.  i think i'm calling it an audio player for now.
* webcam application - can stream live feed and save pictures (no video) - completed on 3/25

I don't want to publish the individual components separately, as they are part of a single cohesive experience.  I am using this WM exclusively on a daily basis, but I'd like to at least complete the pending items before it goes live.
