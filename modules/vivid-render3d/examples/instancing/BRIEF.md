# Vision

A mesmerizing field of 500 cubes arranged in a 3D grid, undulating in synchronized waves with rainbow colors cycling through the spectrum - a demonstration of GPU instancing power.

## Aesthetic Goals
- Rainbow color palette that shifts continuously through hue space
- Organic wave motion creates a living, breathing cube ocean
- Subtle bloom adds warmth and visual softness to the metallic forms

## Techniques Demonstrated
- InstancedRender3D for efficient rendering of hundreds of objects in a single draw call
- Per-instance transforms with position, rotation, and scale animation
- Per-instance colors and PBR material properties (metallic, roughness)
- Wave-based collective animation using sine functions with phase offsets
