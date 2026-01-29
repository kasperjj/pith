# Pith

A minimal, stack-based editor runtime where everything is a dictionary of words.

## Architecture

Pith is split into two independent components:

```
┌─────────────────────────────────────────────────────┐
│                     pith_ui.c                       │
│         Platform-specific rendering (raylib)        │
│                                                     │
│  - Renders Views to screen                          │
│  - Captures input events                            │
│  - Calls into runtime when events occur             │
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
│  - Executes words                                   │
│  - Produces View trees for UI                       │
└─────────────────────────────────────────────────────┘
```

### pith_runtime (pith_runtime.h / pith_runtime.c)

The interpreter. Pure C with no platform dependencies. Responsibilities:

- Parse .pith dictionary files
- Manage the value stack
- Manage dictionary hierarchy (slots, parent lookup)
- Execute words
- Produce a View tree when `ui` slots are evaluated
- Handle file operations (via callbacks for portability)

### pith_ui (pith_ui.h / pith_ui.c)

The renderer. Currently uses raylib, but designed to be replaceable. Responsibilities:

- Initialize window
- Render View trees (text, vstack, hstack, etc.)
- Capture keyboard/mouse input
- Translate input to runtime events
- Main loop

## Building

Requires raylib. On macOS with Homebrew:

```bash
brew install raylib
cd pith
make
```

## Running

```bash
./pith                    # Opens current directory
./pith /path/to/project   # Opens specific project
```

## File Structure

```
/your-project
    /pith
        runtime.pith      # Main dictionary, defines UI
        /agents
            outline.pith  # Agent definitions
    /docs
        your-files.md
```

## Language Overview

See `docs/LANGUAGE.md` for the full language reference.

Quick example:

```
editor:
    parent: component

    buffer: ""
    cursor: 0

    ui:
        buffer cursor text-view
    end

    on-key:
        key "cmd-s" = if
            buffer file.path write
        else
            key cursor insert
            buffer set
        end
    end
end
```

## License

MIT
