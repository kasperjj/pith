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
```

### Setting Values ○

```
# Not yet implemented
"dark" config.theme set
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
add         # ( a b -- sum )
subtract    # ( a b -- difference )
multiply    # ( a b -- product )
divide      # ( a b -- quotient )
mod         # ( a b -- remainder )
abs         # ( a -- absolute )
min         # ( a b -- smaller )
max         # ( a b -- larger )
```

## Comparison ✓

```
=           # ( a b -- bool )
<           # ( a b -- bool )
>           # ( a b -- bool )
```

**Not yet implemented:**
```
!=          # ( a b -- bool )
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

## File Operations ○

**Not yet implemented:**
```
file-read       # ( path -- contents )
file-write      # ( contents path -- )
file-exists     # ( path -- bool )
dir-list        # ( path -- array )
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
vstack      # ( array -- view )    # array of views
hstack      # ( array -- view )    # array of views
spacer      # ( -- view )          # expands to fill available space
```

**Not yet implemented:**
```
textfield   # ( str on-change -- view )
button      # ( label on-click -- view )
texture     # ( path -- view )
width       # ( view n -- view )
height      # ( view n -- view )
scroll      # ( view -- view )
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

## Style Slots (Cascading) ✓

These are not words but slots that cascade through the parent chain:

```
color: "white"          # text color (hex string like "#RRGGBB")
background: "black"     # background color
bold: false
border: "right"         # or "all", "top", "left right", etc.
padding: 1              # cells
gap: 1                  # cells between children
```

## Reactivity ○

**Not yet implemented.** Planned: Components with `buffer` and `ui` slots will be reactive.

## Events ○

**Partially implemented.** Event handlers can be defined but execution is incomplete:

```
on-key          # ( key -- )
on-click        # ( target -- )
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
