# Image Lightbox Test

This page verifies the PhotoSwipe 5 lightbox feature: click any content image to view it fullscreen, with gallery navigation, pinch/double-click zoom, ←/→ keyboard switching, and Esc to close.

## Single Image (click to enlarge)

Click the image below → fullscreen view; click the backdrop or press Esc to close.

![Single test image 800x500](assets/lightbox/test-1.png)

## Gallery Navigation (same-page images group together)

All images on the same page automatically form a gallery: once opened, use ←/→ keys, swipe, or click the arrows on desktop to switch, with the current index and alt caption shown at the bottom.

![Second test image 640x800](assets/lightbox/test-2.png)

![Third test image 1200x675](assets/lightbox/test-3.png)

![Fourth test image 500x500](assets/lightbox/test-4.png)

![Fifth test image 900x600](assets/lightbox/test-5.png)

## Zoom Test

Double-click (or pinch / mouse-wheel) to zoom in for details; drag to pan when zoomed.

## Linked Image (should NOT trigger lightbox)

Images wrapped in a link keep their normal link behavior — clicking navigates, no lightbox:

[![Linked image](assets/lightbox/test-5.png)](https://example.com/)

## Checklist

- Click an image to open the lightbox with a zoom-in animation from the thumbnail
- Multiple images group automatically; ←/→ and swipe switch slides, counter is correct
- Double-click / pinch / wheel zoom; pan when zoomed
- Click backdrop / ✕ / Esc closes, focus returns
- Keyboard Tab reachable, buttons have aria labels
- Linked images do not trigger the lightbox
- Alt caption shown at the bottom (when alt is present)
