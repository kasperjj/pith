# Pith

A minimal, stack-based UI runtime where everything is a dictionary of words. Pith combines the simplicity of Forth-like languages with reactive UI capabilities.

## Features

- **Stack-based execution** - Simple postfix notation, no complex syntax
- **Dictionary-based components** - Everything is a dictionary of slots with inheritance
- **Reactive signals** - Automatic UI updates when state changes
- **Cell-based rendering** - Text-mode UI with colors and layout primitives
- **Cross-platform core** - Platform-independent interpreter with pluggable UI

## Quick Example

```
app:
    parent: root
    count: 0 signal

    ui:
        [
            "Count: " text
            count deref to-string text
            "+" do count deref 1 + count! end button
            "-" do count deref 1 - count! end button
        ] hstack
    end
end

ui:
    app
end
```

This creates a counter with + and - buttons. Clicking them updates the count reactively.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                     pith_ui.c                       │
│         Platform-specific rendering (raylib)        │
│                                                     │
│  - Renders Views to screen                          │
│  - Captures input events                            │
│  - Handles focus and text input                     │
└─────────────────────┬───────────────────────────────┘
                      │
                      │  View tree + Events
                      │
┌─────────────────────▼───────────────────────────────┐
│                   pith_runtime.c                    │
│            Platform-independent interpreter         │
│                                                     │
│  - Parses .pith files                               │
│  - Maintains stack and dictionaries                 │
│  - Executes words, manages signals                  │
│  - Produces View trees for UI                       │
└─────────────────────────────────────────────────────┘
```

## Building

Requires raylib. On macOS with Homebrew:

```bash
brew install raylib
make
```

## Running

```bash
./pith                      # Opens current directory as project
./pith path/to/project      # Opens specific project directory
./pith script.pith          # Runs a single .pith file
./pith -d path/to/project   # Debug mode (shows execution details)
```

## Project Structure

```
your-project/
    pith/
        runtime.pith      # Main dictionary, defines UI and logic
    other-files/
        ...
```

When you run `./pith your-project`, it loads `your-project/pith/runtime.pith`.

## Language Overview

See [language.md](language.md) for the full language reference.

### Core Concepts

**Stack-based execution:**
```
1 2 +           # pushes 1, pushes 2, adds them -> 3
"hello" print   # pushes string, prints it
```

**Dictionaries and slots:**
```
my-component:
    parent: root
    name: "Alice"

    ui:
        name text
    end
end
```

**Signals for reactive state:**
```
counter: 0 signal           # create reactive state
counter deref               # read value (explicit unwrap)
42 counter!                 # write value (triggers re-render)
counter textfield           # bind signal to widget (two-way)
```

**UI primitives:**
```
"Hello" text                # text label
["a" text "b" text] hstack  # horizontal layout
["x" text "y" text] vstack  # vertical layout
spacer                      # flexible space
"Click" do ... end button   # clickable button
"" textfield                # single-line text input
buffer textarea             # multiline text editor
[v1 v2] idx view-switch     # select view by index (for tabs)
my-view fill                # make view fill available space
```

### Lifecycle

Pith applications have four optional lifecycle slots:

```
init:   # runs first - setup
ui:     # mounts UI (opens window if view produced)
main:   # runs after UI closes
exit:   # cleanup
```

## What's Implemented

- Stack operations (dup, drop, swap, over, rot)
- Arithmetic (+, -, *, /, mod, abs, min, max)
- Comparison (=, !=, <, >, <=, >=)
- Logic (and, or, not)
- Strings (length, concat, split, join, trim, substring, etc.)
- Arrays (map, filter, reduce, each, find, sort, etc.)
- Maps (new-map, get, set, keys, values, etc.)
- Gap buffers for text editing
- Signals for reactive state
- File I/O (file-read, file-write, file-exists, dir-list)
- JSON parsing (to-json, parse-json)
- UI: text, textfield, textarea, button, vstack, hstack, spacer, view-switch, fill
- Styling: colors, backgrounds, borders, padding, gap
- Control flow: if/else, do blocks

## License

MIT
