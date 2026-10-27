# After Bloom

After Bloom is a **2D game prototype built with C++ and raylib**

![last-commit](https://img.shields.io/github/last-commit/Fierys0/AfterBloom)
![repo-size](https://img.shields.io/github/repo-size/Fierys0/AfterBloom)
![license](https://img.shields.io/github/license/Fierys0/AfterBloom)

## Cloning Project

``` Terminal
git clone https://github.com/Fierys0/AfterBloom.git --recursive
```

---

## Disclaimer

This project is developed primarily on **Linux**.  
While Windows and other platforms are supported via raylib, you may still encounter platform-specific issues depending on your toolchain.

---

## Features

- raylib-powered rendering
- idk

---

## Build Requirements

### All Platforms

- **CMake ≥ 3.16**
- **C++17 compatible compiler**
  - GCC / Clang (Linux, macOS)
  - MinGW-w64 or MSVC (Windows)

### Linux

Install raylib (system version recommended):

**Pacman**

```bash
sudo pacman -S raylib
```

**APT**

```bash
sudo apt install libraylib-dev
```

Install mpv

**Pacman**

```bash
sudo pacman -S mpv
```

**APT**

```bash
sudo apt install mpv
```

### Windows

just compile cuh

## Building the Project

```bash
mkdir build
cmake -G "Ninja" -S . -B build
cmake --build build
```

---

## Game Requirements

- OpenGL >= 3.3
- 1 GB of free RAM (for now)
