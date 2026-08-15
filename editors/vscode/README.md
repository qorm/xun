# XUN Language Support for Visual Studio Code

Official syntax highlighting and language configuration for **XUN** (X Unquoted Notation) files (`.xun`).

## Features

- ✨ **Accurate Syntax Highlighting**:
  - Unquoted strings and structured keys
  - 20 Core Tagged notations (`!ver`, `!sz`, `!du`, `!ip`, `!xb`, `!dt`, etc.)
  - Compact arrays (`!n[...]`, `!ip[...]`, etc.)
  - Multiline text blocks with delimiter recognition (`|` and `|CUSTOM ... CUSTOM`)
  - Numeric literals (integers, floats, hex `0x...`, octal `0o...`)
- 📌 **Intelligent Language Configuration**:
  - Automatic line comments (`#`)
  - Auto-closing brackets `{}` and `[]`
  - 2-space indentation rules
  - Code folding for nested dicts and multiline blocks

## Installation

### Local Installation (Symlink or Copy)
Copy or symlink this folder to your VS Code extensions directory:
- **macOS / Linux**: `~/.vscode/extensions/vscode-xun`
- **Windows**: `%USERPROFILE%\.vscode\extensions\vscode-xun`
