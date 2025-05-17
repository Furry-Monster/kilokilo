# KiloKilo Editor

A lightweight terminal-based text editor implemented in C, inspired by the Kilo editor. This is a personal practice project that demonstrates basic text editor functionality.

> You may check Kilo Editor made by antirez, the creator of redis.

## Features

- Terminal-based user interface
- Basic text editing capabilities
- Real-time display of cursor position and file status
- Status bar showing filename and modification status
- Save file functionality
- Simple and efficient implementation

## Building and Running

### Prerequisites

- Linux operating system
- GCC compiler
- Make build system

### Build Instructions

```bash
# Build the editor
make

# Build with debug information
make debug
```

### Usage

```bash
# Open a new file
./kilokilo

# Open an existing file
./kilokilo filename
```

### Key Bindings

- `Ctrl-Q`: Quit the editor
- `Ctrl-S`: Save the file
- Arrow keys: Move cursor
- Page Up/Down: Scroll through the file
- Home/End: Move to start/end of line

## Project Status

- Basic editor functionality is implemented
- Windows support is planned for future releases

## License

This project is licensed under the MIT License - see the LICENSE file for details.
