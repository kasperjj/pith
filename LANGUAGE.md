# Pith Language Reference

## Core Concepts

Pith is a stack-based language where everything is a dictionary of slots. Each slot is a word that, when invoked, executes its body.

## Syntax

### Comments

```
# This is a comment
```

### Slots

Single-line slot (ends at newline):

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

### Data Types

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

# Arrays
[1, 2, 3]
["a", "b", "c"]

# Maps
{ "key": "value", "count": 10 }
```

### Components

Components are dictionaries. Inheritance via `parent` slot:

```
my-component:
    parent: component
    
    buffer: []
    
    ui: buffer as tree-view
    
    on-save:
        buffer file.path write
    end
end
```

### Getting Values

```
# Dot notation for known paths
config.theme
settings.editor.font

# Dynamic lookup
config key get
```

### Setting Values

```
# Static path
"dark" config.theme set

# Dynamic
"dark" config key set
```

### Control Flow

```
# If
condition if
    do-something
end

# If-else
condition if
    do-this
else
    do-that
end
```

### Lambdas

Use `do ... end` for anonymous blocks:

```
items do double end map
items do 10 > end filter
items do print end each
```

## Stack Operations

```
dup         # ( a -- a a )
drop        # ( a -- )
swap        # ( a b -- b a )
over        # ( a b -- a b a )
rot         # ( a b c -- b c a )
```

## Arithmetic

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

## Comparison

```
=           # ( a b -- bool )
!=          # ( a b -- bool )
<           # ( a b -- bool )
>           # ( a b -- bool )
<=          # ( a b -- bool )
>=          # ( a b -- bool )
```

## Logic

```
and         # ( a b -- bool )
or          # ( a b -- bool )
not         # ( a -- bool )
```

## Strings

```
concat      # ( a b -- joined )
split       # ( str delim -- array )
join        # ( array delim -- str )
trim        # ( str -- str )
length      # ( str -- n )
substring   # ( str start end -- str )
contains    # ( str search -- bool )
replace     # ( str old new -- str )
uppercase   # ( str -- str )
lowercase   # ( str -- str )
```

## Arrays

```
length      # ( array -- n )
first       # ( array -- item )
last        # ( array -- item )
nth         # ( array n -- item )
append      # ( array item -- array )
append!     # ( array item -- ) mutates
prepend     # ( item array -- array )
prepend!    # ( item array -- ) mutates
slice       # ( array start end -- array )
reverse     # ( array -- array )
reverse!    # ( array -- ) mutates
sort        # ( array -- array )
sort!       # ( array -- ) mutates
contains    # ( array item -- bool )
index-of    # ( array item -- n )
empty?      # ( array -- bool )
```

## Array Higher-Order

```
map         # ( array block -- array )
filter      # ( array block -- array )
each        # ( array block -- )
reduce      # ( array initial block -- value )
find        # ( array block -- item )
any         # ( array block -- bool )
all         # ( array block -- bool )
```

## Maps

```
get         # ( map key -- value )
set         # ( value map key -- map )
set!        # ( value map key -- ) mutates
keys        # ( map -- array )
values      # ( map -- array )
has         # ( map key -- bool )
remove      # ( map key -- map )
remove!     # ( map key -- ) mutates
merge       # ( map map -- map )
merge!      # ( map map -- ) mutates
```

## Type Checking

```
type        # ( a -- str )
string?     # ( a -- bool )
number?     # ( a -- bool )
array?      # ( a -- bool )
map?        # ( a -- bool )
bool?       # ( a -- bool )
nil?        # ( a -- bool )
```

## Conversion

```
to-string   # ( a -- str )
to-number   # ( str -- n )
parse-json  # ( str -- value )
to-json     # ( value -- str )
```

## File Operations

```
file-read       # ( path -- contents )
file-write      # ( contents path -- )
file-append     # ( contents path -- )
file-exists     # ( path -- bool )
file-delete     # ( path -- )
file-info       # ( path -- map )

dir-list        # ( path -- array )
dir-create      # ( path -- )
dir-delete      # ( path -- )

project.path    # ( -- path )
project.files   # ( -- array )

file-watch      # ( path block -- )
file-unwatch    # ( path -- )

path-join       # ( a b -- path )
path-parent     # ( path -- path )
path-filename   # ( path -- str )
path-extension  # ( path -- str )
```

## Text Parsing

```
markdown-headings   # ( str -- array )
markdown-links      # ( str -- array )
markdown-sections   # ( str -- array )
lines               # ( str -- array )
words               # ( str -- array )
```

## UI Primitives

```
text        # ( str -- view )
textfield   # ( str on-change -- view )
button      # ( label on-click -- view )
texture     # ( path -- view )
vstack      # ( views -- view )
hstack      # ( views -- view )
width       # ( view n -- view )
height      # ( view n -- view )
fill        # ( view -- view )
scroll      # ( view -- view )
```

## Style Slots (Cascading)

These are not words but slots that cascade through the parent chain:

```
color: "white"
background: "black"
bold: false
border: nil           # or "all", "top", "left right", etc.
padding: 0
gap: 0
```

## Reactivity

Components with `buffer` and `ui` slots are reactive. When `buffer` changes, `ui` is automatically re-evaluated:

```
my-view:
    parent: component
    
    buffer: []
    
    ui: buffer as list-view
    
    on-data:
        new-data buffer set    # triggers ui refresh
    end
end
```

## Events

Define event handlers as slots. Events bubble up through parent chain:

```
on-key          # ( key -- )
on-click        # ( target -- )
on-file-save    # ( -- )
on-file-change  # ( path -- )
```

## Utility

```
print       # ( a -- )
now         # ( -- timestamp )
random      # ( -- n )
uuid        # ( -- str )
```
