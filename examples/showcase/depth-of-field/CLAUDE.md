# Depth of Field

## Vision

Demonstrate cinematic depth-of-field using real 3D depth information. Objects at different distances from the camera should blur realistically based on focus distance, just like a real camera lens. This showcases Vivid's ability to combine 3D rendering with sophisticated post-processing.

The scene should be visually organized to clearly demonstrate the effect: near objects in warm colors (red/orange), mid-distance objects in green, and far objects in cool colors (blue/purple). As focus shifts, different depth layers come into sharp focus while others blur.

## Key Features

- Real depth-based DOF using the 3D renderer's depth buffer
- Multiple objects arranged at varying distances
- Color-coded depth layers for clear visualization
- Interactive focus control
- Adjustable blur strength
- Debug mode to visualize depth information

## Technical Approach

- Scene with 10+ objects spread across a large depth range (Z=-5 to Z=50)
- Render3D with depthOutput enabled
- DepthOfField operator using depth buffer for blur calculation
- Camera positioned to see full depth range
- Post-processing: Bloom and Vignette for cinematic look

## Operators to Use

| Operator | Purpose |
|----------|---------|
| Sphere, Box, Torus | Geometry primitives |
| Plane | Ground surface |
| SceneComposer | Arrange objects in 3D space |
| CameraOperator | View control with near/far planes |
| DirectionalLight (x2) | Key and fill lighting |
| Render3D | 3D rendering with depth output |
| DepthOfField | Depth-based blur effect |
| Bloom | Highlight glow |
| CRTEffect | Vignette only |

## Interaction

- **Left/Right**: Adjust focus distance (near to far)
- **Up/Down**: Adjust blur strength
- **D**: Toggle depth debug view (shows depth buffer)

## Visual Style

- Near objects: Warm red/orange tones
- Mid objects: Green tones
- Far objects: Cool blue/purple tones
- Ground plane in dark gray
- Dark background
- Subtle bloom on highlights
- Vignette for cinematic framing
