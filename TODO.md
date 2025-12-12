[x] I don't want "Vivid V3" in all of the headers. This isn't *really* v3 -- it's just that I had 2 false start. So really, this is vivid 0.1.0.
[x] the noise operator should support many versions of noise: perlin, simplex, etc.
[x] Add to the chain-demo: a noise node DISTORTS a texture loaded from assets/images
[x] The title bar should include the name of the project
[x] The 'f' key should toggle fullscreen 
[x] I don't like the extern "C" stuff in the chain.cpp files. It seems like boilerplate. In vivid_v1 we just had a VIVID_CHAIN(setup, update) macro. Is it possible to use that approach here?
[x] Create a README.md that follows the conventions of READMEs on github
[x] Add reminders to the ROADMAP to update the readme at the end of major phases.
[x] PLAN: we just finished phase 4. what kind of examples would best illustrate (and test) everyting we added? 
[x] It seems that, in the jump to v3, we lost some of the emphasis on vivid bring LLM-first in the documentation. Let's update the README and the ROADMAP so that they reflect that one of the core philosophies of the project is to be LLM-friendly. It's already mentioned in docs/PHILOSOPHY.md in the "LLM-Native Workflow" section.
[x] Is everything we've done so far likely to work on Windows when I test? vivid_v1 was fully croass platform, so maybe it's a good idea to see how it was handled there.
    - Added Windows Media Foundation decoder (mf_decoder.cpp) ported from vivid_v1
    - Added Linux FFmpeg decoder stub (ffmpeg_decoder.cpp)
    - Updated CMakeLists.txt with platform-specific source selection
    - Updated hot-reload to link video addon on Windows
    - HAP codec requires FFmpeg on Windows/Linux (standard codecs work via MF/FFmpeg)
[x] The hot-reloader shouldn't have hard-coded addon paths. It should be dynamic becuase users might add more addons. Will that be changed when we create the addon registry? If so, is that change explicitly mentioned in the ROADMAP?
    - Yes, planned for Phase 16a (Addon Registry) - now explicitly added to ROADMAP
    - Will scan `#include` directives or read vivid.json to discover addon dependencies
[x] ALL platforms should use the official Vidvox code (hap.h, hap.cpp) to decode hap videos. It's highly optimized. Is that how it's done now?
    - **macOS (v3)**: ✅ Yes - AVFoundation demux → Vidvox hap.c → direct DXT texture upload (optimal, ported from v2)
    - **Windows/Linux (v3)**: ❌ No - HAP is a stub, falls back to standard codec decoder
    - **vivid_v2 design (correct)**: FFmpeg demux + Vidvox hap.c → DXT upload (planned for all platforms, only macOS implemented)
    - **vivid_v1 (incorrect)**: Used FFmpeg's built-in HAP decoder which converts to RGBA, losing DXT benefit
    - **TODO**: For Windows/Linux HAP support, use FFmpeg for container demux only (libavformat), then Vidvox hap.c for DXT decompression, then upload BC1/BC3 compressed texture to GPU
[x] At some point, the video player was working fine. but now it looks like some channels are mixed up. blue is green.
    - Fixed AVFDecoder: Changed from BGRA8Unorm to RGBA8Unorm with explicit BGRA→RGBA channel swap during pixel copy
    - Fixed HSV effect: Replaced buggy compact rgb2hsv/hsv2rgb functions with clearer standard implementations
[x] Why are you setting VK_ICD_FILENAMES, VK_DRIVER_FILES, and DYLD_LIBRARY_PATH when you launch examples. We don't need those anymore, right?
[x] When you run a vivid program, the imgui.ini file should be saved in the local folder, not in the root folder
[x] What changes can we make to make vivid even more LLM-friendly? Should we create better documentation they would make it easier for an LLM to quickly understand vivid? Should we enforce better user workflow, such as roadmaps and plans? Should we use other well known programing patterns like Fluent? Or something else completely?
    - Created docs/LLM-REFERENCE.md - compact operator reference for LLMs
    - Created docs/RECIPES.md - complete chain.cpp examples for common effects
    - Created examples/template/ - well-commented starter project with CLAUDE.md
    - Updated README.md with LLM documentation section and CLAUDE.md guidance
[x] Set up Doxygen for autodoc documentation
    - Created Doxyfile with v3 paths
    - Added Doxygen-style comments to core headers (operator.h, context.h, chain.h, param.h)
    - Added Doxygen-style comments to key operator headers (noise.h, output.h, composite.h, texture_operator.h)
    - Added `make docs` target to CMakeLists.txt (requires Doxygen to be installed)
[ ] Is there currently a "make install" command that will put vivid into my PATH?
[x] The README still mentions ctx.registerOperator, which is now automatic.
    - Updated README to show auto-registration via chain->init(ctx)
[x] The Mesh generators should include normals and UVs
[x] The Render3D node should still have to be conected to an Output node, right? Shouldn't that be fundamental to the whole vivid system? or am I misunderstanding something?
    - Yes, fundamental. Refactored render3d-demo to use Chain + Output pattern.
[x] The canvas-demo still includes the ctx.registerOperator call, which should be automatic.
    - Refactored canvas-demo to use Chain + Output pattern. Auto-registration now happens via Chain. 
[x] Please confirm that 1) all operators are auto-registered. This should be enforced as part of the operator architecture 2) All nodes use the proper chain API. 3) The documentation is updated to reflect 1 and 2.
    - Verified: Chain::init() auto-registers all operators for visualization (chain.cpp:164-170)
    - Converted all demos to use Chain API: hello-noise, particles, pointsprites, template
    - Removed all manual ctx.registerOperator() calls from examples
    - Updated docs/CHAIN-API.md with current API patterns and architecture notes 
[x] I definitely could be wrong, but it seems like we're bending over backwards to keep the core lightweight by putting everything in addons, but it would make our lives a lot easier to move SOME stuff from addons to core. Do you agree? If so, what would help to move to core? I'm thinking MAYBE imgui since it is used for chain visualization.
    - Merged ImGui/ImNodes into core - chain visualization is now a core feature, not an addon
    - Created AddonRegistry class for dynamic addon discovery
    - Hot-reload now scans chain.cpp `#include` directives to discover needed addons
    - Removed hardcoded addon paths from hot_reload.cpp
    - Added addon.json to vivid-video 
[x] It seems to me that the Chain should be constructed by the core and passed in setup and update. This way 1) we don't have to have the awkward "delete chain;" in setup for hotloading, and 2) we can automatically call chain->init(ctx) after setup() is called so that the user doesn't have to. Please help me explore the pros and cons of this approach.
    - Implemented: Context now owns the Chain via `ctx.chain()`
    - Added `chain.output("name")` to specify display output (no more Output operator required)
    - Core auto-calls `chain.init()` after setup() and `chain.process()` after update()
    - Automatic state preservation across hot-reloads (Feedback, VideoPlayer, etc.)
    - Migrated all examples to new pattern
    - Updated all documentation (README, LLM-REFERENCE, RECIPES, ROADMAP)
[x] I'd like to introduce the idea of a Geomery Operator that will show up in the node visualization. So, for the boolean operation, the user will create multiple geometry generation operators, and then fed into the boolean operator, which is fed into the scene, and then into the renderer3d, and then the output: geometry operator -> boolean operator(s) -> scene -> renderer3d -> 2d effects (optional) -> output.  Note that a geometry operator should NOT be a valid output node - only a texture operator is a valid output operator. 
[x] zsh: segmentation fault  ./build/bin/vivid examples/geometry-showcase
[x] regarding geometry-showcase, we just altered it so that the geometry shows up in the chain visualizer. It seems to me that this should be automatic, not a choice the user makes. Can you explain why there are multiple ways to create geometry? What are the pros and cons of that?
[x] does imnodes support zooming and scrolling? We need some way to navigate the node graph.
[x] geometry operators used to show previews of the gometry rotating. but now it just shows the number of vertices. what happened?
[x] I'd like ROADMAP.md to reflect the recent decisions we've made about 1) the new understanding of Geometry versus Mesh, and 2) the idea that cameras and lights are nodes that appear in the chain visualization. You don't need to explain that it was a CHANGE to the appraoch -- just explain the current approach.
[x] Operators with different OutputKinds should be colored differently in the chain visualization.
    - Texture: Default gray
    - Geometry: Blue
    - Value: Orange
    - Camera: Green
    - Light: Yellow
[x] image loading currently lives in vivid-effects-2d, but image loading will be needed in many different addons. Should we (1) make a "media loader" addon, or (2) add image loading to the core?
[x] TouchDesigner has a system where nodes don't get "baked" unless one of their output nodes requests it, and it is marked as "dirty" -- I think that's how it works. Anyway, we should have something simmilar.
    - Implemented generation-based cooking system in Operator base class
    - All ~50 visual operators now use needsCook()/didCook()/markDirty() pattern
    - Pure operators skip processing when inputs unchanged
    - Time-dependent operators (Noise, Gradient, PointSprites) only cook when animated
    - Streaming operators (VideoPlayer, Webcam, Feedback, Particles) always cook
    - Audio operators skipped (special threading requirements) 
[x] Is the Fluent pattern really helping the average user? Or does it obscure more common c++ patterns and syntax?
[x] Mac: Please make a reminder for yourself that you don't need the Molten environment variables. That is from vivid_v2, but not relevant here. 
[ ] Windows: It seems like display settings were customized for the Mac Retina display, but on windows things look too big.
[x] Is it possible to add smooth shading in geometry-showcase?
[x] instancing-demo -- each asteroid should have variations in scale
[x] I'm not sure hot-relading is working. It seems to crash when chain.cpp is edited.
[x] In the Readme, what is a more graceful way to say "Designed from the ground up to be friendly with coding agents/LLMs."
[ ] Idea: what if it's NOT POSSIBLE to set parameters directly in the c++, but ONLY in the sidecar file? pros and cons?
[ ] Idea: Maybe operators should declare all properties in a single structure in the constructor. Would that simplify the operator declaration?
[ ] We *should* be using AVLooperPlayer on Mac for looping vieo, but we can't seem to get it to work. 
[x] in the chain visualizer, the operator controls shouldn't be INSIDE the node -- there should be a separate panel that shows up on the right side of the window when you select a node -- this is where you will be able to adjust parameters. It should be like the inspector panel in TouhDesigner. This is a major change, so let's plan how we will do it. 
[x] Make an example that plays a nice little melody. There should be pads and a lead with a subtle beat. It should be in a minor key, and it should have a "verse, chorus, verse, bridge, chorus, outro" structure. 
[x] Is it true that the texture composite operator currently only accepts 2 inputs? It should take an arbitrary number of input.
[x] Let's make a plan to overhaul the examples folder. A lot of the examples are more like functionality tests, when they should be curated to be the most helpful possible for the users. However, we should also start building out a test suite, whcih I think is mentioned in the ROADMAP.
[ ] PLAN: How can we prepare for the first release on github? What are best practices for a project like this? What github actions can we create that will help automate the process of making a release?
[x] there should be section of examples called "showcase" that shows the *best* of what vivid can do. Let's make some impressive examples. We should have screenshots of these examples in the README 
[x] Let's make a showcase with a couple of GLTF models and a depth of field effectI 
[ ] can we proactively add the VIVID macros throughout the core and addons? I've been mostly developing on Mac, so it's probably missing in a lot of places.
[x] WINDOWS BUG: Snapshot save fails - couldn't save PNG
    - Implemented saveSnapshot() in video_exporter_win.cpp using stb_image_write
    - Added stb_SOURCE_DIR to core CMakeLists.txt include paths
[x] WINDOWS BUG: Audio not playing in audio-reactive example
    - AudioFile and AudioIn were using process() for audio generation
    - The pull-based audio system calls generateBlock() from the audio thread
    - Added generateBlock() override to AudioFile and AudioIn to generate audio properly
[x] WINDOWS BUG: 3D rendering low res in fullscreen - texture not resizing
    - Added resize checks to Render3D, InstancedRender3D, and all 2D effects
    - Added checkResize() helper to TextureOperator base class  
[ ] We need an example of mutliple videos playing at once, and being used as a texture. 
[ ] Make an example with assets/textures/flat_earth_Largest_still.0330.jpg
[ ] Take more snapshots of showcase projects, add them to README
[x] Wipeout-viz system crash after 30 seconds
    - Root cause: Multiple GPU command buffer submissions per frame without explicit GPU synchronization
    - Fix: Added `wgpuDevicePoll(device, false, nullptr)` after `wgpuSurfacePresent()` in `core/src/main.cpp:680`
[ ] Chain visualizer: Render3D thumbnail shows black in wipeout-viz but works in 3d-basics
    - Issue persists even with explicit .resolution(1280, 720)
    - Operators after Render3D (Downsample, Dither, CRTEffect) also show black
    - Final output displays correctly, so textures ARE being rendered properly
    - Likely an ImGui/WebGPU texture binding or format issue specific to chained operators
[x] wipeout-viz: please refactor the example into classes. There should be a class for each major part of the craft.
[x] we need a color class that enables conversions between color formats (RGB, HSV, hex), color blending/lerping, and will make the example code more readable. Let's add some static variables with color names as well. DO any of the libraries we're already using have a color class? It seems too simple to involve a 3rd party library, but maybe I'm wrong?
[ ] What's happening with the docs action? https://github.com/seethroughlab/vivid/settings/pages It's not published anywhere accessable. 
[x] does our canvas api match the HTML canvas api?
    - No, current API is immediate-mode with per-call colors
    - HTML Canvas is stateful with path-based drawing
    - Added detailed alignment plan as Phase 5b in ROADMAP.md
[x] It took you a LONG time to figure out why the canvas was rendering to the screen, though that seems like a simple issue. You had to look through dozens of source files. What would have helped you diagnose that issue faster?
    - Added `VIVID_DEBUG_CHAIN=1` environment variable for debug logging
    - Added `chain.setDebug(true)` programmatic option
    - Debug output shows each operator, its type, and which one is SCREEN OUTPUT
    - Documented in docs/CHAIN-API.md under "Debugging" section 
[ ] Do we have an ortho camera? 
[x] There are a lot of operators whose parameters aren't exposed like they should be. Camera is one of them. Please do an exploration of which operators don't expose their parameters
    - vivid-effects-2d: 100% coverage (27/27 operators)
    - vivid-audio: 100% coverage (26/26 operators)
    - vivid-render3d: ~5% coverage - only DepthOfField complete
    - vivid-video: 0% coverage
[ ] HIGH PRIORITY: Add parameter exposure to CameraOperator (has params() but missing getParam/setParam)
[ ] HIGH PRIORITY: Add parameter exposure to Render3D (shadingMode, metallic, roughness, ambient, etc.)
[ ] HIGH PRIORITY: Add parameter exposure to VideoPlayer (loop, volume, speed)
[ ] HIGH PRIORITY: Add parameter exposure to geometry primitives (Box, Sphere, Cylinder, Cone, Torus)
[ ] MEDIUM: Add parameter exposure to light operators (DirectionalLight, PointLight, SpotLight)
[ ] MEDIUM: Add parameter exposure to InstancedRender3D (has params() but missing getParam/setParam)
[ ] MEDIUM: Add parameter exposure to Boolean (operation type)
[ ] MEDIUM: Add parameter exposure to IBLEnvironment, GLTFLoader
