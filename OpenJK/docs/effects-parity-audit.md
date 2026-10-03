# Effects parity audit

Date: 2026-09-10. Baseline: the active Vulkan checkout after the acid-rain
implementation. Acid-rain acceptance and subsequent fixes are recorded below.
This is a source/asset audit,
not a claim that every effect was observed in the headset.

## Conclusion and scope

### Priority decision: visible campaign coverage (2026-09-29)

User explicitly defers searching for sand/space-dust campaign triggers and
similar unconfirmed-use features until AFTER an optimized ARM 1.0 release on
Steam Frame. Controlled JKA sand/dust acceptance stands; no further level/video
hunt or separate JKO fallback appearance test is required before optimization.
Keep the implementation, but do not inject weather into campaigns to create a
test case. The existing PC-first release/optimization order is unchanged.

Apply the same gate to all remaining rows: a missing source operation alone
does not make it a pre-optimization task. Require an observed defect or concrete
stock asset plus an active execution path. Use bounded offline inspection to
establish the latter; do not send the user searching through possible levels.
If that evidence is absent, defer explicitly rather than expanding the audit.
This prioritization is NOT a claim of complete OpenGL/API parity.

Deferred until after optimized ARM/Steam Frame 1.0:

- Sand/dust occurrence searches, Droid Recovery atmosphere comparisons and
  JKO fallback texture matching where no actual campaign defect is reported.
- Weather freeze diagnostics; inherited puff0/puff1 and swirlingwind omissions.
- Unused or unconfirmed material generators/deformations, alternate billboard
  topologies and general mirror/portal rendering. Existing BSP references are
  candidates, not proof of visible missing behavior. Reopen for a demonstrated
  campaign defect, not merely a declaration in a shader table.
- Dormant IK/gore paths and optional full physical ragdolls. Deliberate VR
  replacements (transition dissolves and omitted artificial outdoor head
  shake) are not required fidelity work before optimization.

Remaining bounded pre-optimization queue:

2026-09-29 update: t2_rancor arrow composition is headset accepted, including
no flicker and Yavin water/t1_sour decal regressions. Next verified material
omission: inverse-alpha blending in Hoth2's ion feed tube and t3_stamp factory
energy panels. Both animations headset accepted 2026-09-30; see
inverse-alpha-run.md. Specular alpha is now implemented separately; JKA's
view-dependent change and intact energy/casing are accepted, and JKO's protocol
droid check also passed (specular-alpha-run.md). General tcMod transform already exists and
was used by the accepted left/right footprint fix; do not reimplement it.

1. Non-rectangular JKO glass: the production face validator rejects five stock
   func_glass models, so the live destruction caller skips their shards.
   ns_streets *106/*107/*172/*173; ns_hideout *26. Polygon fallback implemented
   2026-09-29; user accepted rectangular/non-rectangular shattering after the JKO
   syscall-adapter fix. See `polygon-glass-run.md`. Glass is no longer blocking.
   This is a concrete visible gap, unlike hypothetical sandstorm placement.
2. Bounded audit completed 2026-09-30; see
   [pre-optimization findings](pre-optimization-audit.md). JKO's
   save-preview/image contracts: recent saves have empty SHOT chunks, and the
   decoder stub leaves dimensions unset on an active menu path. This does not
   establish game-state corruption. Safe callbacks, internal on-demand capture
   and autosave levelshots are now implemented; headset pending. See
   [save preview run](save-preview-run.md). User already has external VR capture;
   standalone OpenJK screenshot commands are deferred, not a required feature.
3. Restore secondary vertex lighting: 1,728 JKA and 127 JKO BSP surfaces contain
   nonzero secondary colors currently discarded. Keep diffuse palette and
   ordinary lightmap rendering unchanged. All 7,627 secondary-lightmapped
   stock surfaces fit existing translated UVs; no UV redesign is indicated.
4. Restore local fog for surface vegetation: 3,183 parent surfaces in JKO's
   yavin_swamp carry local water-fog assignments. Match legacy fog eligibility,
   not blanket attenuation of every additive particle. Other independent FX
   remain unconfirmed and deferred unless an actual omitted receiver is found.

No stock definitions were found for vector UVs, inverse vertex/entity RGB,
inverse vertex alpha or alpha noise. Entity-UV definitions exist, but no
nonzero brush input was established. These remain deferred along with the
already deferred general mirrors/portals. Do not expand the queue from counts
of shader declarations alone.

Use short targeted checks, preserve accepted rendering and controls, and then
move to measured optimization; no exhaustive dormant-command parity milestone.
Before optimization, request the user's promised ARM-oriented measurement
details. Preserve the Artus red-pulse cargo-room benchmark requirement.

2026-09-30 user follow-up queue, before ARM optimization (not bundled into the
JKO specular validation build):
- Profile missile explosions and apply localized, measured optimizations.
- Find remaining white subtitle paths that render too high; move them into
  the accepted comfortable reading region in both games.
- Make security-camera control/help text disappear after four seconds.
  Specify the timer reset on camera entry/switch when implementing; do not
  remove the controls or change camera behavior merely to hide the text.

Reminder created 2026-09-29: `review-deferred-effects-after-arm-1-0` checks weekly
in this task for explicit confirmation of BOTH optimized ARM v1.0 and successful
Steam Frame gameplay. Only then remind Patola to revisit the deferred list;
do not reopen the list merely because a calendar date or PC release was reached.
Pause the reminder after delivery.

2026-09-26: JKO temple-vine transparency/stability and JKA Rift wall seams
are headset accepted. Local BSP fog volumes are now implemented for world,
inline-brush and MD3/Ghoul2 geometry. On 2026-09-27 the user accepted JKO Artus
sludge appearance, stereo stability and the small pool after the material-noise
follow-up. JKA local/global coexistence still needs its separate live check.
See `local-fog-run.md` for source contracts, asset bounds and remaining limits.
Global fog, weather and water material composition are not rewritten.
Before subsequent optimization, request Patola's promised ARM-oriented
measurement details and retain the Artus red-pulse room as a performance case.

2026-09-21 Artus follow-up: secondary BSP lightmap styles now have a combined
fragment path (headset accepted); additional vertex-color styles are confirmed
missing. Non-constant secondary UV mappings were not found in the bounded stock
scan, so a general representation change is deferred. Active-camera base visibility
and coplanar implicit-material pass grouping also have focused fixes. See
`artus-lighting-camera-run.md`; do not mark these accepted from build tests alone.

Full functional parity is **not yet achieved**, but exhaustive API parity is
not an optimization/release blocker under the 2026-09-29 scope decision.
Accepted campaign scenes give
useful regression coverage but do not cover the whole renderer contract.
Several missing exports have live gameplay callers, not just cosmetic impact.
Do not close the parity milestone based only on successful Yavin/Hoth runs.

Sources checked:

- `code/rd-vulkan/tr_init.cpp`: renderer export assignments, including stubs.
- `code/rd-vulkan/vk_backend.cpp`: weather dispatcher, shader parser, primitive
  submission and world/model passes.
- `code/rd-vanilla/tr_WorldEffects.cpp` and `code/rd-gles/tr_WorldEffects.cpp`:
  both expose the same 19 weather command names; implementation details differ.
- Legacy `tr_shader.cpp`, `tr_surfacesprites.cpp`, `tr_surface.cpp`, `tr_cmds.cpp`.
- Both cgames and games, client syscall dispatch and server weather forwarding.
- Installed JKA/JKO `GameData/base/assets*.pk3`: read BSP entity/shader lumps
  with ZIP and little-endian BSP readers; inspect shader definitions. Later
  archive filenames override earlier copies of the same file for this audit.
  Mod archives, loose-file overrides, multiplayer execution, dynamic registrations
  and script control flow are not covered by that asset-reference scan.

Status terms: **missing** means a confirmed absent operation; **partial** means
an implementation exists but does not preserve the full source contract;
**replacement** means a deliberate different implementation; **unverified**
requires execution or further call-path investigation. A BSP shader-table
reference is a candidate test location, not proof the surface is visible on a
particular route. Original maps/models/textures are not copied into this repo.

## Atmospheric effects

Headset update: the user confirmed the wind/layer build's weather on JKA
`t1_rail`, `hoth2` and `vjun1`. `t2_wedge` mist was too subtle to confirm.
This accepts the observed weather scenes, not every command or JKO coverage.
Weather-linked saber fizz and temporary storm fog-color flashes are now
implemented (2026-09-14) and user-accepted in the focused JKA run. Rain,
shelter protection, saber combat, stereo comfort and performance remain intact.

| Command or contract | Vulkan status | Consequence / next check |
| --- | --- | --- |
| Local BSP fog brushes | Geometry path implemented; JKO Artus accepted 2026-09-27 | Authored world assignments, boundary clipping and model-volume selection. JKA local volumes still need visual acceptance. Independent FX/vegetation attenuation remains outside this batch; see local-fog-run.md. |
| `lightrain`, `rain`, `acidrain`, `heavyrain` | Implemented; wind update awaiting headset check | User accepts acid rain on 2026-09-11. The 2026-09-13 layer update adds persistent wind-driven particles and velocity-aligned streaks while retaining accepted fall-speed/color presets. |
| `snow` | Implemented; wind update awaiting headset check | Previously accepted Hoth appearance; new per-particle exposure removes the indoor-camera veto, with shared wind and world-anchored wrapping. |
| `spacedust` | Implemented; controlled JKA test accepted 2026-09-29 | Counted zero-gravity additive particles, 3000-unit volume, shared wind/shelter/stereo path. No campaign weather-entity example confirmed; JKO fallback visuals untested. See weather-dust-run.md. |
| `sand` | Implemented; controlled JKA test accepted 2026-09-29 | Alpha-blended amber smoke clouds, shared-eye sorting and near fade. No campaign trigger confirmed; t1_surprise has a distinct static dark_dust patch material. JKO fallback visuals untested. |
| `fog`, `light_fog`, `heavyrainfog` | Implemented 2026-09-13; headset check pending | Independent moving mist layers use stock presets; distinct from BSP/global distance fog. VR near/wrap fading replaces parts of the legacy spawn/fade behavior. |
| `wind` and `windzone` | Implemented 2026-09-13 | Deterministic changing global wind and strictly bounded additive local wind. Frame-independent reference timing. |
| `constantwind` | Implemented 2026-09-13 | Preserves raw strength, defaults missing vector to (0,800,0), supports multiple additive zones. |
| `gustingwind` | Implemented 2026-09-13 | Changing targets, calm periods and slew limits replace the sinusoidal drift shortcut. |
| `GetWindVector`, `GetWindGusting` | Implemented 2026-09-13 | Normalized combined direction and magnitude >1000 threshold; existing gameplay outdoor/bracing/acceleration callers now receive real state. |
| `outsideshake` / `IsShaking` | Missing | Command ignored/query false. Any restoration needs a VR comfort decision, not compulsory artificial head motion. |
| `outsidepain` / outdoor exposure | Newly implemented | Exact contents query gates existing game damage; level reset clears state. Test shelter, cinematic/gameplay transition and save/load. |
| `zone`, `clear` | Implemented with different storage | Zones are recorded; particle shelter uses BSP contents. `clear` preserves the pain toggle like legacy; level reset clears it. |
| `freeze` | Missing | Diagnostic weather simulation freeze ignored. |
| Multiple weather clouds | Implemented 2026-09-13; headset check pending | Up to five independently simulated and drawn layers, bounded counts, shared-eye stream reuse. Test rain plus mist together on `t1_rail`. |
| Thunder/lightning storm | Implemented; JKA headset accepted | Existing `fx_rain_think` still owns lightning/thunder scheduling. `SetTempGlobalFogColor` now overrides global fog color, normalizes authored byte RGB, and restores the unmodified base on zero. Fog density, mist particles, lightmaps and camera pose are unchanged. |
| Weather-related saber/acid fizz | Implemented; JKA headset accepted | `GetChanceOfSaberFizz` now averages rain/snow gravity / 20000, excluding mist. Existing JKA gameplay exposure, FX and acid-contact debounce callers consume it. JKO retains its independent rain-flag/length-based fizz path. |
| Weather-sensitive surface effects | Partial | `ssFXWeather` is parsed into `weatherAffected` but never consumed. Legacy uses weather amount and `r_surfaceWeather` to control their density; weather wind also influences surface-sprite wind. Current grass animation does not prove this contract. |
| Authored/scaled precipitation count | Implemented 2026-09-13 | Parses `snow init N`, `rain init N`, and direct counts; bounds at 4000 per layer. JKO's old rain count is honored even though the shared legacy handler ignored it. |

Verified shipped single-player weather entities:

The wind/layer rows have been rechecked against installed BSP entity data.
See [wind/layer run instructions](weather-wind-run.md) for implementation
boundaries, tests and the next live checks. Wind-driven surface sprites,
storm flashes and fizz are still separate omissions.

| Game / level | Authored request | Focused future check |
| --- | --- | --- |
| JKA `vjun1` | `fx_rain` flags 8: acid rain | Opening live cinematic, damage outdoors, shelter protection, saber fizz. |
| JKA `yavin2` | `fx_rain` flags 1: light rain | Outdoor drizzle, shelter transitions. |
| JKA `t1_rail` | `fx_rain` flags 20, constant wind speed 5000 | Heavy rain + mist + automatic lightning/thunder + outdoor shake request; preserve the accepted train performance. |
| JKA `t2_wedge` | `fx_wind` flags 70, speed 300 | Constant/gusting wind and `light_fog`; not just static distance fog. |
| JKA `hoth2` | Snow plus wind flags 6, speed 3000 | Snow, wind strength, bracing and sheltered views. |
| JKO `yavin_swamp` | `fx_rain`, default count 500 | Ordinary rain and the old command argument form. |

Vjun2/3 have no equivalent rain entity in the inspected shipped BSPs. Do not
enable acid rain merely because a name starts with `vjun`. Scripts or mods can
still issue commands; this scan is not a proof of every runtime weather state.

`swirlingwind` and `puff0`/`puff1` deserve a separate **inherited omission** label:
JKA game code can emit them, but neither checked GL/GLES weather dispatcher
implements them either. `R_IsPuffing` already returns false there. They must not
be presented as confirmed Vulkan regressions. Similarly, no distinct generic
rain-splash implementation was established by this audit; follow the actual
surface-FX/fizz paths before promising an invented legacy feature.

## Other confirmed gaps and risks

### Output contracts: fix first

- **Lighting queries (implemented 2026-09-11, headset check pending):** formerly
  `re.GetLighting` returned false without writing any
  vectors. Both `CG_GetPlayerLightLevel` implementations read the unwritten
  `directed` vector, and `G_GetLightLevel` also consumes `lightDir`. This is an
  uninitialized-data risk affecting game light levels/sight alerts, even while
  Vulkan's internal world/model lighting looks correct. Reuse the lightgrid
  sampler and establish finite, initialized outputs for every call path.
  The new gameplay-only helper reads the existing grid and live styles, with
  initialized fallback outputs. Visual model/world lighting is unchanged.
- **Breaking glass (implemented for quad panes 2026-09-11):** formerly
  `re.GetBModelVerts` wrote nothing. Both games'
  `funcGlassDie` pass uninitialized `verts[4]` and `normal` to `CG_DoGlass`.
  Restore valid brush-face geometry and a defined failure path; merely zeroing
  the output is not functional parity. Candidate glass maps: JKA `t2_dpred`
  (56 `func_glass` entities), `t1_rail` (10), `vjun3` (3); JKO `kejim_post`
  (26), `ns_streets` (52), `cairn_dock1` (28). Counts are entities, not necessarily
  individually accessible breakable windows.
  The export now provides cached broad-face geometry and its normal; callers
  reject invalid seeds without preventing the brush from breaking. Production
  helper asset checks support 80/80 JKA and 287/292 JKO single-player panes.
  Remaining non-quad JKO faces: `ns_streets` models *106/*107/*172/*173 and
  `ns_hideout` *26. They need polygonal shard support, not a fabricated quad.
  See the [contract ledger](vulkan-vr-parity.md#lighting-and-glass-output-contracts-2026-09-11).
- **Refraction export safety:** distortion-property getters return null while
  `CG_R_SETREFRACTIONPROP` dereferences them. No live stock caller was established:
  the cloaking call is commented out, and Force Push explicitly returns through
  the classic blur path before its refractive code. It is a latent API hazard,
  not evidence that ordinary Force Push currently crashes.

### Visual/gameplay effects

- **Projected marks (implemented; JKA/JKO blaster marks and fading, plus JKA
  Hoth footprints accepted 2026-09-15):** `re.MarkFragments`
  now clips against a CPU position/index cache of static BSP geometry using
  existing BSP leaves for candidate selection. Preserves material exclusions,
  angle thresholds, projection planes and bounded caller output; game code
  retains ownership of textures, lifetime/fading and `cg_marks`/footstep settings.
  Dynamic-effect draws now honor authored `polygonOffset`. Test scorch marks
  across walls/edges and footprints on suitable ground. This is not animated
  model gore or moving-brush mark attachment. See `projected-marks-run.md`.
- **Light-amplification goggles (JKA/JKO accepted 2026-09-16):**
  `re.LAGoggles` now snapshots a scene-local fullbright/amber-fog override in both
  variants. Existing game-side zoom-mode/battery gate remains authoritative.
  Baked RGB is bypassed without removing material alpha; entity lighting uses
  the goggles flag while menu models remain unaffected. Mask uses the existing
  binocular head-locked UV mapping. See `goggles-scoped-fog-run.md`.
- **Scoped fog and optical zoom (JKA/JKO device tests accepted 2026-09-16):**
  `SetRangedFog` snapshots JKA's FOV*64 request, retaining map `distanceCull` and
  `linFogStart` for legacy linear start/end calculation. Zero restores the
  authored baseline; frame/level boundaries clear transient requests. JKO has
  no corresponding game-side scoped-fog call, which is intentionally unchanged.
  Ordinary non-ranged fog keeps the accepted Vulkan curve; this is not a global
  EXP2-fog rewrite. Test Hoth Tenloss zoom and fog restoration after release/load.
  JKA binocular fog changes were observed, exposing a separate missing optical
  projection override. Binoculars now share the Tenloss tangent-scale calculation
  without its reticle/aim mapping; user confirmed magnification, goggles,
  Tenloss/E-11 scopes and battery depletion in both games.
- **Electricity and hand aiming (JKA/JKO Lightning accepted 2026-09-16):**
  Vulkan now consumes `RF_FORKED` with the legacy three-fork shared budget,
  early-strand attachment, randomized midpoint targets and inherited taper.
  Uses a local deterministic seed for matching eyes/color/glow, without changing
  gameplay damage or the accepted main-strand shape. This is bounded topology
  parity, not an exact reproduction of legacy random micro-subdivision.
  Authored `usePhysics` means FX_BRANCH for Electricity primitives, including
  Force Lightning in both games. User extensively tested casting-hand direction
  with sabers/firearms, gaze aligned/diverted and head held still; passed in both
  games. Branch generation was also confirmed in the earlier log. See
  `electricity-run.md`.
- **Shader-driven geometry (batch 1, 2026-09-21; JKA/JKO observed scenes accepted):** authored
  wave/bulge/move now use one GPU evaluator across color, fog, dynamic light,
  shadow and depth passes. Ordered stages, legacy lookup quantization, separate
  shader/scene clocks and expanded culling bounds are retained. Unit, GPU and
  source-integration tests pass; see `material-deform-run.md` for limits and
  candidate tests. User confirms waving cobwebs, undulating water, electrical
  body effects and no observed regressions in JKA; the log confirms those
  material submissions and normal shutdown. Actual BSP surfaces confirm Rift cobwebs (`t3_rift`), water
  (`t2_rancor`, `vjun2`) and JKO Artus lava/slime. User accepts corrected JKO
  lava/slime deformation/color and Valley of the Jedi; JKO Force Lightning was
  subsequently tested and accepted with the Artus lighting/camera fixes.
- **Static material quads (batch 2, 2026-09-22; JKA kor1 accepted, JKO pending):** camera-facing
  autosprite and fixed-axis autosprite2 use shared-eye cached affine transforms.
  Korriban chains are autosprite2; JKO blinking-light patches use autosprite.
  Preserve chain endpoints and UVs, including lightmap UVs. See
  `material-autosprite-run.md` for topology restrictions and test candidates.
  Zero-vertex static BSP flares now have separate generation (2026-09-24,
  JKA and JKO headset accepted), with authored sizing/materials and shared-eye FX batching.
  Vulkan pixel depth intentionally replaces legacy synchronous center-depth
  readback; see `bsp-flares-run.md`. Mixed/inline/model billboard geometry, normal deformation,
  projection shadows and text deformation remain explicit follow-ups; ordinary
  FX billboards use their existing separate path.
- **Shader alpha/UV semantics:** `alphaGen wave` is now implemented alongside
  const (2026-09-16, headset pending): standard periodic waves, replacement of
  generated alpha without altering RGB, consistent color/fog/UI coverage.
  Alpha-wave FX preserve per-entity shaderTime and select animated frames in
  both color/glow paths, restoring paired-frame explosion cross-fades. This
  timing/batching change is limited to alpha-wave materials. See
  `alpha-wave-run.md`. 2026-09-27: RGB and tcMod stretch noise now have a CPU
  evaluator after the Artus comparison exposed ignored authored brightness;
  JKO sludge headset check accepted. The private portable noise table preserves the legacy
  interpolation/range, not libc-specific sample values. Alpha/deformation noise
  and general 2D RGB waves remain separate audit items. See local-fog-run.md.
  Specular alpha is now implemented and headset accepted in both games;
  portal alpha remains deferred. Entity/vertex
  alpha can already flow through other paths, so do not label every transparent
  material broken. `tcMod transform` is implemented (including mirrored
  footprints); broader compound-operation ordering is not fully audited.
  `entityTranslate`, `tcGen vector` and
  inverse color generators also lack general implementations. Examples in BSP
  shader tables: JKA conveyors in `t3_stamp`/`taspir2`, Hoth specular feed tubes;
  JKO Cairn door transforms. A material declaration alone does not prove visible
  failure, especially with local asset overrides or redundant stages.
- **General mirrors/portals:** no general `RT_PORTALSURFACE` rendering path was
  found. The working authored sky-portal composition and security-camera view
  are different features. Candidate material references: JKO
  `textures/tests/mirror_floor` in `cairn_dock1` and `textures/system/portal_yavin`
  in `yavin_final`; require in-level inspection before defining acceptance.
- **Skeletal extras:** physical ragdolls, IK callbacks and skin-gore submission
  remain stubbed. Full ragdolls were explicitly deferred. Accepted animated
  deaths/dismemberment are not a physical-ragdoll or gore-texturing implementation.
  Determine enabled build flags and live callers before expanding this work.

### Not missing in the same sense

- Generic effect sprites, oriented quads, lines/beams, cylinders, polygons,
  electricity, dynamic lights, cinematic video textures, fogged world/models,
  sky composition, surface vegetation, disintegration, shadows, bloom and Force
  Speed blur have Vulkan implementations. Their existence does not certify
  every material/flag, but they must not be counted as wholly absent systems.
- `InitDissolve`/`ProcessDissolve` are stubs: original transition dissolves are
  absent, but seamless retained/black VR transition frames deliberately replace
  the compositor flash. This is visual-style parity, not the old loading bug.
- GL stereo-replay hooks are intentionally unused by native Vulkan stereo
  submission. They are not a missing effect.
- Internal save capture/temporary-image and JKO JPEG-buffer exports now have
  implementations (2026-09-30; headset pending). Standalone OpenJK screenshot
  commands are deferred because the user's external VR capture already works.
  Save thumbnails remain separate from game-state serialization; do not infer
  that accepted manual saves are corrupt. See save-preview-run.md.
- Empty registration, PVS, scissor and other model-service hooks need a broader
  API contract audit; internal Vulkan replacements may make some unnecessary.
  An empty callback alone is not sufficient to classify all of them as defects.

## Completion plan and regression gates

Historical technical inventory below is subordinate to the 2026-09-29 priority
decision above. In particular, atmospheric-family completeness and latent
uncalled API implementations are no longer required before optimization.

1. Acid-rain test accepted 2026-09-11; retain it as a regression gate.
2. Repair the live unwritten-output contracts (lighting and glass), with focused
   contract tests, then make latent pointer APIs safe. Do not mask missing
   behavior with silent success stubs.
3. Complete the atmospheric command/query family as a coherent subsystem:
   multiple simultaneous clouds, wind vector/speed/regions, fog particles,
   storm fog flashes and fizz; maintain exact shelter/damage queries. Decide
   outdoor shake as an explicit VR comfort option. Preserve train/Wedge timing.
4. Add rate-limited diagnostics for unsupported weather commands, shader
   directives and effect flags, recording map/material/owner. Maintain a checked
   support inventory so silently ignored requests cannot masquerade as parity.
5. Restore projected marks, goggles and remaining material/effect semantics in
   separate small batches. Use minimal test materials and reference math/image
   tests; do not change global winding, sorting, blend defaults or palette to
   repair an isolated directive. Extend the matrix when a new use is found.
6. Per batch: build/test both variants; verify deployment hashes; headset-check
   near/far views, stereo fusion, shelter/occlusion, save/load and level changes.
   Measure CPU/GPU cost in crowded Yavin, `t1_rail`, `t2_wedge` and Hoth. Retest
   accepted water, foliage, shadows, scopes, holograms and spatial console.
7. Separate required functional parity, deliberate VR replacements and deferred
   enhancements in release notes. No claim of 100% effect parity until every
   required row has a working contract and recorded acceptance evidence.

This audit changes documentation only. No additional renderer, game DLL, asset
archive or user configuration was deployed during it.
