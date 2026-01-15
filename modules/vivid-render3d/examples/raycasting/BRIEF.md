# Vision

An interactive 3D picking demo where colorful spheres bob and spin while responding to mouse hover and click, with a custom outline shader highlighting selected objects.

## Aesthetic Goals
- Playful, toy-like spheres in primary colors against a dark stage
- Responsive visual feedback: cyan glow on hover, white glow on selection
- Continuous animation proves raycasting tracks moving objects accurately

## Techniques Demonstrated
- Screen-to-world ray conversion using inverse view-projection matrix
- Ray-sphere intersection testing with depth sorting for correct picking
- Custom WGSL outline effect using blur-based alpha edge detection
- Dual-scene rendering: main scene + mask scene for selection compositing
- Orbital camera with right-drag control
