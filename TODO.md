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
[x] Windows: It seems like display settings were customized for the Mac Retina display, but on windows things look too big.
[x] Is it possible to add smooth shading in geometry-showcase?
[x] instancing-demo -- each asteroid should have variations in scale
[x] I'm not sure hot-relading is working. It seems to crash when chain.cpp is edited.
[x] In the Readme, what is a more graceful way to say "Designed from the ground up to be friendly with coding agents/LLMs."
[x] Idea: what if it's NOT POSSIBLE to set parameters directly in the c++, but ONLY in the sidecar file? pros and cons?
[x] in the chain visualizer, the operator controls shouldn't be INSIDE the node -- there should be a separate panel that shows up on the right side of the window when you select a node -- this is where you will be able to adjust parameters. It should be like the inspector panel in TouhDesigner. This is a major change, so let's plan how we will do it. 
[x] Make an example that plays a nice little melody. There should be pads and a lead with a subtle beat. It should be in a minor key, and it should have a "verse, chorus, verse, bridge, chorus, outro" structure. 
[x] Is it true that the texture composite operator currently only accepts 2 inputs? It should take an arbitrary number of input.
[x] Let's make a plan to overhaul the examples folder. A lot of the examples are more like functionality tests, when they should be curated to be the most helpful possible for the users. However, we should also start building out a test suite, whcih I think is mentioned in the ROADMAP.
[x] PLAN: How can we prepare for the first release on github? What are best practices for a project like this? What github actions can we create that will help automate the process of making a release?
[x] there should be section of examples called "showcase" that shows the *best* of what vivid can do. Let's make some impressive examples. We should have screenshots of these examples in the README 
[x] Let's make a showcase with a couple of GLTF models and a depth of field effectI 
[x] can we proactively add the VIVID macros throughout the core and addons? I've been mostly developing on Mac, so it's probably missing in a lot of places.
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
[x] Wipeout-viz system crash after 30 seconds
    - Root cause: Multiple GPU command buffer submissions per frame without explicit GPU synchronization
    - Fix: Added `wgpuDevicePoll(device, false, nullptr)` after `wgpuSurfacePresent()` in `core/src/main.cpp:680`
[x] Chain visualizer: Render3D thumbnail shows black in wipeout-viz but works in 3d-basics
    - Issue persists even with explicit .resolution(1280, 720)
    - Operators after Render3D (Downsample, Dither, CRTEffect) also show black
    - Final output displays correctly, so textures ARE being rendered properly
    - Likely an ImGui/WebGPU texture binding or format issue specific to chained operators
[x] wipeout-viz: please refactor the example into classes. There should be a class for each major part of the craft.
[x] we need a color class that enables conversions between color formats (RGB, HSV, hex), color blending/lerping, and will make the example code more readable. Let's add some static variables with color names as well. DO any of the libraries we're already using have a color class? It seems too simple to involve a 3rd party library, but maybe I'm wrong?
[x] does our canvas api match the HTML canvas api?
    - No, current API is immediate-mode with per-call colors
    - HTML Canvas is stateful with path-based drawing
    - Added detailed alignment plan as Phase 5b in ROADMAP.md
[x] It took you a LONG time to figure out why the canvas was rendering to the screen, though that seems like a simple issue. You had to look through dozens of source files. What would have helped you diagnose that issue faster?
    - Added `VIVID_DEBUG_CHAIN=1` environment variable for debug logging
    - Added `chain.setDebug(true)` programmatic option
    - Debug output shows each operator, its type, and which one is SCREEN OUTPUT
    - Documented in docs/CHAIN-API.md under "Debugging" section 
[x] There are a lot of operators whose parameters aren't exposed like they should be. Camera is one of them. Please do an exploration of which operators don't expose their parameters
    - vivid-effects-2d: 100% coverage (27/27 operators)
    - vivid-audio: 100% coverage (26/26 operators)
    - vivid-render3d: ~5% coverage - only DepthOfField complete
    - vivid-video: 0% coverage
[x] HIGH PRIORITY: Add parameter exposure to CameraOperator (has params() but missing getParam/setParam)
[x] HIGH PRIORITY: Add parameter exposure to Render3D (shadingMode, metallic, roughness, ambient, etc.)
[x] HIGH PRIORITY: Add parameter exposure to VideoPlayer (loop, volume, speed)
[x] HIGH PRIORITY: Add parameter exposure to geometry primitives (Box, Sphere, Cylinder, Cone, Torus, Plane)
[x] MEDIUM: Add parameter exposure to light operators (DirectionalLight, PointLight, SpotLight)
[x] MEDIUM: Add parameter exposure to InstancedRender3D (has params() but missing getParam/setParam)
[x] MEDIUM: Add parameter exposure to Boolean (operation type)
[x] MEDIUM: Add parameter exposure to IBLEnvironment, GLTFLoader
[x] While diagnosing a few bugs recently, the solution has been to "lock resolution", which seems to be related to the idea that things automatically get resized when the window size changes. But this isn't how thing should work (with the notabl exception of MAYBE the screen texture). Users should declare the resolution of canvases once. Also, when movies and images are loaded, they should use the size of the media as the default resolution, but then the user can decide the drawing dimensions in their chain. If this isn't how it currently works, let's make a plan to make this change.
[x] BUG: Canvas text rendering appears directly on screen instead of on canvas texture
    - Root cause: `Canvas::size()` only set `m_resolutionLocked = true` when dimensions changed
    - When calling `size(1280, 720)` on a freshly created Canvas (which defaults to 1280x720), the lock was never set
    - This allowed `checkResize()` to resize the canvas to window dimensions (2560x1440) on subsequent frames
    - Text vertices were generated at canvas coordinates but rendered with window-size uniforms, causing misalignment
    - Fix: Always set `m_resolutionLocked = true` in `Canvas::size()` regardless of whether dimensions changed 
[x] Please add a comand line snapshot feature that you (claude) can use in the future to evaluate your work. Also add this feature to the documentation so that it's obvious that this feature is available in the future.
[x] Do we have an ortho camera?
    - Added ProjectionMode enum (Perspective, Orthographic) to Camera3D
    - Added projectionMode() and orthoSize() to Camera3D and CameraOperator
    - Usage: `camera.orthographic().orthoSize(10.0f)` for ortho, `camera.perspective().fov(60.0f)` for perspective
[x] add a formant generator to the audio synthesis addon
[x] I have a new vision for the VSCode extension. Let's do some planning. When a user clicks/highlights a node in VSCode, it automatically shows a preview of the node in the running project. Meanwhile, in VSCode, all of the parameters are showed in VSCode -- sliders, color choosers, file choosers. (for movies, sounds, texures, models), and the input and output nodes. Help me figure out how to flesh out this idea and integrate it into the extension. When a parameter is adjusted in the VSCode extension, it should be reflected immeiately in the code. 
[x] PLAN: Maybe operators should declare all properties in a single structure in the constructor. Would that simplify the operator declaration?
[x] PLAN: addons should be self-contained (to prepare for community addon registry), so this probably means moving examples, tests, and any needed assets to the respective addon folders as well. Each addon should have its own README.md 
[x] PLAN: how much would it increase the runtime size to incorporate vivid-io, vivid-network, and vivid-effects-2d, vivid-video and vivid-render3d into the core?
[x] VSTHost using [JUCE](https://juce.com/) 
[x] PLAN: Is the vivid runtime currently distributed with the addon? Should it be?
[x] Have all of the operators now been updated? I'm specifically thinking about light operators (DirectionalLight, PointLight, SpotLight), InstancedRender3D, Boolean (operation type), and IBLEnvironment, GLTFLoader, which don't even have exposed parameters yet. 
[x] What's happening with the docs github action?  It's not published anywhere accessable. I'm sure I haven't set up something properly at https://github.com/seethroughlab/vivid/settings/pages, but I'm not sure what. 
[x] Why does canvas.h still have m_resolutionLocked? Why would we need to lock and unlock the resolution? We made a change a while ago that textures shouldn't be linked to the size of the window, and I thought would eliminate the need to "lock" the size of things. 
[x] Make a globe example with assets/textures/flat_earth_Largest_still.0330.jpg
[x] It seems that there is a lot of repeated code in a lot of the core effects. Would you recommend any optimizations? Perhaps more inheritence?
[x] Phase 5b.1: State Management
[x] git tag v0.1.0 && git push --tags
[x] PLAN: We need to add support for serial communication (focus on DMX over serial for Enttec devices). But should it be in core? Or in a separate addon (~/Developer/vivid-serial)? WHat are the pros and cons of each approach. If it's an addon, make sure it registers itself as an addon, and registers all of its parameters
[x] there are a bunch of places in the runtime where you are manually constructing json structures with long strings. Let's please change these occurances to using the json library that is already part of the core. 
[x] Bundler/Release build should suport: Web, iOS, Mac, Windows, Raspberry Pi
[x] Can we add a raspberry pi build to the github action?
[x] Please add an action to ~/Developer/vivid-vscode: Vivid: Create Node Template -- allows users to make their own custom operator. But how? Perhaps it makes a new file for the node with template code? The update the ~/Developer/vivid-vscode/README.md to document all of the actions.
[x] We've been adding a lot of new features that haven't been tested. Let's make an extensive plan on hwo to test all of the functionality. 
[x] What else (if anything) can be turned into an addon that would make it easier, in its absence, to create the web exporter (see WEB_EXPORT.md) 
[x] can you please move ~/Developer/vivid-serial to ~/Developer/vivid/addons
[x] what's the difference between ./tests and ./testing-fixtures?
[x] Create a new document with ideas for getting started with vivid that play to vivid's strengths. Add 10 right away, but I'll want to add lots more. 
[x] Find out what the maximum number of tokens Claude can read and maek the VS Code Extension warn users if their CLAUDE.md file gets longer than that. 
[x] Please take snapshots of showcase projects and save them to an "imags" folder, then add them to README in a Showcase section. The purpose is to show users what vivid is capable of.
[x] I think we need a somewhat fundamental change in the workflow for a typical user. The user should be encouraged to install the addon (~/Developer/vivid-vscode) first, and it will prompt the user to either point to an existing download of vivid or automatically download from github. Then there will be a "projects" folder inside of the vivid folder. I Hope this will improve Claude Code's ability to help with coding because it will have access to all of the source code. Does this make sense? I'm not sure how to approach this and all of the different systems that it will affect.
    - Implemented extension-first workflow in vivid-vscode:
    - Added vivid.vividPath setting for source directory
    - Onboarding flow: "Point to Existing Checkout" or "Clone from GitHub"
    - Projects default to {vividPath}/projects/my-projects/
    - RuntimeManager downloads to {vividPath}/build/
    - MCP configured with VIVID_SOURCE_DIR for Claude access
    - Migration prompt for existing ~/.vivid users
[x] Please plan a showcase app: We need an example of multiple videos playing at once - a VJ application that blends them in different ways, uses flashes of 2d and 3d geometry, and lage, bold typography. 
[x] Let's address Phase 14: Advanced Window & Input from the roadmap
[x] Please add a WEBSOCKET_API.md document that explains how you can control a vivid app with the same websockcet API that the vscode extension uses.
[x] I noticed the line if(NOT EMSCRIPTEN) in the main CMakeLists.txt, but we removed the web exporter, so there shouldn't be any refrence to EMSCRIPTEN anymore. Can you check the whole project for any remaining references? 
[x] Based on the changes we just made and the overall conventions used in this project, please make a document about coding style. What is a good name for this document that is commonly used?
[x] Does phase 5 have some overlap with the ASSET_LOADING.md document?
[x] Please examine rpi-job-logs.txt and ubuntu-job-logs.txt and fix the CI erorrs
[x] "All operators inherit from `Operator` or `TextureOperator`" -- what other base operators would you recommend?
    - See **OPERATOR_REFACTOR.md** for full analysis
    - Recommended: SignalOperator (control-rate signals), GeometryOperator (3D primitives), ComputeOperator (future) 
[x] There's still a slow memory leak in all projects. 
[x] should testing fixtures be moved to the projects folder?
    **Decision: Keep at root.** testing-fixtures/ is framework test infrastructure, not user content.
    projects/ is the user's workspace. Keeping them separate prevents accidental modification
    and maintains CI script clarity.
[x] Do you have any external proof that Cube map depth comparison sampling has issues in wgpu-native?
    **Yes - relevant GitHub issues:**
    - [#1690](https://github.com/gfx-rs/wgpu/issues/1690): Multi-layer texture rendering produced garbage
      on layers other than 0 (Vulkan/AMD). Workaround: use 6 separate textures.
    - [#2138](https://github.com/gfx-rs/wgpu/issues/2138): WebGL2 point light shadow issues
    - [PR #2143](https://github.com/gfx-rs/wgpu/pull/2143): Cube map shadow map fix (misread GL spec)
    - [#4524](https://github.com/gfx-rs/wgpu/issues/4524): textureSampleCompare returns only 0/1
    **Next step:** Try 6 separate 2D textures instead of 6-layer array (documented workaround)
[x] We just updated the wgpu-native package to try to fix the memory leak. What other packages might we want to update to a newer version? Please do an audit. 
[x] Are all of the 3rd party packages we are using compatible with the MIT license?
[x] are ASSET_LOADING.md and MEMORY_LEAK.md complete? Can we delete them?
[x] Is the 02-hello-noise noise stretched horizontally? Is it aware of aspect ratio? If not, I think ALL generators will need to be updated. 
[x] OPERATOR_REFACTOR.md
[x] Can we delete SHADOWS.md with the understanding that we still have to deal with POINT_SHADOW_INVESTIGATION.md?
[x] Is there anything we can learn from TouchDesigner's new POPs (which replace SOPs) that we can apply to our Geometry Operator? https://derivative.ca/community-post/pops-new-operator-family-touchdesigner/69468
[x] All lights and camera should have a "draw debug" boolean that will draw a WIREFRAME (NOT solid) structure that is appropriate to the subject. A Camera should show the frustum, a directional light should show the direction, spot light should show the light cone, etc.
[ ] We *should* be using AVLooperPlayer on Mac for looping video, but we can't seem to get it to work.
[ ] CLAP_HOST.md
[x] Is it safe to delete POINT_SHADOW_INVESTIGATION.md?
    - Yes, status is RESOLVED. Fixes implemented in renderer.cpp.
[ ] Please check out the three.js, which implements shadows well - albeit in WebGL instead of wgpu. Is there anything we can learn from that shadow implementation?
[ ]  testing-fixtures/shadow-point
=== Memory Tracking Started ===
[10.0s] Memory: 109 MB (total: +0 MB, last 10s: +0 MB)
[20.0s] Memory: 112 MB (total: +3 MB, last 10s: +3 MB)
[30.0s] Memory: 116 MB (total: +7 MB, last 10s: +4 MB)
[40.0s] Memory: 119 MB (total: +10 MB, last 10s: +3 MB)
[50.0s] Memory: 122 MB (total: +13 MB, last 10s: +3 MB)
[60.0s] Memory: 125 MB (total: +16 MB, last 10s: +3 MB)
[70.0s] Memory: 129 MB (total: +20 MB, last 10s: +4 MB)
[80.0s] Memory: 132 MB (total: +23 MB, last 10s: +3 MB)
[x] addons/vivid-render3d/src/renderer.cpp is getting REALLY long. How can we refactor it?
    - Extracted GPU structs to gpu_structs.h (~125 lines)
    - Extracted debug geometry to debug_geometry.h (~130 lines)
    - Created external .wgsl shader files with AssetLoader loading
    - Reduced from 4782 to 4541 lines
[ ] DEFERRED: Extract shadow system from renderer.cpp to shadow_manager.h/cpp (~600 lines)
    - Tightly integrated with main render pass (bind groups, textures)
    - Point shadows use 6 separate textures (wgpu workaround)
    - Requires careful interface design
[ ] DEFERRED: Split renderer.cpp createPipeline() into helper methods
    - Pipelines share resources (bind group layouts, uniform buffers)
    - Internal reorganization for readability