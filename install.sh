#!/bin/bash

# Output colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}> Leteciel Magivant Universal Installer${NC}"
echo "This script will compile and install Magivant onto your Linux system."
echo "-------------------------------------------------------------------"

print_deps_help() {
    echo -e "${RED}Error: Compilation failed! Required libraries (GTK3/Libusb) or the compiler are missing.${NC}"
    echo -e "${YELLOW}Please install the following dependencies for your distribution:${NC}"
    echo ""

    if [ -f /etc/arch-release ]; then
        echo "> Arch Linux / CachyOS / Manjaro:"
        echo "   sudo pacman -S base-devel gtk3 libusb pkgconf"
    elif [ -f /etc/fedora-release ]; then
        echo "> Fedora:"
        echo "   sudo dnf install gcc gtk3-devel libusb1-devel pkgconf-pkg-config"
    elif [ -f /etc/debian_version ]; then
        echo "> Ubuntu / Debian / Pop!_OS:"
        echo "   sudo apt update && sudo apt install build-essential libgtk-3-dev libusb-1.0-0-dev pkg-config"
    elif grep -q "suse" /etc/os-release; then
        echo "> openSUSE:"
        echo "   sudo zypper install gcc gtk3-devel libusb-1_0-devel pkg-config"
    else
        echo "> General Linux:"
        echo "   Install: gcc, pkg-config, gtk3-development, and libusb-1.0-development"
    fi
}

echo -e "${YELLOW}[1/5] Compiling GTK3 C code...${NC}"
if gcc main.c magivant.c usbdac_manager.c -o magivant_app $(pkg-config --cflags --libs gtk+-3.0 libusb-1.0) -lpthread 2>/dev/null; then
    echo -e "${GREEN}Compilation successful!${NC}"
else
    print_deps_help
    exit 1
fi

echo -e "${YELLOW}[2/5] Installing binary to /usr/local/bin...${NC}"
sudo mkdir -p /usr/local/bin
sudo cp magivant_app /usr/local/bin/magivant
sudo chmod +x /usr/local/bin/magivant

echo -e "${YELLOW}[3/5] Installing application icon...${NC}"
sudo mkdir -p /usr/share/pixmaps
if [ -f "magivant.png" ]; then
    sudo cp magivant.png /usr/share/pixmaps/magivant.png
    sudo chmod 644 /usr/share/pixmaps/magivant.png
    echo -e "${GREEN}Icon installed successfully.${NC}"
else
    echo -e "${RED}Warning: magivant.png not found! Falling back to 'audio-card'.${NC}"
fi

echo -e "${YELLOW}[4/5] Applying Udev rules for hardware access...${NC}"
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="2fc6", ATTR{idProduct}=="f13b", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/99-magivant.rules > /dev/null
sudo udevadm control --reload-rules
sudo udevadm trigger

echo -e "${YELLOW}[5/5] Creating Desktop Entry...${NC}"
ICON_NAME="magivant"
if [ ! -f "magivant.png" ]; then
    ICON_NAME="audio-card"
fi

cat <<EOF | sudo tee /usr/share/applications/magivant.desktop > /dev/null
[Desktop Entry]
Name=Leteciel Magivant
Comment=Hardware Controller for Leteciel Magivant DAC
Exec=magivant
Icon=$ICON_NAME
Terminal=false
Type=Application
Categories=AudioVideo;Audio;HardwareSettings;
Keywords=dac;audio;leteciel;magivant;
EOF

sudo update-desktop-database /usr/share/applications 2>/dev/null || true
if command -v gtk-update-icon-cache >/dev/null; then
    sudo gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true
fi

echo "-------------------------------------------------------------------"
echo -e "${GREEN}> INSTALLATION COMPLETE${NC}"
echo -e "Magivant is now ready to use."
echo -e "Launch it from your ${YELLOW}Application Menu${NC} or type ${YELLOW}'magivant'${NC} in the terminal."

rm magivant_app