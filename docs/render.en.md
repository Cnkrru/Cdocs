# Render Loop

The render loop is the process by which a UI library continuously copies the `Canvas` buffer onto the screen surface.

## Two phases

- **Logic update**: process input, advance state.
- **Draw**: call primitives like `canvas_set_pixel` to refresh the buffer, then blit to the window.

> A real app's window is kept visible by the OS compositor; we only need to repaint the content each frame.

## How it relates to us

The `Canvas` we wrote is "what to draw"; the window backend is responsible for pasting it onto the screen surface.
