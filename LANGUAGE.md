# Pith Language Reference

## Core Concepts

Pith is a stack-based language where everything is a dictionary of slots. Each slot is a word that, when invoked, executes its body.

> **Note:** This document describes both implemented features and planned features. Sections marked with ✓ are implemented; sections marked with ○ are planned but not yet implemented.

## Syntax ✓

### Comments ✓

```
# This is a comment
```

### Slots ✓

Single-line slot (ends at next slot or dictionary end):

```
name: "hello"
count: 42
double: 2 multiply
```

Multi-line slot (ends with `end`):

```
process:
    file read
    summarize
    buffer set
end
```

### Data Types ✓

```
# Strings
"hello world"

# Numbers
42
3.14

# Booleans
true
false

# Nil
nil

# Arrays - execute contents and collect results from stack
# Commas are optional (treated as whitespace)
[1, 2, 3]
["a", "b", "c"]
["Hello" text "World" text]    # executes text builtin, collects views
[header sidebar content]        # executes each as word, collects results

# Maps (○ not yet implemented)
{ "key": "value", "count": 10 }
```

### Components ✓

Components are dictionaries. Inheritance via `parent` slot:

```
my-component:
    parent: root

    buffer: "hello"

    ui:
        buffer text
    end
end
```

When a dictionary name is used as a word, its `ui` slot is automatically executed:

```
# In another component's ui:
ui:
    [my-component other-component] vstack
end
```

### Application Lifecycle ✓

Pith applications have four optional lifecycle slots that run in order:

```
init:
    # Runs first - setup, load data, initialize state
    "Starting up..." print
end

ui:
    # Runs second - mounts the UI (opens window if view is produced)
    app
end

main:
    # Runs third - main logic, runs after UI closes (or immediately if no UI)
    "Processing..." print
end

exit:
    # Runs last - cleanup before quitting
    "Goodbye!" print
end
```

**Execution order:**
1. `init:` - Initialization (before UI)
2. `ui:` - Mount UI view (if present, opens window and runs event loop)
3. `main:` - Main logic (after UI closes, or immediately if no `ui:`)
4. `exit:` - Cleanup (always runs last)

All lifecycle slots are optional. For non-UI applications, omit the `ui:` slot:

```
# A simple CLI program
init:
    "Loading data..." print
end

main:
    1 2 add print
end

exit:
    "Done!" print
end
```

### Getting Values ✓

```
# Dot notation for accessing slots in other dictionaries
app.buffer              # gets 'buffer' slot from 'app' dictionary
settings.theme          # gets 'theme' slot from 'settings' dictionary

# Nested dot notation works to any depth
app.config.theme        # gets nested value
root.level1.level2.val  # deep nesting supported

# Dynamic path access with strings
"app.config.theme" get-path   # same as app.config.theme
```

### Setting Values ✓

```
# set-path for modifying dictionary slots
"dark" "config.theme" set-path

# Nested paths work too
"new value" "app.settings.theme" set-path
```

### Control Flow ✓

```
# If - executes body when condition is truthy
condition if
    do-something
end

# If-else - executes else branch when condition is falsy
condition if
    do-this
else
    do-that
end
```

**Truthiness:**
- `false`, `nil`, and `0` are falsy
- Everything else is truthy (including non-zero numbers, strings, arrays)

**Examples:**
```
5 3 > if
    "greater" print
end

x 0 = if
    "zero" print
else
    "non-zero" print
end
```

### Lambdas ✓

Use `do ... end` for anonymous blocks:

```
items do process end map    # applies block to each item
```

## Stack Operations ✓

```
dup         # ( a -- a a )
drop        # ( a -- )
swap        # ( a b -- b a )
over        # ( a b -- a b a )
rot         # ( a b c -- b c a )
```

## Arithmetic ✓

```
add, +      # ( a b -- sum )
subtract, - # ( a b -- difference )
multiply, * # ( a b -- product )
divide, /   # ( a b -- quotient )
mod         # ( a b -- remainder )
abs         # ( a -- absolute )
min         # ( a b -- smaller )
max         # ( a b -- larger )
```

## Comparison ✓

```
=           # ( a b -- bool )
!=          # ( a b -- bool )
<           # ( a b -- bool )
>           # ( a b -- bool )
<=          # ( a b -- bool )
>=          # ( a b -- bool )
```

## Logic ✓

```
and         # ( a b -- bool )
or          # ( a b -- bool )
not         # ( a -- bool )
```

## Strings ✓

```
length      # ( str -- n )
concat      # ( a b -- joined )
split       # ( str delim -- array )
join        # ( array delim -- str )
trim        # ( str -- str )
substring   # ( str start end -- str )
contains    # ( str search -- bool )
replace     # ( str old new -- str )
uppercase   # ( str -- str )
lowercase   # ( str -- str )
```

## Arrays ✓

```
length      # ( array -- n )
first       # ( array -- item )
last        # ( array -- item )
nth         # ( array n -- item )
append      # ( array item -- array )
prepend     # ( item array -- array )
slice       # ( array start end -- array )
reverse     # ( array -- array )
sort        # ( array -- array )
contains    # ( array item -- bool )
index-of    # ( array item -- n )
empty?      # ( array -- bool )
map         # ( array block -- array )
filter      # ( array block -- array )
each        # ( array block -- )
reduce      # ( array initial block -- value )
find        # ( array block -- item )
any         # ( array block -- bool )
all         # ( array block -- bool )
```

## Maps ✓

Maps use the same dictionary structure as components, enabling dynamic code modification at runtime.

```
new-map     # ( -- map )           # create empty map
get         # ( map key -- value )
set         # ( value map key -- map )
keys        # ( map -- array )
values      # ( map -- array )
has         # ( map key -- bool )
remove      # ( map key -- map )
merge       # ( map map -- map )
sanitize    # ( value -- value )   # recursive copy with only data (no code)
```

**Example:**
```
main:
    42 new-map "x" set
    100 swap "x" set      # update value
    "x" get print         # prints 100
end
```

## Gap Buffer ✓

A gap buffer is an efficient data structure for text editing. It stores text with a "gap" that moves with the cursor, making insertions and deletions at the cursor position very fast.

```
new-gap     # ( -- gapbuf )            # create empty gap buffer
string-to-gap  # ( str -- gapbuf )        # convert string to gap buffer
gap-to-string  # ( gapbuf -- str )        # convert gap buffer to string
gap-insert  # ( str gapbuf -- gapbuf ) # insert string at cursor
gap-delete  # ( n gapbuf -- gapbuf )   # delete n chars (positive=forward, negative=backward)
gap-move    # ( n gapbuf -- gapbuf )   # move cursor by n positions
gap-goto    # ( n gapbuf -- gapbuf )   # move cursor to absolute position
gap-cursor  # ( gapbuf -- n )          # get cursor position
gap-length  # ( gapbuf -- n )          # get content length
gap-char    # ( n gapbuf -- str )      # get character at position n
```

**Example:**
```
main:
    "Hello World" string-to-gap    # cursor at 0
    5 swap gap-goto             # cursor at 5
    1 swap gap-delete           # delete space
    ", " swap gap-insert        # insert comma and space
    12 swap gap-goto            # go to end
    "!" swap gap-insert         # add exclamation
    gap-to-string print            # prints "Hello, World!"
end
```

## Signals (Reactive State) ✓

Signals provide reactive state management. When a signal's value changes, the UI automatically re-renders.

### Creating Signals

```
signal      # ( value -- signal )  # wrap a value in a signal
```

Signals are typically defined as slots in a component:

```
app:
    count: 0 signal           # creates a signal with initial value 0
    name: "Alice" signal      # creates a signal with initial string
end
```

### Reading Signals

Use `deref` to explicitly unwrap a signal to its inner value:

```
app.count deref               # returns the number (e.g., 0)
app.count deref to-string text      # displays "0"
```

**Note:** Signals must be explicitly dereferenced with `deref` when you need their value. This allows signals to be passed to widgets like `textarea` and `textfield` for two-way binding.

### Writing Signals

Use the `!` suffix to write to a signal:

```
42 app.count!                 # sets count to 42
app.count 1 + app.count!      # increments count by 1
```

The write syntax works with dot notation for accessing signals in other components.

### Reactive UI Example

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

When the `+` or `-` button is clicked:
1. The button's block executes, reading (`deref`) and modifying the signal
2. The signal is marked as dirty
3. The UI automatically re-renders with the new value

### How It Works

- Signals wrap values and track when they change
- Writing to a signal (`word!`) marks it as dirty
- After event handling, dirty signals trigger a UI rebuild
- Reading a signal always returns the unwrapped inner value

## Type Checking ✓

```
type        # ( a -- str )
string?     # ( a -- bool )
number?     # ( a -- bool )
array?      # ( a -- bool )
map?        # ( a -- bool )
bool?       # ( a -- bool )
nil?        # ( a -- bool )
```

## Conversion ✓

```
to-string   # ( a -- str )
to-number   # ( str -- n )
to-json     # ( map -- str )      # map to JSON object string
parse-json  # ( str -- map )      # JSON object string to map
```

**Note:** `to-json` and `parse-json` require a JSON object at the root level (not arrays or primitives).

## File Operations ✓

```
file-read       # ( path -- contents )   # returns nil if file doesn't exist
file-write      # ( contents path -- )   # creates or overwrites file
file-append     # ( contents path -- )   # appends to file
file-exists     # ( path -- bool )
dir-list        # ( path -- array )      # returns nil if directory doesn't exist
```

**Example:**
```
main:
    "Hello, World!" "/tmp/test.txt" file-write
    "/tmp/test.txt" file-read print       # prints "Hello, World!"
    "/tmp/test.txt" file-exists print     # prints "true"
    "src" dir-list length print           # prints number of files in src/
end
```

**Not yet implemented:**
```
project.path    # ( -- path )
project.files   # ( -- array )
```

## Text Parsing ✓

```
lines               # ( str -- array )
words               # ( str -- array )
```

## UI Primitives

**Implemented:**
```
text        # ( str -- view )
textfield   # ( signal-or-str -- view )  # single-line editable text field
textarea    # ( signal-or-str -- view )  # multiline editable text area
button      # ( label block -- view )    # clickable button with on-click handler
vstack      # ( array -- view )          # vertical stack of views
hstack      # ( array -- view )          # horizontal stack of views
spacer      # ( -- view )                # expands to fill available space
view-switch # ( array index -- view )    # select one view from array by index
fill        # ( view -- view )           # set fill=true on a view
```

**Not yet implemented:**
```
texture     # ( path -- view )
width       # ( view n -- view )
height      # ( view n -- view )
scroll      # ( view -- view )
```

### Textfield ✓

A single-line editable text input field. Internally uses a gap buffer for efficient editing.

```
"" textfield                  # empty text field
"initial value" textfield     # text field with initial content
my-signal textfield           # bound to a signal (persists edits)
```

Click on a textfield to focus it, then type to edit. Supports:
- Arrow keys (left/right) for cursor movement
- Backspace/Delete for deletion
- Home/End to jump to start/end
- Escape to unfocus

When bound to a signal, the textfield's content is written back to the signal when focus is lost (clicking elsewhere or pressing Escape).

### Textarea ✓

A multiline editable text area. Like textfield but supports multiple lines.

```
"line 1\nline 2" textarea     # textarea with initial content
my-signal textarea            # bound to a signal (persists edits)
```

Supports all textfield keys plus:
- Arrow keys (up/down) for line navigation
- Home/End to jump to line start/end
- Enter to insert newlines

When bound to a signal, changes are persisted when focus is lost.

**Example - tabbed editor:**
```
app:
    parent: root
    current-tab: 0 signal
    buffer-1: "File 1 content" signal
    buffer-2: "File 2 content" signal

    ui:
        [
            "File 1" do 0 app.current-tab! end button
            "File 2" do 1 app.current-tab! end button
        ] hstack
        [
            app.buffer-1 textarea
            app.buffer-2 textarea
        ] app.current-tab deref view-switch fill
    end
end
```

### Button ✓

A clickable button with a label and an on-click handler block.

```
"Click me" do "Clicked!" print end button
```

The block after `do ... end` executes when the button is clicked. Commonly used with signals for reactive updates:

```
"+" do count deref 1 + count! end button
```

### Spacer ✓

The `spacer` element expands to fill available space in a stack. Use it to push elements apart:

```
# Status bar at bottom of screen
ui:
    [
        header
        content
        spacer          # pushes status-bar to bottom
        status-bar
    ] vstack
end

# Elements at left and right edges
ui:
    [
        "Left" text
        spacer          # fills middle space
        "Right" text
    ] hstack
end
```

Multiple spacers share the available space equally.

### View Switch ✓

Selects one view from an array based on an index. Useful for tabbed interfaces or conditional rendering.

```
[view1 view2 view3] 0 view-switch    # returns view1
[view1 view2 view3] 1 view-switch    # returns view2
```

The index is clamped to valid bounds (0 to length-1). Views not selected are freed to prevent memory leaks.

**Example - tab bar:**
```
app:
    parent: root
    tab: 0 signal

    ui:
        [
            # Tab buttons
            [
                "Tab A" do 0 app.tab! end button
                "Tab B" do 1 app.tab! end button
            ] hstack

            # Tab content
            [
                "Content for Tab A" text
                "Content for Tab B" text
            ] app.tab deref view-switch
        ] vstack
    end
end
```

### Fill ✓

Sets the `fill` property on a view, causing it to expand and fill available space in its parent stack.

```
my-view fill                  # view now fills available space
```

Commonly used with `view-switch` to make the selected view fill its container:

```
[
    app.buffer-1 textarea
    app.buffer-2 textarea
] app.current-tab deref view-switch fill
```

Without `fill`, views only take their natural size. With `fill`, they expand to use all remaining space.

## Style Slots (Cascading) ✓

These are not words but slots that cascade through the parent chain:

```
color: "white"          # text color
background: "black"     # background color
bold: false
border: "right"         # or "all", "top", "left right", etc.
padding: 1              # cells
gap: 1                  # cells between children
```

### Colors

Colors can be specified as:

- **Hex values:** `"#RRGGBB"` or `"#RRGGBBAA"`
- **Basic names:** `"black"`, `"white"`
- **Open Color palette:** All colors from [Open Color](https://yeun.github.io/open-color/)

**Open Color names** support base names (defaults to shade 6) or specific shades (0-9):

```
color: "red"            # red shade 6
color: "red 2"          # red shade 2 (lighter)
color: "red 9"          # red shade 9 (darker)
background: "gray 0"    # lightest gray
background: "blue 4"    # medium blue
```

**Available color families:** gray, red, pink, grape, violet, indigo, blue, cyan, teal, green, lime, yellow, orange

Each family has 10 shades (0-9), from lightest to darkest.


### Event Handlers

**Button clicks** are handled via the block passed to `button`:

```
"Click me" do "Clicked!" print end button
```

**Keyboard events** can be handled with `on-key` slot (partially implemented):

```
on-key:
    key "cmd-q" = if
        "Goodbye!" print
    end
end
```

## Utility

**Implemented:**
```
print       # ( a -- )
length      # ( array-or-string -- n )
```

**Not yet implemented:**
```
now         # ( -- timestamp )
random      # ( -- n )
uuid        # ( -- str )
```

## CLI Options

```
pith [options] [path]

Options:
  -h, --help      Show help message
  -v, --version   Show version information
  -d, --debug     Enable debug output (parsing, execution, rendering)
```

**Path can be:**
- A directory containing a `pith/runtime.pith` file (project mode)
- A `.pith` file directly (single-file mode)

**Examples:**
```bash
# Run a project directory
pith my-project/

# Run a single pith file directly
pith script.pith

# Run with debug output
pith -d my-project/
```
