# Handoff notes

Written at commit `d853af9` on branch `claude/ue-spout-plugin-windows-q4kozn`,
for whoever picks this up next. The README is the user-facing documentation;
this file is the engineering state: what is uncertain, what was decided on
purpose, and what to do first.

---

## Where things stand

The plugin is feature-complete and pushed. **It has never been compiled.** It
was written in a Linux container with no Unreal installation and no Windows SDK,
so not one line has been through a compiler. Treat every API call as plausible
rather than verified.

Six commits, four modules, ~50 source files excluding the vendored SDK.

| Module | What it is |
|---|---|
| `SpoutSDK` | Spout 2 SDK vendored verbatim (upstream `c2bcc12`, BSD 2-Clause) + `FSpoutSenderDX` |
| `SpoutUE` | D3D11On12 bridge, streams, components, actors, subsystem, settings, Blueprint API |
| `SpoutUEMedia` | `USpoutMediaOutput` / `USpoutMediaCapture` |
| `SpoutUEEditor` | sender picker, live sender status |

Target: **UE 5.5 → 5.8**, Windows 64-bit, **D3D12 only**.

## Do this first

Build it. Everything below is guesswork until a compiler has had a look.

```
YourProject/Plugins/SpoutUE/   <- clone the branch here
```

Then generate project files and build. Expect to fix things. When the build is
green, the first functional test is the cheapest one: drop a **Spout Sender
Manager** in a level, assign any render target, set Startup Policy to *Editor
and game*, and open OBS with a Spout2 Capture source. If the image appears at
all, the whole sharing path works and what remains is polish.

---

## Ranked risk list

Ordered by how likely each is to be wrong, highest first.

### 1. `UMediaCapture` — `SpoutUEMedia`

The weakest link, and the only module that can be deleted outright. Three
specific doubts:

- `SetMediaOutput()` and `GetMediaOutput()` are assumed public on
  `UMediaCapture`. I did not verify this against real headers — it is the
  pattern the engine's own capture implementations use, but the accessor could
  be named differently or protected.
- `OnRHIResourceCaptured_RenderingThread` gained an `FRHICommandListImmediate&`
  first parameter in 5.3. Guarded with `UE_VERSION_OLDER_THAN`. If it moved
  again in 5.6+, this is where it shows.
- `InitializeCapture()` is the 5.2+ entry point. Fine for 5.5+, but it replaced
  three older virtuals, so an engine that reintroduces them would silently never
  call ours.

**If it fights you:** delete the `SpoutUEMedia` entry from `SpoutUE.uplugin`.
Nothing else references the module. The sender components cover every use case
except nDisplay and Movie Render Queue.

### 2. `ID3D12DynamicRHI` — `Private/D3D12/SpoutD3D12Device.cpp`

`RHIGetDevice(0)`, `RHIGetCommandQueue()` and `RHIGetResource(FRHITexture*)`.
Stable since 5.0 as far as I know, and it is the public interface rather than
D3D12RHI internals, but it is the tightest coupling to the engine in the whole
plugin. If this breaks there is no fallback — the plugin cannot work without
reaching Unreal's device and queue.

### 3. D3D12 resource states around the 11On12 wrapper

This is the part most likely to compile cleanly and still be wrong at runtime,
so it deserves the most scrutiny once the build is green.

`CreateWrappedResource` declares the state the D3D12 resource must be in when
`AcquireWrappedResources` is called, and the state it is left in on release. The
plugin declares `COPY_SOURCE` for the sender and `COPY_DEST` for the receiver,
and brackets each acquire with an RHI transition to the matching `ERHIAccess`.

Unreal tracks its own resource states and knows nothing about the 11On12
transitions, so the two views can drift. **Run with `-d3ddebug` early.** If the
D3D12 debug layer complains about a barrier with a mismatched before-state, that
is this. The fix is to adjust the `ERHIAccess` on either side of the acquire in
`SpoutSenderStream.cpp` / `SpoutReceiverStream.cpp` so it matches the declared
state exactly.

### 4. `FWidgetRenderer::DrawWidget` — UMG burn-in

Only reached when `BurnInWidgetClass` is set on a capture component. Low blast
radius: if it misbehaves, the burn-in is the only casualty.

### 5. `ModuleRules.bUseUnity` — `SpoutSDK.Build.cs`

Deprecated in recent UBT. A warning is expected and harmless. If it errors,
delete the line — the vendored SDK will then build inside unity blobs, which may
or may not produce symbol collisions. If it does, that is the moment to think
about it, not before.

---

## Decisions made on purpose

Do not "fix" these without reading why.

**Unreal's typeless formats.** `PF_B8G8R8A8` is registered as
`DXGI_FORMAT_B8G8R8A8_TYPELESS` so the engine can create both UNORM and
UNORM_SRGB views. A Spout sender advertising a typeless format is unusable —
receivers cannot know how to interpret the bits, and OBS shows nothing at all.
`FSpoutSenderDX::SendTextureAs` exists solely to publish an explicit typed
format instead of the one read off the texture. It is a subclass rather than a
patch so the vendored sources stay refreshable from upstream.

**`SubmitCommandsHint` before every acquire.** The conversion pass can still be
sitting in an open command list when the D3D11 side copies, which shows
receivers the previous frame. This is not defensive paranoia; remove it and you
get a one-frame lag that looks like a Spout problem.

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

- `r.PostProcessing.PropagateAlpha` changed meaning around 5.5 (it used to have
  a mode 2; it is effectively a bool now). The capture component reads it with
  `GetInt()` and only warns, so a change in semantics degrades to a spurious
  warning rather than a failure — but verify the alpha actually comes through.
- Sender **rename** tears down and recreates the sender, because Spout
  identifies senders by name. Confirm receivers re-latch rather than hanging.
- **Multiple senders in one process** share the bridge but hold separate
  `spoutDX` instances. Worth running three or four at once early.
- **PIE start/stop cycles** are where the render-thread teardown
  (`FlushRenderingCommands` in `DestroyStream` / `UnbindViewportSource`) earns
  its keep. If anything is going to crash, it is here.
- **Laptops with switchable graphics.** Spout cannot share across adapters;
  Unreal and the receiving application must both be on the discrete GPU. A
  "sender exists but shows black" report is almost always this.

## Vendored SDK

`Source/ThirdParty/SpoutSDK/Spout/` is upstream `leadedge/Spout2` at commit
`c2bcc12`, unmodified. Only the files `SpoutDX` needs are included; the OpenGL
half and `SpoutDX12` were left out (the plugin implements its own 11On12 layer).
To refresh, copy the same file list from a newer upstream — nothing in that
folder has been edited, so there is no patch to reapply. The one behaviour change
lives beside it in `Public/SpoutUEExtensions.h`.
