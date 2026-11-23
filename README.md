# Ilands
# iLands (Native PC Port)

A native Windows C++17 port of the voxel sandbox game **iLands**, built with SDL2, OpenGL 3.3, and Winsock2.

## Overview

This project recreates the core gameplay and visual presentation of iLands as a standalone Windows executable using the original game's terrain generation algorithms, lighting model, block definitions, and assets.

## Features

- Procedural 256x256x65 voxel island generation (Normal and Flat world types) with caves, trees, water flooding, and dynamic sky/emission lighting.
- OpenGL 3.3 renderer with custom shaders for opaque terrain, transparent cutouts, animated water, and a dynamic day/night sky.
- Full 64-block inventory and 5-slot hotbar with 3D block previews.
- Local world creation, renaming, deletion, and persistent block/time saving.
- In-game chat (`T`) with history and system notifications.
- Local Area Network (LAN) multiplayer over Wi-Fi via Winsock2 (TCP synchronization and UDP auto-discovery), supporting real-time player movement, block updates, and chat.

## Controls

- `W A S D` — Move
- `Mouse` — Look
- `Space` — Jump / Swim up
- `Shift` / `Ctrl` — Swim down
- `Left Click` — Break block
- `Right Click` — Place block
- `Middle Click` — Pick block
- `1`–`5` / `Mouse Wheel` — Select hotbar slot
- `E` / `I` / `Tab` — Open block inventory
- `T` — Open chat
- `Esc` — Pause menu / LAN toggle

## Building

Built on Windows with MinGW-W64 (GCC 14, C++17) and SDL2 using `src/build.bat`. Game assets are loaded from `src/assets`.

I hope to make you smile and not let this gem of 2013 die
