# Authored alpha waves

## Contract and boundaries

Legacy `tr_shade_calc.cpp::RB_CalcWaveAlpha` evaluates a clamped waveform and
replaces generated vertex alpha with its byte value. Vulkan now parses the
standard periodic wave types (sin, triangle, square, sawtooth, inversesawtooth)
and supplies the same alpha independently of vertex RGB. Constant zero-amplitude
waves matter too: Kejim crystal shells request 0.3 or 0.2, not full opacity.
The old continuous waveform evaluator was moved unchanged into a testable
header; it approximates the original sampled wave tables, not bitwise table
identity. Invalid/non-finite declarations are rejected with a warning.

Color, glow, UI and fog paths retain alpha-wave coverage. Unspecified alpha
keeps the previous behavior, including Yavin river/pool adjustments. This does
not implement lightingSpecular, portal alpha, noise waves, general deformation,
autosprites, mirrors, or global shader-clock support for every material.

Explosion `gfx/exp/explosion1_a` uses two adjacent one-shot image sequences
with complementary 3 Hz alpha waves. Its FX particles setShaderTime. Previously
Vulkan grouped these particles solely by shader and used the first image. For
alpha-wave materials only, batches now also key on finite shaderTime; both
image selection and alpha use scene seconds minus that start time. Other
effects keep their batching and timing. Different start times require separate
draw batches, but no additional render targets/passes/readback are introduced.

## JKA controlled test

Use the normal terminal launcher:

```bash
JKXR_JKA_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Academy/GameData" \
jkxr-jka +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jka.log
```

Load a disposable test state in a quiet open area. In the spatial console:

```text
fx play explosions/probeexplosion1
fx delay 4000
```

The existing FX command places an emitter just ahead of the player near foot
height. Step back several metres to observe several cycles for about 20 seconds.
This particular authored effect includes sound, debris, flashes and camera
shake; avoid testing it at arm's length. It is a visual-effect test, not an
explosive weapon or a change to combat damage. Do not overwrite campaign saves.

1. The orange explosion sprites should advance through their images with
   cross-fading, followed by their existing smoke/debris. No persistent opaque
   squares, frozen first image or eye disagreement should appear.
2. Walk/turn around it and check stereo stability and frame pacing. The change
   may be subtle inside a busy explosion; the log confirms which material ran.
3. Enter `fx stop`, wait several seconds for remaining particles/debris to expire,
   then continue ordinary gameplay. Spot-check saber glow, water and menus.
4. Exit normally and preserve the log. `rd-vulkan-alpha-wave` reports parsed
   material/stage parameters; `rd-vulkan-alpha-fx` samples actual FX shader,
   elapsed age, evaluated alpha and selected image handle every two seconds.

JKA's `effects/explosions/probeexplosion1.efx` was verified in the installed
assets, so this is more reliable than guessing a particular enemy or location.

## JKO separately

Use the ordinary JKO launcher and `/tmp/jko.log`. The equivalent effect is named
without the directory prefix:

```text
fx play probeexplosion1
fx delay 4000
```

Repeat the test, then `fx stop`. A useful additional story case is Morgan's
spirit in `valley`: his authored alpha varies slowly around 0.7 (0.6-0.8), with
a 10-second period, but the user sees stable opacity in both Quest and Vulkan.
Do not require an obvious pulse or increase its authored amplitude. His
separate blue glow remains. The installed valley BSP contains
Morgan's cinematic references, and JKO players.shader supplies his alpha wave.
No requirement to replay that entire cinematic if a suitable save is unavailable.

Kejim crystal materials are another affected asset family, but not every
crystal in the games uses those shaders. Rift crystals are not a substitute
acceptance case. RGB noise on crystal glow remains a separate omission.

## September 20 follow-up

User reports good explosion appearance in both games, including nearby lighting
and particles, but cannot isolate every cross-fade inside the busy effect. Morgan
has excess internal chest/wrist triangles compared to Quest. `/tmp/jko2.log`
confirms the six Morgan alpha-wave shaders and a clean Ghoul2 surface audit.

Morgan's base stages request alpha blending AND depthWrite, followed by additive
blue glow, with default front-sided culling. Vulkan honored depthWrite but used
two-sided pipelines for these GLMs. A narrow material policy now retains authored
culling on depth-writing alpha-wave GLM shells and their additive/fog stages.
Ordinary opaque GLMs, world geometry, two-sided shaders and existing late MD3
policy are unchanged. Morgan's 2,671 LOD0 bind-pose triangles all have clockwise
winding relative to their authored normals, consistent with the retained legacy
front-face convention. That first correction added no depth-only prepass.

Retest Morgan's chest, wrists and face during the Valley scene; check that his
exterior stays complete from both sides and ordinary characters remain intact.
`rd-vulkan-depth-alpha` identifies the scoped material path. Live acceptance of
the first correction: chest back faces are gone, but shoulder joints and wrists
remain visible through the sleeves.

Separately, JKA dual-saber Heal can reportedly become unavailable after Push until
blades are switched off. The available `/tmp/jka.log` and `/tmp/jka2.log` predate
this run (September 16), so they are not proof of its cause. Input now releases
the motion-attack command when eligibility ends, including inactive blades/UI,
instead of leaving a branch-local latch behind. Gesture/selected-power binding
and game eligibility rules are unchanged. Added debug records:

- `jkxr-force-state`: trigger edge, chord ownership, eligibility, selector state.
- `jkxr-force-heal`: started or rejected (health, unusable, pain/attack, saber lock),
  plus active powers, timers, blade state and authored restrictions.

For JKA launch with `+set vr_controller_debug 1` and log to
`/tmp/jka-force-heal.log`. With less than full health and sufficient Force, select
Heal, perform grip+trigger Push, release BOTH controls, then try trigger alone
without extinguishing blades. Repeat with the two release orders, then test held
Lightning and normal dual-saber swings. If Heal fails, keep still several seconds,
release/repress trigger, then switch blades off and repeat. Exit normally. This
distinguishes input ownership from legitimate cooldown/energy/health restrictions;
it does not assume the reported lockout is already solved.

## Shell overlap follow-up

User now accepts the JKA input correction. Leave controls unchanged. Morgan's
remaining artifacts concern front-facing surfaces behind other front-facing
surfaces, which back-face culling cannot remove. Per-surface alpha blending can
leave wrist color/glow in the framebuffer before the closer sleeve writes depth.
Legacy batches surfaces by shader; reproducing its static ordering alone would
not guarantee a nearest-shell result for different viewing directions.

For the existing depth-writing alpha-wave GLM case only, collect the visible
surfaces' already-skinned buffer ranges, draw stage zero with color writes
disabled for ALL of them, then draw their normal color/glow stages. Both passes
reuse identical geometry, alpha-test coverage, depth bias and per-eye projection.
This is an intentional order-independent self-occlusion correction, not a claim
that the legacy renderer used a prepass. The authored alpha remains unchanged,
so scenery is visible through the exterior while covered joints are hidden.

This adds one depth-only draw per participating surface (Morgan logged 16 visible
surfaces), per rendered view, without reskinning or an extra render target.
Ordinary opaque models, MD3s, world/water, disintegration and explicitly two-sided
materials do not use it. Pipeline color writes are opt-out only for this pass.
`rd-vulkan-alpha-shell` confirms the model and participating surface count.
Numeric tests exercise wrist/sleeve overlap in both orders; source guards verify
depth-before-color ordering and reuse of the same skinned buffers. Headset
acceptance remains pending: inspect shoulders, sleeves/wrists, chest and face
throughout Valley, including changing viewpoints and both eyes. Check that the
spirit retains its translucent blue appearance and normal characters are intact.

Live result (2026-09-21): user confirms Morgan now renders correctly. The shell
overlap follow-up passes; no additional transparency or input changes are needed.
