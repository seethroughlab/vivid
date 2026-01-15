# GUI Widgets Example

Demonstrates the immediate-mode GUI widget system built on OverlayCanvas.

## API Overview

```cpp
#include <vivid/gui/gui.h>

// Create GUI context each frame
Gui gui(canvas, input);

// Panels contain widgets with auto-layout
gui.beginPanel("Title", x, y, width, height);
  gui.label("Text");
  gui.slider("Scale", &value, min, max);
  gui.checkbox("Enable", &flag);
  gui.dropdown("Mode", &index, {"A", "B", "C"});
  gui.colorPicker("Color", &rgba);
  if (gui.button("Click")) { /* handle click */ }
gui.endPanel();
```

## Widgets

| Widget | Signature | Returns |
|--------|-----------|---------|
| `label` | `label(text)` | void |
| `button` | `button(label)` | true if clicked |
| `checkbox` | `checkbox(label, *bool)` | true if changed |
| `slider` | `slider(label, *float, min, max)` | true if changed |
| `dropdown` | `dropdown(label, *int, options)` | true if changed |
| `colorPicker` | `colorPicker(label, *vec4)` | true if changed |

## Layout Helpers

- `gui.spacing(height)` - Add vertical space
- `gui.separator()` - Draw horizontal line

## Style Customization

```cpp
gui.style().panelBackground = {0.1f, 0.1f, 0.1f, 0.9f};
gui.style().widgetActive = {0.2f, 0.5f, 0.9f, 1.0f};
gui.style().padding = 10.0f;
```

## Setup Requirements

The GUI needs an initialized OverlayCanvas with a font:

```cpp
OverlayCanvas canvas;
FontAtlas font;

// In first frame:
canvas.init(ctx.device(), ctx.queue(), surfaceFormat);
font.load(ctx, "path/to/font.ttf", 16.0f);
canvas.setFont(0, &font);
```
