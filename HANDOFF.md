# Handoff notes

Originally written at `d853af9`; updated after the first Windows build (UE 5.5
and 5.8) and again after the first runtime test against Resolume, for whoever
picks this up next. The README is the user-facing documentation; this file is
the engineering state: what is uncertain, what was decided on purpose, and what
to do first.

---

## Where things stand

The plugin **compiles cleanly on UE 5.5 and UE 5.8** (Windows, MSVC 14.44) and
**has been run end to end against Resolume Arena on both**, with the D3D12 debug
layer on (`-d3ddebug`). 5.6 and 5.7 have still not been built. The only build
warning left is C4828 from a non-UTF-8 byte in a comment of the vendored
`SpoutCopy.cpp`, left alone on purpose (see *Vendored SDK*).

What was exercised, on an RTX 4090 with Resolume 7 on the same machine:

| Path | Result |
|---|---|
| Cine camera → Resolume | live, correct exposure and colour |
| Resolume → receiver → loopback sender → Resolume | identical to the source: max error 0.0007 (under 1/255), no flip, no double gamma |
| Game viewport source (`-game` and PIE) | live, colours match the screen, the 10-bit format is accepted by Resolume |
| Media Output (scene viewport capture in PIE) | live, matches the viewport source within 1/255 |
| Editor world, *Editor and game* policy | live in the foreground, in the background and with the editor minimised (120 fps) |
| 4 PIE start/stop cycles, live resize 1080p → 720p, live rename | no crash, senders appear and disappear cleanly |
| Clean quit | every sender leaves Spout's directory |
| D3D12 debug layer | no errors, apart from the benign `ReflectSharedProperties` note below |

The test harness lives outside the repository (`C:\SB\h55`, `C:\SB\h58`): a
Python script that builds a test level, editor scripts that drive PIE and the
Media Output, and `spoutgrab`, a small tool built on the vendored SDK that dumps
any sender as float RGBA so two senders can be compared pixel for pixel.

### What the first run found

| Symptom | Cause | Fix |
|---|---|---|
| Crash on the first frame sent: `!bInitializedSerializationHistory` | the global shaders lived in `SpoutUE`, which loads at `Default`; shader types must exist before the global shader map is built | new `SpoutUEShaders` module, no UObjects, loaded at `PostConfigInit`, owns the shader types and the `/Plugin/SpoutUE` mapping |
| Debug layer: the 11On12 copy reads `Spout.SenderSurface` in `RENDER_TARGET`, the next draw finds it in `COPY_SOURCE` | the D3D12 RHI never applied the `RTV → CopySrc` transition we asked for, so Unreal and 11On12 disagreed about the state | the wrapper is declared in the state Unreal believes the surface is in (`RENDER_TARGET`) and 11On12 issues its own barriers around its copy; the sender always ends on a raster write so that belief is deterministic |
| Receiver surface: the same class of problem, mostly invisible to the debug layer | a shader-resource-only texture gets **no** state tracking in the D3D12 RHI; every transition on it is dropped and it is assumed to sit in SRV forever | wrapper declared in SRV, no Unreal-side transition |
| D3D11 work could reach the queue ahead of the frame it copies | the copy was recorded from the render thread while Unreal's commands for that frame were still on the RHI thread | all 11On12 work goes through `FSpoutD3D12Device::RunOnGraphicsQueue_RenderThread` (see *Decisions*) |
| Viewport source and Media Output washed out | "Match viewport" encoded a frame that is already display-encoded | for those two sources Match viewport passes the frame through |
| Viewport source in PIE published the whole editor window | in PIE the back buffer is the editor's main window | cropped to the game viewport widget's rectangle |
| Viewport source never started on 5.8 `-game` | the game window appears a few frames after `BeginPlay` and binding was tried once | retried from the tick, quietly |
| Media Output stuck in `Preparing`, no frame | the implementation never called `SetState(Capturing)` | called at the end of `InitializeCapture`, as Epic's own captures do |
| Editor-world senders froze whenever the editor lost focus | without a realtime viewport the editor ticks its world in `TimeOnly` mode; with *Use Less CPU when in Background* it pushes a non-realtime override on every viewport each frame; minimised, it caps itself at 3 fps | while an editor-world stream runs, the editor module holds a realtime override on the level viewports, suspends that setting in memory and registers a `ShouldDisableCPUThrottling` delegate |
| Commandlets (cooking, scripted level building) logged `Error: ... cannot start` | components auto-started without an RHI | no auto-start under `IsRunningCommandlet()` |

### Fixes the first build needed

| Where | Problem | Fix |
|---|---|---|
| `SpoutUEExtensions.h` | `SPOUTSDK_API` expands to `DLLEXPORT`, which the vendored sources never get a definition for | export with the SDK's own `SPOUT_DLLEXP` |
| `SpoutMediaCapture.cpp` | `UMediaCapture::GetMediaOutput()` does not exist | read the protected `MediaOutput` member (confirmed in both engines) |
| `SpoutSenderComponent` | 5.8's `OnBackBufferReadyToPresent` passes `ISlateViewportProvider&` instead of the back buffer | generic lambda + `GetBackBuffer` overloads, the provider one behind `__has_include`; delegate tracked by handle |
| `SpoutUE.Build.cs` | 5.5's `D3D12RHI` does not re-export `d3dx12.h` | `AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12")` |
| four `.cpp` files | `UWorld` used through transitive includes only; broke non-unity builds | include `Engine/World.h` |

### Building it locally — gotchas met on this machine

- **Path length.** UBT refuses intermediate paths over 260 characters. Package
  into something short (`-Package=C:\SB\p58`), not a deep temp folder.
- **UE 5.5 + Jawset Postshot.** The Marketplace install ships
  `UnrealEditor-PostshotEditor.dll` without `Source/Editor`, and UBT then fails
  *every* C++ editor build on that engine with "Unable to find module
  'PostshotEditor'", even with the plugin disabled. `RunUAT BuildPlugin` cannot
  work around it. What works: a host project carrying an empty
  `Plugins/Postshot/Postshot.uplugin` (no modules) that shadows the engine one.
  Reinstalling or removing Postshot fixes it for good.
- **Game targets need a C++ host project.** A content-only project's
  `UnrealGame` build just reuses the engine's prebuilt executable and never
  compiles the plugin.
- **A crash leaves `CrashReportClientEditor.exe` holding the plugin DLLs**, and
  the next link fails with LNK1104 until it is closed.

| Module | What it is |
|---|---|
| `SpoutSDK` | Spout 2 SDK vendored verbatim (upstream `c2bcc12`, BSD 2-Clause) + `FSpoutSenderDX` |
| `SpoutUEShaders` | the conversion and overlay global shaders, loaded at `PostConfigInit` |
| `SpoutUE` | D3D11On12 bridge, streams, components, actors, subsystem, settings, Blueprint API |
| `SpoutUEMedia` | `USpoutMediaOutput` / `USpoutMediaCapture` |
| `SpoutUEEditor` | sender picker, live sender status, keeping the editor awake while Spout runs in it |

Target: **UE 5.5 → 5.8**, Windows 64-bit, **D3D12 only**.

---

## What is still open

### 1. 5.6 and 5.7

Never built. The viewport source picks its back buffer overload by whether
`Slate/SlateViewportProvider.h` exists; that is right for 5.5 and 5.8 and relies,
on the two releases in between, on the header and the delegate signature having
changed together. The other engine APIs this depends on (`RHIRunOnQueue`,
realtime overrides, `UEditorPerformanceSettings`, `UMediaCapture::CaptureSourceType`)
are identical in 5.5 and 5.8.

### 2. Editor and PIE publish under the same name

While PIE runs, the editor-world senders keep publishing, so the PIE ones come
up as `Name_1`, and a receiver following `Name` keeps showing the editor view.
Whether editor-world streams should pause during PIE is not decided yet.

### 3. `ReflectSharedProperties` debug-layer note

Once per wrapped resource, the debug layer reports *"Resource provided was not
shared by D3D11, or with a D3D11 desc."* It comes from the 11On12 runtime probing
a resource it did not create, does not repeat per frame and has no visible
effect.

### 4. Not exercised yet

- UMG burn-in (`BurnInWidgetClass`) and captured alpha (`r.PostProcessing.PropagateAlpha`).
- HDR output: the viewport source assumes an SDR, display-encoded back buffer.
- Laptops with switchable graphics, and multi-GPU.

A packaged Shipping game of a Blueprint-only project, using the plugin installed
in the engine by the installer, publishes and cleans up correctly on 5.8. When
testing one: Shipping ignores a map given on the command line, so the level with
the Spout actors has to be the project's default map.

---

## Installer

`Installer\Build-Installer.ps1` → `Installer\out\SpoutUE-Setup-<version>.exe`.
The exe is a small WinForms program (`SpoutUEInstaller.cs`, C# 5 for the
compiler in `Windows\Microsoft.NET\Framework64\v4.0.30319`) with one zip per
engine version embedded as `Payload.<major>.<minor>.zip`. A build only loads in
the minor version it was compiled for, so versions without a payload are listed
but cannot be ticked. It installs to `Engine\Plugins\Marketplace\SpoutUE`,
unpacking next to the destination and swapping folders so a failure leaves the
previous install intact, and refuses while that engine's editor runs.

It is manifested `asInvoker` and asks for elevation itself, so `/engineroot`
runs (the build script's self-test) need no UAC prompt.

Verified on this machine: installed into UE 5.8 from the exe, loaded from
`Marketplace` by a blank project with no plugin of its own, all streams live in
the editor, in PIE, and in Development and Shipping packages of that project.
Reinstalling over an existing install, uninstalling twice and asking for a
version without a build were checked from the command line.

**The 5.5 payload cannot be built here yet:** `BuildPlugin` on 5.5 stops on the
broken Postshot install described above. Repair or remove Postshot, then run the
build script again; it picks up every engine it finds.

## Decisions made on purpose

Do not "fix" these without reading why.

**Unreal's typeless formats.** `PF_B8G8R8A8` is registered as
`DXGI_FORMAT_B8G8R8A8_TYPELESS` so the engine can create both UNORM and
UNORM_SRGB views. A Spout sender advertising a typeless format is unusable —
receivers cannot know how to interpret the bits, and OBS shows nothing at all.
`FSpoutSenderDX::SendTextureAs` exists solely to publish an explicit typed
format instead of the one read off the texture. It is a subclass rather than a
patch so the vendored sources stay refreshable from upstream.

**All 11On12 work runs from `RHIRunOnQueue`.** Recorded from the render thread,
it reaches the GPU queue before Unreal's own commands for the same frame, which
are still on the RHI thread. `RunOnGraphicsQueue_RenderThread` dispatches what
the render thread has recorded, then enqueues a lambda that calls
`ID3D12DynamicRHI::RHIRunOnQueue`: the callback runs on Unreal's submission
thread, after everything recorded before it and before anything recorded after.
This is the pattern Epic's NNE plugin uses. The receiver pays for it with one
frame of latency on its connection state (not on its pixels), and teardown
flushes the RHI thread before deleting a `spoutDX`, since a callback may still
be using it.

**Wrappers are declared in the state Unreal believes the resource is in**, not
in the state the copy needs. 11On12 transitions around its own work and hands
the resource back where it found it; Unreal never sees any of it, so its view
stays right. Moving the resource on the Unreal side first does not work: the
D3D12 RHI drops transitions on untracked resources, and in practice did on the
sender surface too.

**Global shaders in a module of their own.** `SpoutUEShaders` has no UObjects so
it can load at `PostConfigInit`; `SpoutUE` cannot.

**The editor is kept awake while Spout runs in it.** Off World Live asks users to
untick *Use Less CPU when in Background*; here the editor module does it in
memory, for as long as an editor-world stream exists, and puts it back after.
The restore happens on `OnEnginePreExit`, because by the time `ShutdownModule`
runs on exit the settings object is gone.

**One shared D3D11On12 device.** Every sender and receiver uses the same bridge,
reached through `GetSpoutDevice()` in `SpoutUE.h`. A second 11On12 device would
mean a second D3D11 runtime for no benefit. It is a free function rather than a
module method because the streams reach for it on the render thread, and
`FModuleManager` is not something to touch from there.

**Senders and receivers always go through an intermediate texture.** It costs one
pass. It buys a resource whose lifetime we control (so the 11On12 wrapper is
created once rather than per frame), a guaranteed non-typeless format, and
somewhere to apply the gamma, alpha and flip conversions. A zero-copy path that
wraps the user's render target directly is possible and is *not* implemented —
see below.

**No Place Actors category.** `FPlaceableItem`'s constructors shift between
releases and a compile error in the editor module takes the whole editor build
down. The actors are placeable UCLASSes, so the panel already finds them by
typing "Spout". Not worth the upgrade tax.

**The engine floor lives in `SpoutUETypes.h`, not in the `.uplugin`.** The
descriptor carries no `EngineVersion`, so the engine will not refuse to load or
offer to rebuild. An unsupported engine stops at one readable `#error` instead.

---

## Known gaps

Things that are deliberately missing, not bugs.

- **`FSpoutSenderInfo::FrameRate` and `FrameNumber` are always 0** in the
  subsystem's directory listing. Those values live behind a per-sender frame
  count semaphore that only a connected receiver opens. Populating them for the
  whole list would mean opening every sender's semaphore on every poll. The
  fields are real on a connected receiver's `GetStats()`.
- **No zero-copy send path.** Wrapping a `UTextureRenderTarget2D`'s own resource
  and skipping the intermediate would remove one full-frame copy when no
  conversion is needed and formats already match. It is the obvious next
  optimisation, and it is also where the resource-state hazards get sharper.
  Measure before building it.
- **Receiving does not go through `UMediaPlayer`.** Frames land in a render
  target, which is what materials and Composure want. A full `IMediaPlayer`
  implementation is a large piece of work for a narrow gain.
- **No automated tests.** There is no way to test GPU texture sharing without a
  GPU, a Windows box and a second application.
- **The capture component uses `SceneCapture`**, so it gets post processing,
  depth of field and ray tracing but not TSR or DLSS — temporal upscaling only
  exists in the main view. The viewport source mode is the answer when those are
  needed, and it is why that mode exists at all.

## Things worth checking at runtime, beyond "does it build"

Checked and fine: renaming and resizing a live sender, four to five senders and
a receiver in one process, PIE start/stop cycles, a clean quit. Still to check:

- `r.PostProcessing.PropagateAlpha` changed meaning around 5.5 (it used to have
  a mode 2; it is effectively a bool now). The capture component reads it with
  `GetInt()` and only warns, so a change in semantics degrades to a spurious
  warning rather than a failure — but verify the alpha actually comes through.
- **Laptops with switchable graphics.** Spout cannot share across adapters;
  Unreal and the receiving application must both be on the discrete GPU. A
  "sender exists but shows black" report is almost always this.
- **Run the D3D12 debug layer after touching the streams.** `-d3ddebug` on the
  command line; anything other than the `ReflectSharedProperties` note means the
  wrapper states and Unreal's view have drifted apart again.

## Vendored SDK

`Source/ThirdParty/SpoutSDK/Spout/` is upstream `leadedge/Spout2` at commit
`c2bcc12`, unmodified. Only the files `SpoutDX` needs are included; the OpenGL
half and `SpoutDX12` were left out (the plugin implements its own 11On12 layer).
To refresh, copy the same file list from a newer upstream — nothing in that
folder has been edited, so there is no patch to reapply. The one behaviour change
lives beside it in `Public/SpoutUEExtensions.h`.
