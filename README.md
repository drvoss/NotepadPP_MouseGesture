# MouseGestures for Notepad++

A Notepad++ plugin that adds **mouse gesture** support to the editor.  
Hold the **right mouse button** and drag in a direction to trigger actions — no keyboard required.

## Gestures

| Gesture | Action |
|---|---|
| ← Left | Previous tab |
| → Right | Next tab |
| ↑ Up | Go to top of document |
| ↓ Down | Go to bottom of document |
| ↓→ Down + Right | Close current tab |
| ↓← Down + Left | Cut |
| ↑← Up + Left | Copy |
| →↓ Right + Down | Paste |
| ←→ Left + Right | Undo |
| →← Right + Left | Redo |

A red trail is drawn on screen while the gesture is in progress.

## Requirements

- Notepad++ **7.6 or later** (64-bit)
- Windows 10 or later

## Installation

1. Download `MouseGes.dll` from the [Releases](https://github.com/drvoss/NotepadPP_MouseGesture/releases) page.
2. Copy it to: `C:\Program Files\Notepad++\plugins\MouseGes\MouseGes.dll`
3. Restart Notepad++.

The plugin appears under **Plugins → MouseGestures → About**.

## Building from Source

### Prerequisites

- Visual Studio 2022 (with "Desktop development with C++" workload)

### Steps

```
git clone https://github.com/drvoss/NotepadPP_MouseGesture.git
cd NotepadPP_MouseGesture
```

Open `MouseGes.sln` in Visual Studio and build in **Release | x64** configuration.

The post-build event automatically copies the DLL to  
`C:\Program Files\Notepad++\plugins\MouseGes\` (requires UAC approval).

## License

MIT License — see [LICENSE](LICENSE) for details.
