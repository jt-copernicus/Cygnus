#!/bin/bash

# Cygnus WM Dependency Installer, Builder and Setup
# This script installs all required development headers, builds the project,
# installs it system-wide, and sets up the user configuration directory.

REAL_USER=${SUDO_USER:-USER}
REAL_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)


# Exit on error
set -e

echo "Checking for system dependencies..."

# Detect package manager (currently supports Debian/Ubuntu-based systems)
if [ -x "$(command -v apt-get)" ]; then
    echo "Detected Debian/Ubuntu-based system."
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        libx11-dev \
        pkg-config \
        libavformat-dev \
        libavcodec-dev \
        libswscale-dev \
        libswresample-dev \
        libavutil-dev \
        libsdl2-dev \
        libsdl2-ttf-dev \
	libsfml-dev
else
    echo "Error: This script only supports Debian/Ubuntu-based systems (apt-get)."
    echo "Please manually install the equivalent of these packages for your distribution:"
    echo "gcc, make, libx11-dev, pkg-config, ffmpeg-dev, sdl2-dev, sdl2-ttf-dev"
    exit 1
fi

echo "------------------------------------------------------------"
echo "All dependencies installed successfully."
echo "------------------------------------------------------------"

echo "Compiling Cygnus..."
make clean
make

echo "------------------------------------------------------------"
echo "Build complete."
echo "------------------------------------------------------------"

echo "Installing Cygnus system-wide (requires sudo)..."
sudo make install

echo "------------------------------------------------------------"
echo "Setting up user configuration in ~/.cygnus-wm/ ..."
mkdir -p "$REAL_HOME/.cygnus-wm"

# Copy configuration files
cp menu "$REAL_HOME/.cygnus-wm/"
cp keys "$REAL_HOME/.cygnus-wm/"
cp icons "$REAL_HOME/.cygnus-wm/"
cp session "$REAL_HOME/.cygnus-wm"/

# Change ownership to ensure user can use files
chown -R "$REAL_USER":"$(id -gn "$REAL_USER")" "$REAL_HOME/.cygnus-wm"

# Ensure session script is executable
chmod a+x "$REAL_HOME/.cygnus-wm/session"

echo "Configuration files copied to ~/.cygnus-wm/"
echo "------------------------------------------------------------"
echo "Cygnus WM installation and setup complete!"
echo "To start Cygnus, add 'exec cygnus' to your .xinitrc"
echo "or select Cygnus from your display manager."
