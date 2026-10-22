# SpoutUE

Zero-latency GPU texture sharing between Unreal Engine 5 and any Spout-capable
application — OBS Studio, Resolume, TouchDesigner, Notch, MadMapper, vMix,
Millumin, Isadora, and the rest.

Send any render target, CineCamera or the game viewport out over Spout, and pull
Spout streams back into render targets and materials. Nothing is compressed,
nothing is read back to the CPU, and nothing leaves the GPU.

> **Status:** the plugin is complete but has not yet been compiled against a real
> engine installation — see [Before your first build](#before-your-first-build).

---

## Requirements

| | |
|---|---|
| Engine | Unreal Engine **5.5 → 5.8** |
| Platform | Windows 64-bit |
| RHI | **DirectX 12** (the Unreal default on Windows) |
| GPU | A single GPU. Spout cannot share between two adapters. |

The plugin refuses to start and logs why if the project is running on D3D11,
Vulkan or a null RHI. `Is Spout Supported` and `Get Spout Unavailable Reason`
expose the same check to Blueprint.

The `.uplugin` carries no `EngineVersion`, so the engine will not refuse to load
it or offer to rebuild it for a different version. The floor is enforced in code
instead: on 5.4 or older the build stops with one readable `#error` rather than a
few hundred errors from whichever API moved.

### Why 5.5 is the floor

Two engine APIs the plugin depends on landed just below it, so supporting older
versions would mean carrying a second code path for each:

- `FRHICommandListBase::CreateTexture` (5.3) — how every intermediate and
  staging texture is allocated.
- `UMediaCapture::InitializeCapture` (5.2) — replaced the older
  `CaptureSceneViewportImpl` / `ValidateMediaOutput` entry points.

Everything else in the plugin is older than 5.0. The one override whose
signature genuinely moved inside the supported range,
`UMediaCapture::OnRHIResourceCaptured_RenderingThread` (gained an
`FRHICommandListImmediate&` in 5.3), is already guarded with
`UE_VERSION_OLDER_THAN`, so that file would also build on 5.2 if the floor were
ever lowered.

## Installing

```
YourProject/
└── Plugins/
    └── SpoutUE/          <- this repository
```

Then regenerate project files and build. There is no DLL to copy and nothing to
install system-wide: the Spout SDK is compiled into the plugin, so packaged
builds carry everything they need.

## Quick start

### Send a render target

1. Place a **Spout Sender Manager** in the level (type "Spout" in the Place
   Actors search box).
2. Assign a Render Target to **Source Render Target**.
3. Leave **Startup Policy** on *Game only*, or set it to *Editor and game* to
   see the feed while you work in the viewport.

The sender shows up immediately in OBS (Spout2 Capture source), Resolume, or
anything else on the machine.

### Send a camera

Place a **Spout Cine Camera** instead. It is a stock `ACineCameraActor` — same
filmback, focal length, focus and Sequencer behaviour — with a capture component
that renders it at broadcast resolution and a sender that publishes it. Set the
resolution on the capture component and you are done.

To output a camera you already have, add a **Spout Capture** component to it and
point a **Spout Sender** at `Get Capture Target`.

### Send the whole viewport

Set a sender's **Source Mode** to *Game viewport*. This publishes the presented
frame, so it carries everything a scene capture cannot: TSR/DLSS upscaling, UMG,
the HUD, debug draw. It requires a game viewport, which means PIE and packaged
builds — not the editor viewport.

### Receive

1. Place a **Spout Receiver Manager**.
2. Pick a sender from the dropdown, or leave it on `<Active sender>` to follow
   whichever sender Spout considers active machine-wide.
3. Read `Get Render Target` and feed it to a material, Composure, or anything
   else that takes a texture.

The render target is created and resized for you to match the sender.

### From the Media framework

Create a **Spout Media Output** asset for the routes that already speak Media IO:
nDisplay output mapping, Movie Render Queue, the Media Capture panel.

## Feature overview

| | |
|---|---|
| Send render targets | any `UTextureRenderTarget2D`, several senders at once |
| Send cameras | `ASpoutCineCameraActor`, or `USpoutCaptureComponent` on any camera |
| Send the viewport | presented frame including UMG, HUD and temporal upscaling |
| Receive | into a managed or supplied render target, auto-resizing |
| Colour | linear ↔ sRGB conversion ("match viewport"), or raw passthrough |
| Alpha | opaque, straight, premultiply, un-premultiply |
| Formats | 8-bit BGRA/RGBA, 10-bit RGB10A2, 16-bit float, 32-bit float |
| Resolutions | 240p → 4K presets, or custom up to 16K |
| Burn-in | any UMG widget composited into the capture only |
| Rate control | independent caps on capture and on publish |
| Multicam | per-camera enable and pause; disabling frees the render target |
| Editor | works in the editor viewport, in PIE and in packaged builds |
| Discovery | live sender list, active sender, per-stream statistics |
| Media IO | `USpoutMediaOutput` for nDisplay and Movie Render Queue |

## Project settings

**Project Settings → Plugins → Spout**

| Setting | What it does |
|---|---|
| Default Sender Name | starting name for new senders |
| Default Sender / Receiver Colour Space | defaults for new components |
| Default Sender Alpha Mode | default alpha convention |
| Max Senders | size of Spout's machine-wide sender table |
| Sender Discovery Interval | how often the sender list is refreshed |
| Verbose Spout Logging | mirrors the SDK's own log to `SpoutUE.log` |

## Colour, and why your feed looks washed out

This is the single most common problem with Spout in Unreal, so it is worth
being explicit.

Unreal render targets hold **linear** values. Almost every Spout receiver expects
**encoded (sRGB)** values. Send linear pixels to OBS and the image looks flat and
milky; send encoded pixels into a linear compositing chain and it looks crushed.

- **Sender → Colour Space → Match viewport** encodes on the way out. This is the
  default, and it makes the shared frame look like the Unreal viewport.
- **Sender → Raw** passes values through untouched. Use it when the receiver is
  a compositor working in linear and doing its own management.
- **Receiver → To linear** decodes on the way in, so an incoming feed composites
  correctly against an Unreal scene. Also the default.

## Alpha

Capturing scene transparency needs `r.PostProcessing.PropagateAlpha` enabled
(*Project Settings → Rendering → Support alpha channel in post processing*).
Without it Unreal writes an opaque alpha and your key is a solid rectangle. The
capture component warns once when you ask for alpha without it.

Then pick the convention your receiver wants: most compositors want **straight**
alpha, most real-time video mixers want **premultiplied**.

## Architecture

Spout shares textures through the D3D11 sharing model: a sender publishes a DXGI
shared handle in shared memory and receivers open it with
`ID3D11Device::OpenSharedResource`. Those are legacy, non-NT handles, which D3D12
cannot open at all.

So the plugin layers a single **D3D11On12** device on top of Unreal's own D3D12
device *and its graphics queue*. Because the queue is shared, every copy the
D3D11 side submits is naturally ordered against Unreal's rendering; no fence of
our own is needed. What does need care is that Unreal has actually submitted the
work that produced the frame, which is why the streams call `SubmitCommandsHint`
before acquiring a wrapped resource.

```
UTextureRenderTarget2D
        │  conversion pass (gamma / alpha / flip)
        ▼
  owned intermediate texture (D3D12, typed format)
        │  D3D11On12 wrapped resource, created once
        ▼
  Spout shared texture ──► every other application on the machine
```

Two details the obvious implementation gets wrong, and that this one handles:

- **Typeless formats.** Unreal registers `PF_B8G8R8A8` as
  `DXGI_FORMAT_B8G8R8A8_TYPELESS` so it can create both UNORM and UNORM_SRGB
  views over one resource. A sender advertising a typeless format is unusable —
  receivers have no way to interpret the bits — so the plugin publishes an
  explicit typed format instead of the one read off the texture.
- **Frame ordering.** The conversion pass can still be sitting in an open command
  list when the D3D11 side copies, which shows receivers the previous frame.

The vendored SDK sources under `Source/ThirdParty/SpoutSDK/Spout` are unmodified
upstream files, so they can be refreshed from Spout2 without re-applying a patch;
the one behaviour change lives in `FSpoutSenderDX`, a subclass beside them.

## Module layout

| Module | Type | Contents |
|---|---|---|
| `SpoutSDK` | Runtime | vendored Spout 2 SDK plus `FSpoutSenderDX` |
| `SpoutUE` | Runtime | bridge, streams, components, actors, subsystem, settings |
| `SpoutUEMedia` | Runtime | `USpoutMediaOutput` / `USpoutMediaCapture` |
| `SpoutUEEditor` | Editor | sender picker, live status |

## Known limitations

- **Single GPU only.** Spout's sharing model cannot cross adapters. On a laptop
  with switchable graphics, make sure Unreal and the receiving application are
  both on the discrete GPU.
- **D3D12 only.** D3D11 projects are not supported; the plugin says so rather
  than failing quietly.
- **The viewport source needs a game viewport.** Use a render target or a capture
  component to output from the editor viewport.
- **No audio.** Spout is video only; that is the protocol, not the plugin.
- **Receiving does not go through `UMediaPlayer`.** Received frames land in a
  render target, which is what materials and Composure want anyway.
- **The capture component uses `SceneCapture`.** It gives you post processing,
  depth of field and ray tracing, but not temporal upscaling — TSR and DLSS only
  exist in the main view. Use the viewport source when you need those.

## Troubleshooting

**Nothing appears in OBS / Resolume.**
Check the log for `LogSpout`. If it says the RHI is wrong, launch with `-d3d12`.
Otherwise confirm the sender is listed: `Get Spout Senders` in Blueprint, or
Spout's own SpoutSettings utility.

**The image is washed out or too dark.** See
[Colour](#colour-and-why-your-feed-looks-washed-out).

**The alpha channel is solid.** Enable
`r.PostProcessing.PropagateAlpha`.

**The feed is one frame behind.** Cap `Max Frame Rate` on the sender to the rate
the receiver actually consumes; publishing faster than that gains nothing.

**The receiver keeps reconnecting.** `Get Stats → Reconnect Count` climbing means
the sender is restarting — usually another Unreal instance fighting for the same
sender name. Give them distinct names.

**Turn on `Verbose Spout Logging`** in project settings; the SDK's own log goes
to `SpoutUE.log` and says precisely which mutex or handle failed.

## Before your first build

The plugin has not been compiled against a real engine installation yet. Nothing
here is speculative design — the sharing path follows the Spout SDK's own D3D12
sample — but a few call sites are the kind that move between engine versions. If
the build complains, look at these first:

1. **`ID3D12DynamicRHI`** (`Source/SpoutUE/Private/D3D12/SpoutD3D12Device.cpp`) —
   `RHIGetDevice`, `RHIGetCommandQueue` and `RHIGetResource` have been stable
   since 5.0, but they are the tightest coupling to the engine here.
2. **`UMediaCapture`** (`Source/SpoutUEMedia`) — the override signature changed
   once already, in 5.3, and is guarded. If it moved again, that is the place.
   The whole `SpoutUEMedia` module can be deleted from `SpoutUE.uplugin` without
   affecting anything else.
3. **`FWidgetRenderer::DrawWidget`** (`SpoutCaptureComponent.cpp`) — only used by
   the UMG burn-in.
4. **`ModuleRules.bUseUnity`** (`SpoutSDK.Build.cs`) — deprecated in recent UBT
   versions. If it warns, that is expected; if it errors, drop the line and the
   vendored SDK will simply build inside unity blobs.

Everything else is ordinary UObject, RHI and RDG code.

The Place Actors panel has no custom Spout category on purpose: the three actors
are placeable UCLASSes, so they already appear under All Classes and come up by
typing "Spout". A category would have meant depending on `FPlaceableItem`, whose
constructors shift between releases, and a compile error in the editor module
takes the whole editor build with it.

## Credits and licences

- Plugin code: MIT, see [LICENSE](LICENSE).
- Embedded [Spout 2 SDK](https://github.com/leadedge/Spout2) by Lynn Jarvis:
  BSD 2-Clause, see
  [Source/ThirdParty/SpoutSDK/LICENSE.txt](Source/ThirdParty/SpoutSDK/LICENSE.txt).
  Vendored from upstream `c2bcc12`.
