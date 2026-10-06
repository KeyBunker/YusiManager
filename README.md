# YusiManager 2.6

- Organize and manage SSH profiles.
- Securely connect to SSH servers using your FIDO2 hardware key.
- Opens remote folders in external file managers such as Krusader and Dolphin.
- Built with Qt 6 and C++20.



## Build

Requirements: **GNU Make**, a **C++20 compiler**, **pkg-config** (or pkgconf),
**Qt 6 base development files and tools** (`moc` and `rcc`), and
**OpenSSL development files** (`libcrypto`).


### Debian / Ubuntu
```bash
sudo apt install -y build-essential pkg-config qt6-base-dev qt6-base-dev-tools krusader libssl-dev
git clone https://github.com/KeyBunker/YusiManager.git
cd YusiManager
make
```

### Arch / Manjaro
```bash
sudo pacman -S --needed base-devel pkgconf qt6-base krusader openssl
git clone https://github.com/KeyBunker/YusiManager.git
cd YusiManager
make
```

### Fedora
```bash
sudo dnf install -y make gcc-c++ pkgconf-pkg-config qt6-qtbase-devel krusader openssl-devel
git clone https://github.com/KeyBunker/YusiManager.git
cd YusiManager
make
```

### openSUSE (Leap/Tumbleweed)
```bash
sudo zypper install -y make gcc-c++ pkg-config libqt6-qtbase-devel krusader libopenssl-devel
git clone https://github.com/KeyBunker/YusiManager.git
cd YusiManager
make
```
