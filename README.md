# ThermalVision

A thermal camera post-process for Unreal Engine 5.8, written in C++ against the Render Dependency Graph as a scene view extension. No engine fork, no post-process material, no Blueprint.

**This is a learning project.** I am a graphics engineer with a Vulkan and PBR background, and I built this to learn how Unreal's renderer is actually put together: RDG pass construction, scene view extensions, global shaders and the parameter struct system, scene textures, and where a custom pass can legally hook into post processing. The effect itself is the excuse. Wherever there was a choice between the quick route and the one that taught me more about the engine, I took the second, and the reasoning is written down below.

---

## What it looks like

| Visible | Thermal |
| --- | --- |
| ![Visible](docs/images/comparison_visible.png) | ![Thermal](docs/images/comparison_thermal.png) |

Same frame, same camera, one console variable apart. The two figures, the cabinet and the cable runs along the right wall carry explicit temperatures in the custom stencil buffer — they read flat and hard-edged because a tag is a number, not a measurement. Everything else comes from the luminance heuristic: the ceiling lights and lamps clip to white, and the walls and floor get their whole tonal range from how lit they are. The cabinet is the interesting one: it is brightly lit, so the heuristic alone would have made it as hot as the soldier standing next to it, and tagging it as warm electronics is what puts it back where it belongs.

<!-- TODO: RenderDoc capture showing the four passes under the ThermalVision scope. -->

![Render passes](docs/images/renderdoc_passes.png)

The scene in the images is a subway maintenance tunnel from Fab and is **not** included in this repository. Only the plugin and a minimal test map are.

---

## What it does

Per frame, the plugin builds a temperature field in degrees Celsius for the whole screen, blurs it the way a lens would, and only then maps it to colour. Temperature is the quantity that travels through the passes; colour happens once, at the end.

Where the temperature of a pixel comes from, in priority order:

1. **Tagged objects.** An actor with *Render CustomDepth Pass* enabled carries its temperature in `CustomDepthStencilValue`. The stencil value *is* the temperature in Celsius. A depth comparison against the scene depth in the same texel means a tagged object hidden behind a wall stays hidden — the effect is a camera, not an X-ray.
2. **The sky.** Pixels with nothing rendered in them get a configurable sky temperature, cold by default.
3. **Everything else.** Untagged geometry gets an ambient temperature plus a term proportional to scene luminance, so lit surfaces read warmer than surfaces in shadow. It is a heuristic, not radiometry, and it is the piece that makes an untouched scene look plausible without tagging every prop.

On top of that, in the composite pass: sensor noise in Celsius (a per-frame grain and a static fixed-pattern term with a per-column component, the way a bolometer's readout offsets behave), then normalization into the configured range, a gamma bias, and an analytic ironbow palette.

### Passes

All four run under a single `RDG_EVENT_SCOPE_STAT`, so they group under `ThermalVision` in RenderDoc and in `ProfileGPU`.

| Pass | Type | What it does |
| --- | --- | --- |
| `ThermalVision.Temperature` | Compute | Reads scene colour, custom stencil, custom depth and scene depth; writes raw Celsius to an `R16F` UAV texture |
| `ThermalVision.Blur` (H) | Compute | Separable Gaussian over the temperature field, horizontal |
| `ThermalVision.Blur` (V) | Compute | The same shader, vertical |
| `ThermalVision.Composite` | Raster | Sensor noise, normalization, palette, writes the pass output |

The intermediate passes are compute writing to their own UAV textures; the final write is a raster pixel shader because `OverrideOutput` is not guaranteed to support UAV writes — the engine's own tonemap pass makes the same check. The blur passes are skipped entirely when the radius is 0: no pass is added to the graph at all, rather than a pass that does nothing.

---

## Using it

1. Project Settings → Rendering → **Custom Depth-Stencil Pass = Enabled with Stencil**.
2. On any actor you want to give an explicit temperature: enable **Render CustomDepth Pass** and set **CustomDepthStencilValue** to the temperature in degrees Celsius (integer, 1 to 255; 0 means untagged).
3. In the console: `r.ThermalVision.Enable 1`.

### Console variables

| Variable | Default | Meaning |
| --- | --- | --- |
| `r.ThermalVision.Enable` | `0` | Turns the effect on |
| `r.ThermalVision.AmbientTemperature` | `20` | Celsius for untagged geometry before the luminance term |
| `r.ThermalVision.LuminanceTemperatureGain` | `30` | Degrees added at full scene luminance |
| `r.ThermalVision.SkyTemperature` | `-20` | Celsius for pixels with no geometry |
| `r.ThermalVision.TemperatureMin` | `5` | Celsius mapped to the cold end of the palette |
| `r.ThermalVision.TemperatureMax` | `50` | Celsius mapped to the hot end |
| `r.ThermalVision.BlurRadius` | `3` | Blur radius in pixels, 0 to 8; 0 skips both blur passes |
| `r.ThermalVision.BlurSigma` | `4` | Gaussian standard deviation in pixels |
| `r.ThermalVision.NoiseTemporal` | `4` | Peak-to-peak Celsius of the per-frame grain |
| `r.ThermalVision.NoiseFixedPattern` | `4` | Peak-to-peak Celsius of the static pattern noise |

The default range of 5 to 50 °C is deliberately narrow. A wide range squashes everything interesting into a fifth of the ramp; a narrow one keeps the 15–40 band readable and lets lamps and hot cables clip to white, which is what a real camera's auto-ranging does.

---

## Decisions worth explaining

**Where it hooks.** `SubscribeToPostProcessingPass(EPostProcessingPass::Tonemap)`, from a `UEngineSubsystem`. The obvious alternative, `PrePostProcessPass_RenderThread`, takes `FPostProcessingInputs`, which lives in `Renderer/Internal` — UnrealBuildTool only exposes Internal headers to engine modules, so a plugin cannot include it without hacking private include paths into the build. `FPostProcessMaterialInputs` is in `Renderer/Public` and is the supported way in. The extension is registered from an engine subsystem rather than from the module startup because `RegisterExtension` requires a valid `GEngine`, and a world subsystem would register twice with editor and PIE running together.

**After the tonemapper, and what that costs.** Running after tonemap means the input is display-referred colour at output resolution, while scene depth and custom stencil are still at render resolution — the shader derives its sampling positions per input instead of assuming one fullscreen texture, and the effect is tested at `r.ScreenPercentage 50`. The trade-off is real: an earlier hook (`BeforeDOF`, also public) would give HDR input and TSR antialiasing on the mask, at the cost of the palette being pushed around by exposure and the tonemapper, and of the sensor grain being partly eaten by TSR. For a camera effect that is meant to *replace* the image rather than blend into it, taking the final image and mapping it was the simpler and more predictable choice.

**Temperature is a field, not a colour.** The noise is added in Celsius, before normalization, so it scales with the palette range exactly like the signal does — narrow the range and the grain grows on screen, which is what happens when you raise the gain on a real thermal camera. It goes in *after* the blur, because a sensor's noise comes after the optics; putting it before the blur would let the blur eat the high frequencies that make it read as noise at all.

**The palette gamma.** Real ironbow spends most of its low end in the darks. With a linear ramp, an ambient scene sits mid-palette and competes with the targets for attention, so the normalized value gets a 1.6 gamma before the palette. It is a constant in the shader, not a console variable: it is a property of the palette, not a tuning knob for the scene.

**Tagging beats the heuristic, and that is the point.** With a luminance gain high enough to give walls and floors a useful tonal range, any brightly lit surface reaches body temperature and a lit cabinet reads as hot as a person. The fix was not lowering the gain — it was tagging the cabinet explicitly as warm electronics. Half of a believable thermal view is deciding what temperature each prop has.

---

## Known limitations

- **Temperatures are integers from 1 to 255 °C.** The custom stencil buffer is 8 bits, and 0 means untagged. No sub-zero targets, no decimals. See *Possible improvements*.
- **Tagged silhouettes shake slightly with the camera still.** Custom depth and custom stencil are rasterized without antialiasing under TSR's jittered projection, so the edge texels of a tagged object flip between frames. Compensating the jitter on the sample position and reconstructing the mask with four bilinear taps were both tried and neither fixed it, which makes sense: the information is already binary by the time the shader reads it. The blur reduces the amplitude, and the real fix is listed below.
- **The luminance term is a heuristic.** A white wall and a warm white wall are indistinguishable to it. Anything that needs to be right has to be tagged.
- **Single view.** Not tested with split screen, scene captures or VR. With no `FSceneViewState` — scene captures, thumbnails — the frame counter stays at 0 and the temporal grain freezes.

---

## Possible improvements

- **A temperature actor component**, taking a float in Celsius and writing `clamp(round(Temperature + 50), 1, 255)` to the stencil, with the shader subtracting the offset. That buys −49 to 205 °C and moves the tagging out of raw stencil numbers into something an artist can read.
- **Temporal accumulation of the temperature field**, framed as the thermal inertia of the detector: `lerp(history, current, alpha)` with the history kept per view state and discarded on camera cuts and resolution changes. This is the honest fix for the silhouette flicker, and it is physically motivated rather than a patch.
- **Dead pixels.** A handful of texels stuck at the top or bottom of the range, picked with the same hash as the fixed-pattern noise. Two lines, and it sells the sensor.
- **An earlier hook as a compile-time option**, to compare the pre-TSR and post-tonemap versions side by side on the same scene.
- **Per-material emissivity**, so that metal and skin at the same physical temperature do not read identically — which is the single biggest thing this effect gets wrong.

---

## Performance

<!-- TODO: ProfileGPU numbers at 1080p, blur radius 0 / 4 / 8. -->

| Blur radius | `ThermalVision` total | Temperature | Blur H+V | Composite |
| --- | --- | --- | --- | --- |
| 0 | | | — | |
| 4 | | | | |
| 8 | | | | |

Measured at 1920×1080 on a <!-- TODO: GPU -->.

---

## Building

Unreal Engine 5.8 (source build). Open `Thermal.uproject`, let it compile, and the plugin comes with it — the module is `ThermalVision`, loading phase `PostConfigInit` so that its `/Plugin/ThermalVision` shader directory mapping is in place before shaders are compiled. Plugins get no automatic virtual shader directory in 5.8; it has to be registered explicitly.

The test map is `Content/Maps/Basic.umap`. The scene used for the screenshots and the video is third-party content and is not redistributed here.
