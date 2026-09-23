# Building and using ShaderTool for Quest shaders

This guide records the working Windows workflow used to compile Area 51 shader containers (`.ecs`) with the Git version of `Apps/ShaderTool`. ShaderTool runs on the PC; its generated Vulkan binaries are used by the Quest build.

## Requirements

- Visual Studio 2022 Build Tools with the **Desktop development with C++** workload and the v143 toolset.
- A previously built Area 51 checkout. ShaderTool links the engine's `xCore` libraries.
- The local DXC package under `build/shader-tool-deps`:
  - `inc/dxcapi.h`
  - `lib/x64/dxcompiler.lib`
  - `bin/x64/dxcompiler.dll`

`build/` is ignored by Git, so the DXC package is a local build dependency and is not committed. If it is missing, restore/provision that package before building ShaderTool. The DXC source tree under `xCore/3rdParty/DXC` is not a drop-in replacement for these prebuilt files in this checkout.

## Build ShaderTool

Run from the repository root in PowerShell. If Visual Studio is installed elsewhere, adjust the MSBuild path.

```powershell
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$dxcRoot = (Resolve-Path 'build\shader-tool-deps').Path
& $msbuild 'Apps\ShaderTool\ShaderTool.vcxproj' /m `
  /p:Configuration=Release /p:Platform=x64 `
  /p:BuildProjectReferences=false `
  "/p:DxcRoot=$dxcRoot" `
  /p:PostBuildEventUseInBuild=false
if ($LASTEXITCODE -ne 0) { throw 'ShaderTool build failed' }
```

`BuildProjectReferences=false` avoids rebuilding the old VS projects for `xCore`, whose project files refer to source files absent from this checkout. The normal CMake engine build supplies their libraries. For this legacy VS project, place the matching x64 Release libraries in these paths before building, if they are not already present:

```text
build/shadertool-win/xCore/x_files/Release/x_files.lib
    -> xCore/x_files/_x64Release/x_files.lib
build/shadertool-win/xCore/Parsing/Release/Parsing.lib
    -> xCore/Parsing/_x64Release/Parsing.lib
build/shadertool-win/xCore/Auxiliary/CommandLine/Release/CommandLine.lib
    -> xCore/Auxiliary/CommandLine/_x64Release/CommandLine.lib
```

The `build/shadertool-win/...` libraries come from the host CMake build of this checkout. Do not copy ARM/Quest libraries here: ShaderTool is a Windows x64 host program.

The executable is `Apps/ShaderTool/_x64Release/ShaderTool.exe`. Since the post-build event is disabled above, copy DXC beside it:

```powershell
Copy-Item -LiteralPath 'build\shader-tool-deps\bin\x64\dxcompiler.dll' `
  -Destination 'Apps\ShaderTool\_x64Release\dxcompiler.dll' -Force
```

## Compile shaders

ShaderTool accepts one or more script paths and the `-clean` and `-v` options:

```text
ShaderTool [-clean] [-v] <script.shaderscript>
```

The checked-in full game script is `Apps/GameApp/media/GameApp.shaderscript`. Its paths are for the original asset build environment, so make a working copy and set absolute paths for this checkout. For example:

```powershell
$shaderRoot = (Resolve-Path 'Apps\GameApp\media\shaders').Path.Replace('\', '\\')
$outputRoot = (Join-Path (Resolve-Path 'build').Path 'shader-tool-output\full-game').Replace('\', '\\')
$scriptText = Get-Content -LiteralPath 'Apps\GameApp\media\GameApp.shaderscript' -Raw
$scriptText = $scriptText -replace 'source_root\s+shaders', "source_root $shaderRoot"
$scriptText = $scriptText -replace 'out_root\s+\S+', "out_root $outputRoot"
Set-Content -LiteralPath 'build\GameApp-local.shaderscript' -Value $scriptText -Encoding ascii

$tool = (Resolve-Path 'Apps\ShaderTool\_x64Release\ShaderTool.exe').Path
& $tool -v 'build\GameApp-local.shaderscript'
if ($LASTEXITCODE -ne 0) { throw 'Shader compilation failed' }
```

The script writes outputs under `<out_root>\release`. Use `-clean` when intentionally rebuilding and replacing every output declared by that script. It removes old outputs for those shaders before compiling them; avoid it when you only want an incremental compile.

For a focused shader experiment, make a separate `.shaderscript` containing only the variants being tested. Set `source_root` to the absolute `Apps/GameApp/media/shaders` path and `out_root` to a dedicated directory under `build/`. Double each backslash in absolute paths in the script, as in the generated scripts in this checkout. The script format is line based; see the checked-in game script for examples. Each shader declaration names the output, stage, HLSL file, entry point, and shader model. Use `targets all` and `binding_model sdl` for the current Quest ECS containers. A small test script is useful for iteration, but run the complete game script when you need to regenerate the full shader set.

## Check and stage Quest outputs

The script output should include `.ecs` containers plus per-target intermediate files. The Quest uses Vulkan, which ShaderTool emits when the target mask includes `vulkan` (the `all` target also emits D3D12). Check the command log for successful Vulkan compilation and ECS output for each declared shader.

The current Quest asset staging directory is `build/quest3-data/SHADERS`. Copy only the shader containers that were deliberately rebuilt into that directory; keep the filenames unchanged. The running game reads assets from `/sdcard/Area51`, with shader containers under `/sdcard/Area51/SHADERS`. Install/copy updated containers there only after checking the target device and confirming the output set. Do not launch the game as part of shader compilation; launch it manually for the headset test.

For a focused pixel shader change, compare the generated `.ecs` files with the prior versions and stage just those pixel variants. File size changes can confirm that output changed, but do not establish a frame-time improvement; measure performance in the headset.
