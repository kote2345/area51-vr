# Quest 3 single-pass stereo plan

## Goal

Render both OpenXR eyes from one scene submission using Vulkan multiview. This
is intended to reduce duplicated CPU draw submission and vertex processing.
The existing two-pass stereo path remains the fallback until every required
render pass is multiview-capable.

## Current path

- `RenderGameStereoXR()` in `Apps/GameApp/main.cpp` renders the scene once for
  each eye and selects a separate SDL GPU texture for each pass.
- `SDLEngine` creates two ordinary 2D XR render textures. The OpenXR bridge
  copies their contents into the eye swapchains.
- Geometry shaders read one `View` and one `Projection` matrix from
  `cbFrameConstants`; the vertex shader does not select a view by view index.
- SDL3 GPU's public render-pass API exposes one attachment layer at a time and
  has no multiview mask. Its Vulkan backend therefore also needs an explicit
  multiview path. Merely changing HLSL would still render only one eye.
- Scene color, depth, G-buffer, and post-processing resources have to be
  audited together. A multiview scene pass cannot safely reuse the current
  single-layer resources as if they contained both eyes.
- Shader blobs are built by `Apps/ShaderTool` from
  `Apps/GameApp/media/GameApp.shaderscript`; shader changes must use that tool
  and produce SPIR-V with the Vulkan multiview view-index semantic.

## Implementation stages

### 1. Backend and target capability

- Extend the SDL GPU Vulkan render-pass path to express a two-view mask and
  attach array views for both color and depth layers.
- Keep ordinary one-layer passes unchanged on Vulkan and other SDL backends.
- Keep OpenXR's existing per-eye swapchains and copy array layers 0 and 1 into
  them after the shared scene pass. This avoids changing compositor submission
  while the SDL multiview path is being brought up.
- Make render-target descriptors and cached target bindings retain the layer
  count and view mask so that depth and MRT attachments agree.

### 2. View data and shader variants

- Pass both eye view and asymmetric projection matrices through the frame
  render context and culling setup.
- Add multiview vertex entry points using `SV_ViewID` and per-eye matrix data.
- Register the new entry points in `GameApp.shaderscript` and compile them
  with `ShaderTool`; retain the existing shader entry points for fallback.
- Rigid, skinned, dynamic, decal, and primitive draws have shader variants.
  Post effects and gameplay UI still need an explicit multiview policy before
  the single-pass path is ready for general scenes.

### 3. Scene pipeline

- Allocate array-backed scene color, depth, and G-buffer resources for the XR
  path and render the primary opaque scene in one multiview pass.
- Make geometry submissions and visibility/culling use both eye frusta.
- Audit shadow maps, distortion, decals, fog, glow, and screen-space effects.
  Effects that are inherently per-eye must run once per layer or stay on the
  existing two-pass path until adapted.
- Switch stereo to one scene traversal only when required passes and resources
  are ready; keep the per-eye path selectable for regression comparisons.

## Acceptance checks

- The Quest Vulkan device/runtime reports multiview support; the scene is
  rendered to two array layers and copied into the existing per-eye swapchains.
- A debug scene with asymmetric left/right projections draws into both array
  layers in one scene render pass, with no eye copy from separate 2D targets.
- Captured frame commands show one opaque-scene traversal and one multiview
  render pass; image inspection confirms correct depth, culling, and lighting
  in both eyes.
- The non-VR renderer and the existing stereo fallback still use their
  existing one-view paths.

## Status

`rtarget_desc` now supports an explicit layer count, defaulting to one, and the
SDL backend creates a 2D array texture when the count is greater than one.
Rigid, skinned, dynamic, decal, and primitive geometry have separate multiview
vertex entry points using `SV_ViewID`; their legacy entry points remain as the
fallback. The variants compile through `ShaderTool`, and the managers supply
per-eye constants and create pipelines with the matching view mask.

The game still uses two-pass rendering. The Quest OpenXR-owned Vulkan device
now queries and enables Vulkan multiview when supported, and the capability is
carried into the SDL external-device setup. Engine pass and pipeline
descriptors now carry a default-zero view mask; SDL's Vulkan render-pass cache
and pipeline-compatible transient render passes include that mask. Array
targets and mask-to-layer-count validation are available in the render-target
API.

The SDL Vulkan framebuffer path now creates and uses 2D-array attachment views
for masked color, resolve, and depth targets. It transitions each selected
array layer at pass start and restores each layer at pass end. Forward passes
inherit the array view mask, and the distortion snapshot copy handles both
layers. The Quest gameplay path renders the shared G-buffer with mask `0b11`,
feeds both eye matrices to geometry, then copies each array layer into the
existing OpenXR eye swapchains. Legacy shaders and two-pass rendering remain.

The Quest Release and signed Debug APKs build successfully. The Debug APK and
the five new shader containers are installed on the connected Quest 3, but the
app has not been launched yet. Post-processing and gameplay UI are still
skipped in the multiview path; distortion primitives still sample the first
scene layer. The first headset run should confirm session startup, both eyes,
asymmetric projection, and log output before those effects are addressed.
