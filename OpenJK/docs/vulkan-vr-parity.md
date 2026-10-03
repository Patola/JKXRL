# Vulkan and VR parity ledger

This document records behavioral contracts discovered while replacing the legacy
OpenGL/SDL2 path. A feature is not considered complete merely because it appears
once: its source contract, Vulkan/VR implementation, diagnostic evidence, and
repeatable acceptance test must agree.

## Checkpoints

### JKO save previews (2026-09-30; headset pending)

- Restore on-demand left-eye capture for manual saves and authored autosave
  levelshots. Explicit request/render/read cycle, image copied only while
  acquired; no continuous readback. Retain stereo rendering and external VR
  capture. Standalone OpenJK screenshot commands deferred by user scope.
- Bounded JPEG callbacks reject malformed/empty previews safely; initialize
  dimensions/pointers, preserve SHLN/SHOT layout and legacy row order.
  Separate save RGB/loading RGBA; use explicit UI validity, not pixel color.
- API 24 requires paired engine/renderer deployment. Both games rebuilt;
  JKA retains authored level previews. See [validation run](save-preview-run.md).

### Bounded pre-optimization audit (2026-09-30)

- Specular accepted in both games; JKO protocol droid was the clear live example.
- Read-only stock scan: 34 JKA and 26 JKO non-MP BSPs. All 7,627 secondary
  lightmap surfaces fit the existing translated-UV representation.
- Concrete queue: JKO save-preview/image output contracts, nonzero secondary
  vertex-lighting colors, then local fog on JKO swamp surface vegetation.
  See [audit findings and regression gates](pre-optimization-audit.md).
- No runtime changes or deployment for this audit. Unconfirmed material
  generators remain deferred, not additional campaign-search requests.

### Authored specular alpha (2026-09-30; both games headset accepted)

- Restore alphaGen lightingSpecular, not a global material or light rewrite.
  Per-vertex exponent 4 and byte quantization follow RB_CalcSpecularAlpha.
  World surfaces use its legacy fixed origin; inline/MD3/Ghoul2 models use
  model-local light direction, with diffuse lighting left unchanged.
- Each eye derives its local viewpoint from its own MVP. The shared material
  UBO gains a direction field (240 bytes; still 256-byte aligned on this GPU).
  Cached direction/deformation combinations reset per frame; scopes restore
  entity state. No extra draw, framebuffer, or rendering pass.
- Only explicitly authored stages use the new alpha. Color generation,
  texture alpha, depth testing, culling and material pass order are retained.
  r_vulkanSpecularAlpha 0 is an immediate former-behavior diagnostic; 1 is default.
  Authored detail stages still honor r_detailtextures; enable it at startup
  for the Hoth check. See specular-alpha-run.md.

### Inverse-alpha energy materials (2026-09-29; headset accepted 2026-09-30)

- Bounded stock audit found actual BSP surfaces using the ignored
  ONE_MINUS_SRC_ALPHA / SRC_ALPHA pair: Hoth2 ion_feedtube and two long
  t3_stamp factory wallliner panels. Ordinary-alpha fallback reverses which
  regions show the underlying animation. Factory bomb_new_glow additionally
  uses ONE_MINUS_SRC_ALPHA on both sides of its intermediate stage.
- Restore both exact RGB/alpha blend equations through shared pipeline factors,
  material parsing, world/model/2D selection and resource lifetime. No new
  render passes, global palette changes, geometry, draw-order or cvar changes.
- GPU raster tests cover alpha 0/0.25/0.75/1, including output alpha. Native
  tests execute the real parser branch and guard unchanged existing equations.
  See inverse-alpha-run.md for precise candidate viewpoints and scope.
- Specular alpha was a separate confirmed material gap; this batch repairs
  the base energy mask, not all possible Hoth material semantics. General tcMod
  transform was already implemented for footprints; the older audit overstated
  that omission and is corrected.

### BSP alpha decals over unfinished walls (2026-09-29; headset accepted)

- User confirms the arrow now has no pale background or flicker; Yavin water
  and t1_sour tower decals remain perfect.

- User accepts console teleport following and reports an opaque pale background
  plus flickering on the t2_rancor arrow. Log position (-3407,4411,1156), yaw 131,
  identifies surfaces 403/404, rocky_ruins/arrow3 and arrow3_b, over metal2 10980.
- Both decal PNGs retain alpha. Their authored blended depthWrite stages ran
  before metal2's modulated texture pass, occluding it even at zero-alpha texels
  and exposing its bare lightmap. This is draw ordering, not missing image alpha.
- Cache BSP translucent-stage order at map load: finish solid materials, then
  layered polygon-offset decals, then remaining blended surfaces. Stable within
  groups, applied to static and inline BSP lists only when decals are present.
  No new draws or per-frame sort; no shader, geometry, culling, alpha, bias,
  shadow, fog, sky or accepted late-water path changes.
- Production-fragment Vulkan raster test reproduces the pale rectangle with
  the old order and verifies alpha/foreground occlusion with the corrected order.
  Native tests execute the real pass classifier/order function, including
  vertex promotion, solid cutouts, implicit walls, inline subsets and no-decals.
- Read-only stock audit covers all eight rocky_ruins arrow variants, exit arrows,
  Rift carved_symbol, other painted signs/scorches and JKO layered window/oil
  decals. See `decal-composition-run.md`; focused headset acceptance recorded above.

### Visible-effects scope before optimization (2026-09-29)

- User defers campaign searches for sand/space dust and similarly unconfirmed
  features until after optimized ARM 1.0 on Steam Frame. Preserve the passed
  controlled JKA test; no new sandstorm placements or fallback-only test runs.
- Prioritize reported defects or verified stock assets with active callers.
  Five non-rectangular JKO glass panes are the next concrete gap. Bound offline
  checks for other material/fog/thumbnail candidates; absent evidence, defer.
- Dormant weather diagnostics, inherited puff/swirl omissions and unconfirmed
  shader/mirror/IK/gore paths do not block optimization. Full physical ragdolls
  remain optional/deferred. Live correctness/safety issues are not waived.
- See effects-parity-audit.md for the queue and explicit post-1.0 exclusions.
  The PC-first release roadmap remains unchanged; this adds a deferred-work
  milestone, not a promise about hardware availability or an accelerated port.
- Before optimization starts, ask for the user's ARM-oriented measurement
  details; keep the Artus red-pulse cargo room in the benchmark set.

### Sand and space-dust weather (2026-09-27; JKA accepted 2026-09-29)

- User confirms all controlled JKA test stages behaved as described. JKO
  fallback textures still need separate visual acceptance. Droid Recovery
  (t1_surprise) has ten static dark_dust patches, not a confirmed sand-preset
  request; retain its natural atmosphere as a separate comparison candidate.

- Restore two ignored weather presets with bounded counts, existing shared-eye
  particle simulation, wind and outdoor exposure. Sand uses alpha blending and
  a shared-view back-to-front order; space dust uses additive blending.
- JKO lacks JKA's authored textures: use existing JKO smoke/dust fallbacks only
  when the requested asset is absent. No asset copying or campaign edits.
- Restore the legacy r_we console command for a controlled test because no
  stock campaign weather-entity trigger was confirmed. See weather-dust-run.md.
- No changes to accepted rain/snow/mist presets, local fog or water rendering.

### Local BSP fog volumes (2026-09-26; JKO Artus accepted 2026-09-27)

- User accepts the latest sludge appearance, stereo stability and small pool.
  JKA local volumes and independent FX/vegetation attenuation remain separate
  acceptance/scope items, not implicitly covered by the JKO result.

- 2026-09-27: screenshot comparison is much closer after the ordering fix;
  residual pattern-contrast difference led to a separate shader omission:
  RGB noise was ignored (slime's authored 0.5 +/-0.1 became 1.0), as was noise
  texture stretching. CPU material-noise support added with portable private
  tables, legacy interpolation/phase and stereo-stable scene time. Tests and
  remaining scope limits documented in local-fog-run.md; JKO headset accepted.

- First Artus test passes boundary/stereo stability and dry scenery. Brighter,
  flatter distant sludge exposed incorrect fog/liquid composition order.
  Fog-liquid materials with explicit seeThrough and no depth-writing stages
  now finish after terrain fog; their own fog pass uses legacy EQUAL depth.
  GPU regression includes an old-order negative control. Recheck pending.
- Logged small pools near (-2121,1280,555) have fogNum=-1 and lie outside both
  Artus volumes. Their lack of a fog-toggle difference is authored, not a
  loader omission. Details and exact surface IDs are in local-fog-run.md.

- Preserve raw surface fog assignments and load validated brush bounds/visible
  planes. World, inline-brush and MD3/Ghoul2 fog draws use local color/density
  and boundary clipping; model planes follow the actual entity transform.
- Reuse the geometry fog path without changing global fog or water materials.
  Keep indirect groups separated by fog index and goggles as an override.
- CPU boundary/selection/transform tests, production-fragment GPU tests and
  seven retail brush fixtures provide automated coverage. Live Artus/JKA
  checks and limitations are recorded in `local-fog-run.md`.

### JKO temple-vine cutout (2026-09-25; accepted)

- Follow-up: user accepts transparency but reports shimmer. Retail BSP has
  three coincident mirrored front/back patch pairs. Extend authored plane-side
  visibility only to flat, one-sided, non-deforming depth-masked patches; both
  eyes share selection. No winding/filter/alpha changes. User confirms this
  completely fixes the vines when viewed from every perspective.
- 2026-09-25: user rejects first build. Installed hash correct, but log lacks
  `rd-vulkan-cutout`: SRC_ALPHA/ZERO actually falls back to ALPHA, so the
  classifier requiring opaque never activated. Accept alpha coverage in the
  exact three-stage pattern and normalize its overwritten first pass to opaque
  before scheduling. Add executable production-classifier regression plus
  source guards; GPU coverage tests alone missed this integration failure.
- Flare acceptance uncovered opaque/flickering backgrounds on hanging vines
  near Luke's chamber. Log and stock assets identify `temple_vinesalpha`:
  coverage/depth first, replace with lightmap at equal depth, modulate at equal
  depth. The renderer parsed depthFunc but ignored it during world stage draws.
- Add two non-depth-writing EQUAL pipelines, gated to lightmapped BSP batches
  with this three-stage coverage/replace/modulate contract. No general depth,
  sorting, winding, water or texture changes. Log `rd-vulkan-cutout` at material
  registration. See `temple-vines-run.md` for evidence and acceptance route.
- Headless production-fragment raster test verifies clear holes, lit texels and
  foreground occlusion; negative control reproduces filled holes. Both renderer
  variants build. Coverage and paired-side fixes now have headset acceptance.

### t3_rift vertical seams (2026-09-26; accepted)

- User revisited both reported positions: no seams remain, and all other
  observed effects/objects remain correct. Reported defect closed. Third
  location was not separately reported in this acceptance run.

- All ten screenshots analyzed: lines persist on solid gray rock; hiding it
  reveals the sky/terrain behind. Base texture/lightmap cannot explain them.
- Previous audits missed the 0.5-unit gap at 9135/9139, the other boundary of
  the same strip as the previously repaired 0.125-unit gap at 9138/9139.
  The two edges are consistent with the reported parallel lines.
- Scoped full-height boundary joining now includes half-unit discontinuities,
  with maximum movement 0.25 units and unchanged UVs/indices/attributes.
  Production retail audit: six pairs, 45 moved copies, ASan/UBSan clean.
- Expanded two-edge GPU test reproduces the old incomplete repair: half-unit
  gap in 48/390 samples before AND after the old repair, zero after joining
  both edges. This isolated test now has accompanying headset acceptance from
  both reported viewpoints, not just numerical coverage.
- Both renderer builds and all 23 suites pass. Deployed and hash-verified.
  See rock-seams-run.md for exact evidence, rollback pair and focused retest.

Historical failed attempts:

- Endpoint joining also rejected: `/tmp/jka-seams3.log` confirms four joins and
  32 moved copies, user sees no change. Remove all production geometry repair.
  The observed small BSP gaps have not been causally tied to the headset lines.
- Add narrowly gated `r_vulkanRiftSeamDebug` modes: 0 normal, 1 gray, 2 texture,
  3 lightmap, 4 hidden rock. Log mode/material count/view origin; suppress the
  selected material's late/fog/dynamic-light stages during diagnosis. Keep
  geometry and other rendering unchanged; defaults to off, not archived.
  This is instrumentation, not another claimed fix. See `rock-seams-run.md`.

- Bridge attempt rejected: `/tmp/jka-seams2.log` confirms five repairs activated,
  but the user still sees seams and reports possible worsening. New viewpos is
  `(-1204 417 -267) : 102`; corrected save name is `selost3rift`.
- Remove added strips. Join only matched full-height edge positions instead,
  including duplicate vertex copies in the same static rock material. Retail
  audit joins four edges, moves 32 vertex copies by at most 0.0625 units, and
  preserves attributes/triangle counts. Partial spans are not joined.
- GPU subpixel coverage sweep reproduces the original gap and verifies joined
  edges, but also passes the rejected bridge in isolation. It therefore does
  not explain the entire in-game failure; headset acceptance remains required.
- `/tmp/jka-seams.log` locates viewpos `(692 1341 630) : 165`; user supplied
  named save `selost3rift` and reports additional similar locations. Ray/asset
  inspection identifies PLANAR rock faces 9138/9139, not curved patches. Their
  vertical edges differ by 0.125 units at y=1878, z=-5056..4928.
- Rejected first attempt added narrow bridging strips only for static `textures/rift/rock3_phong` in
  `maps/t3_rift.bsp`, with compatible normals, vertical boundary edges, gap at
  most 0.1251 units and overlapping height at least 64 units. Preserve original
  vertices, triangles, UVs and batch/surface identities. No global weld,
  tessellation, depth bias, texture or collision change.
- Production repair executed against retail BSP under ASan/UBSan: five unique
  strips (ten triangles), including this location. Duplicate edge pairs are
  deduplicated. See `rock-seams-run.md` for evidence and test procedure.

- User screenshot `/tmp/codex-clipboard-cd1b3ae2-918f-466f-9cd2-8baedfad2eb5.png`
  shows thin discontinuous vertical highlights across the cave wall. Reportedly
  the lines extend top to bottom and shimmer in headset.
- User confirms lines stay attached to the same wall locations as the head
  moves, not fixed to the view. Prioritize geometry/material boundaries, not
  view-fixed streaming artifacts. The subsequently supplied viewpos resolves
  the exact faces as described above.
- Read-only stock-map audit: 220 exactly shared quadratic patch-edge spans;
  none had different subdivision counts under the current algorithm. This
  does not exclude partial-edge T junctions, floating-point cracks, material
  artifacts. A second audit found no sub-unit mismatches among corresponding
  quadratic controls grouped by nearest whole-unit coordinates. No speculative
  shared geometry change; need location/save before treating it as a confirmed
  tessellation defect.

### Static BSP light flares (2026-09-24; JKA and JKO accepted)

- Next omission after water/console acceptance: generate zero-vertex MST_FLARE
  records using legacy radius/offset/vertex intensity and authored material
  stages. Shared scene geometry feeds existing batched FX/bloom streams.
- Preserve PVS/area masking, map teardown and the existing r_flares setting.
  Vulkan pixel depth replaces blocking center-pixel depth readback; explicitly
  test occluder silhouettes. No new GPU resources, global palette or geometry
  winding changes. See `bsp-flares-run.md` for contracts, scope and candidates.
- Both renderer libraries deployed with matching hashes and rollback backups;
  all 22 CTest suites pass, including 1,480 new flare assertions.
- User passed all five JKA headset checks, clearly distinguished r_flares 0/1,
  and compared with Quest JKXR and OpenJK. Separate JKO `yavin_temple` run passed
  all four checks, including clear r_flares 0/1 contrast and good halo behavior.

### t3_hevil water ordering (2026-09-24; accepted)

- User passed all four requested checks: photographed lake locations, shadows
  on/off, unchanged Yavin river/pool, and no long-Y console outside gameplay.
  This also accepts the gameplay-only console gate below.

- Screenshots `/tmp/t3_hevil/hevil-1-angle1.png`, `hevil-1-angle2.png`, and
  `hevil-2.png` show hard cutouts and clear submerged rock/fixtures, not simply
  uniformly excessive transparency. Latest run: `/tmp/jka-autosprite3.log`.
- Found a concrete composition discrepancy: the shadow path already finishes
  static materials before water; the no-shadow path instead interleaves water
  with their blended finishing stages in BSP surface order. Shadow-map validity
  depends on active casters, not just the selected quality setting.
- Stock BSP audit: lake faces 6996..7004; eight later `evil_rock13` surfaces and
  eleven later `light2`/`light3` surfaces overlap the upper lake bounds and extend
  below z=-144. Their modulate/glow stages must finish before the water surface.
- Use the existing stage-major water compositor on both paths. Water remains
  after shadows when present, before blended effects, with normal depth testing.
  No geometry, winding, palette, alpha, fog, or texture changes. This corrects a
  proven ordering defect; user subsequently accepted the photographed scenes.
- Log `rd-vulkan-water-order` once per map/path, reporting after-solid-materials
  and after-shadows. Source-contract checks reject reintroducing conditional
  water exclusion and enforce stage-major ordering. All 22 CTest suites pass;
  these do not replace scene acceptance or prove visual parity by themselves.
- Both renderers built and atomically deployed, installed/build hashes match.
  Backup: `build-vulkan-clean/pre-hevil-water-order-20260924/`.
  JKA SHA256: `acccc9cc6e13ab48722b56b67f62799c4cd2cc8219b590cec42e910a26c183fb`.
  JKO SHA256: `f67ba5b094758b1cf5a73f3a3bca1b45360b753a676b30d7ff5aeb2306c4b78d`.
- Retest photographed locations at multiple head angles, with current shadows
  and temporarily Off, then restore setting. Check Yavin river and temple pool,
  water animation/stereo, and saber glow above water. No water cvar overrides.

### Gameplay-only spatial console and t3_hevil water report (2026-09-24)

- Map/save automatic console closure accepted. User reports a long Y hold can
  still open a slanted flat panel in menus. Gate all spatial openings on active
  gameplay, and close on UI/cinematic/security-camera takeover. Suppress delayed
  openings from holds crossing these boundaries; keep ordinary short taps.
  Prevent closed VR drawing from using the legacy disconnected-console fallback.
- User clarified the bad water is `t3_hevil`, not Korriban. Latest evidence is
  `/tmp/jka-autosprite2.log`: lakewater uses all four stages, opacity scale 1,
  extinction 0.22, diagnostic off. Nine stock planar surfaces use the same
  `textures/h_evil/lakewater` material as the accepted Yavin river. Two missing
  black local fog volumes are side shafts, not a general lake-water fog volume.
  No causal link to autosprites established. Screenshot/location requested to
  distinguish ordering/coverage from transparency; no speculative shared-water
  change or cvar adjustment. JKO billboard acceptance remains pending.

### Console close across level/movie transitions (2026-09-24; headset pending)

- User finds `devmap` carries console graphics into movies and leaves invisible
  input ownership on the next level. `CL_MapLoading` and `CIN_PlayCinematic`
  already call `Con_Close`; it cleared only the legacy catcher/fractions.
- Reset spatial phase, visibility, repeat/hover state and renderer pose at that
  boundary, immediately and without closing sound/animation. Cancel pending
  binding taps/holds while retaining consumed controller inputs until release.
  Preserve command field/history and Caps Lock. No renderer geometry changes.
- Compiled production-function lifecycle probe covers all phases, binding states,
  idempotence, missing hooks and flat fallback. Map/movie integration guards added.
- Follow-up route audit confirms map/devmap aliases, saved games and scripted
  transitions share the loading boundary. Add closure before disconnect/memory
  flush/renderer teardown and explicit menu commands. Move non-system foreground
  movie closure into successful playback so cached handles close too, without
  background video textures dismissing the console. See `console-lifecycle-audit.md`.

### Static material autosprites (2026-09-22; JKA kor1 accepted, JKO pending)

- Next small batch after Artus acceptance: static four-vertex autosprite and
  axis-preserving autosprite2 geometry. Shared camera basis and per-frame cached
  affine uniforms; no changes to ordinary vertex layout, winding or palette.
- Preserve chain UVs/lightmap UVs and endpoints. Color, lightmap, dynamic-light
  and fog draws use the same transformation. Unsupported topology is logged.
- Korriban chains and JKO blinking-light patches are actual quad candidates.
  Zero-vertex BSP flare records require separate generation and remain pending.
- See `material-autosprite-run.md` for scope and the separate JKA/JKO checks.
- Both renderer libraries deployed with matching hashes, backup retained. All
  22 CTest suites pass; 11,264 production deformation shader samples pass under
  Vulkan validation on the RX 7900 XTX. User accepted JKA chain orientation,
  endpoints, lighting/stereo and ordinary rendering. Log confirms 38 supported
  static chains and four explicit unsupported warnings; JKO remains next.

### Artus room lightstyles, camera housing and floor overlap (2026-09-21; accepted)

- User accepts the preceding JKO lava/slime correction and Valley of the Jedi.
  JKO Force Lightning was initially waived, then explicitly tested and accepted.
- New evidence: cargo room's model reacts to red light while world stays dark;
  active camera assembly obstructs its own feed; circled floor patch flickers.
- Restore secondary lightmap style composition before albedo modulation without
  widening vertex streams. Suppress only active camera head/matching saved base.
  Separate complete material passes for positively overlapping coplanar implicit
  BSP surfaces; no global winding, geometry, water, palette or bias changes.
- Both variants build; 22 tests pass, including eight production-fragment GPU
  checks under validation. JKO renderer and game module deployed and hash-checked;
  JKA installation unchanged. Evidence, hashes, remaining lighting limitations
  and focused run are in `artus-lighting-camera-run.md`.
- User confirms all fixes and lava/sludge/Force Lightning remain correct. Record
  the cargo-room red-light cycle as a later optimization hotspot: compare pulse
  off/on from fixed direct and security-camera views, without altering lighting.

### Artus slime face-selection follow-up (2026-09-21; subsequently accepted)

- JKO run stopped for black slime/thin green line and an unidentified flickering
  decal near crates. Other JKO batch-1 checks remain paused. No confirmed causal
  link to the deformation update yet; this area had not received acceptance.
- BSP data contains top/underside planar pairs with black underside lightmaps.
  The slime's final double-modulate stage can erase the lit top if that underside
  is also submitted. Legacy planar face selection rejects it; the current
  two-sided Vulkan world submission did not. See `material-deform-run.md` for
  surface numbers, evidence and separate local-fog/noise-wave omissions.
- Add a precomputed list of authored one-sided static deforming planes and
  filter their shared-eye PVS mask before direct/indirect draws. This is not
  global Vulkan winding/cull state. Retain two-sided, non-deforming, patch,
  triangle-soup, inline-model and character behavior. Expanded near-plane
  margin accounts for deformation. No water/palette/opacity changes.
- Tests cover plane selection, margin, black-lightmap compositing and source
  integration. Add Artus slime to the opt-in material audit. No speculative
  decal fix; request exact location/image and deformation-off comparison.
- Both builds and all 20 CTest suites pass. Deployed JKO renderer ONLY, SHA256
  `723aede0dbbb5406ddd9ec8e4eaeabb34bec280ac17e2a64c4a72647b32e92fc`, verified
  against build output. Backup: `build-vulkan-clean/pre-artus-deform-faces`.
  Installed JKA renderer remains at its accepted batch-1 hash; JKO engine/game
  module were unchanged. Subsequent user report accepted lava/slime and Valley;
  new lighting/camera/floor issues are handled in the checkpoint above.

### Material wave/bulge/move, batch 1 (2026-09-21; JKA accepted, JKO pending)

- JKA headset acceptance: user reports all requested tests successful, with
  slight cobweb waving, undulating water, correct electrical body effects and
  no observed regressions. `/tmp/jka-deform.log` confirms submitted cobweb,
  water_1, personalshield, fullbodyelectric2 and electric deformations, A/B
  commands and normal shutdown. No Vulkan error or deformation-stream
  exhaustion found. JKO remains the next check, with no intervening code change.

- User approved two small batches, with autosprite/autosprite2 deferred until
  this wave/bulge/move batch is accepted. Before subsequent optimization, ask
  for the promised ARM-oriented measurement details.
- Parse up to three ordered, finite authored deformations. Share one GLSL
  evaluator between world/model color, fog, dynamic-light and shadow/depth
  passes. Use a 160-byte std140 dynamic-uniform block, aligned to the device
  requirement and deduplicated across both eyes by material/time/shaderTime.
  Preserve stage order, legacy table quantization, raw bulge U, separate scene
  and shader clocks, and constant/negative shell expansion. No CPU geometry
  rebuild, additional geometry upload, draw or render target is introduced.
- Identity descriptor bindings are restored across incompatible postprocess
  layouts. World/model/skin/light/shadow bounds include deformation without
  changing stored model centers. Materials without declarations retain identity.
  Collision, authored visibility/PVS and existing shadow distance limits remain.
- New `r_vulkanDeforms 0/1` is a non-archived diagnostic (default 1), documented
  in the cvar reference. First-draw logs identify actual material submission.
- Asset surface audit confirms Rift cobwebs, Rancor water and Artus lava/slime.
  Korriban chains use autosprite2 and are NOT a batch 1 example. Stock move
  declarations found have zero vectors; nonzero motion currently has numerical
  rather than campaign acceptance coverage. See `material-deform-run.md`.
- Both renderer builds and all 20 CTest suites pass. Shared GLSL GPU probe:
  10,240 samples, max CPU/GPU error 0.00000286102 on RX 7900 XTX; validation
  reported no errors in that probe. Source guards check pass coverage, clocks,
  bounds and parser brace handling. This is not full in-headset graphics
  validation or visual acceptance.
- Only renderer libraries deployed; hashes match build outputs. Previous
  installed libraries backed up in `build-vulkan-clean/pre-material-deform`.
  JKA: `d33758b59e73b413e5dc3c8fcbc77fe5cec84eac5d7a73e66494e9e8ab529eb6`;
  JKO: `4e96b89df665887a7cda79a72314858de37ac7a856bdfa4b6380531c0a83de4a`.
  Engine/game-module hashes are unchanged from the preceding checkpoint.
- Separate validation follow-up: the existing main graphics layout declares
  a combined vertex/fragment push-constant range, while some legacy call sites
  update it with vertex-only stage flags. The new shadow-layout calls use the
  matching combined flags. Full graphics validation should audit the remaining
  call sites against VUID-vkCmdPushConstants-offset-01796; this predates batch 1
  and is not exercised by the compute-only deformation probe.

### NPC scripted facing after loading; Morgan accepted (2026-09-21)

- User accepts Morgan's corrected translucency/self-occlusion in JKO. JKA
  `t3_byss` reveals a separate save/load defect: Kyle keeps his door-prying pose
  but starts facing/tracking the player after loading.
- Both games' `gNPC_t::sg_import` read `watchTarget`, then overwrite it with NULL
  before `EvaluateFields` performs the F_GENTITY pointer fixup. At that boundary
  the field is an encoded index: -1 means no target, zero means entity 0 (player).
  `NPC_BSCinematic` then replaces scripted yaw/pitch with angles to that target.
- Remove only that overwrite in both readers. Preserve existing serialization,
  pointer fixup, AI/scripts and rendering. The authored `t3_byss/kyle_door.ibi`
  uses BOTH_CIN_1/BOTH_CIN_2; the brief floating saber also reported in Quest is
  not evidence of this save-state defect and is not changed here.
- Real GNPC serializer round-trip tests for both games reproduce `-1 -> 0`
  before the fix. Cover null, player and other entity indices, scripted yaw/state,
  adjacent fields, complete buffer consumption and byte-identical re-export.
- Live check: start `t3_byss` normally, save in a NEW slot during Kyle's door
  sequence, reload and walk around him. He should keep facing the door. Repeat
  save/load and verify ordinary companion behavior. An original pre-load save
  can also work; saves made after the bad load may already store player index 0.
  Do not automatically erase those targets, as intentional player-watching is
  valid. Preserve old saves; use a fresh-level save for an unambiguous test.
- Both builds and all 18 CTest suites pass. Deployed only JKA/JKO game modules;
  installed hashes match the build. Engines/renderers are byte-identical to the
  prior deployment. Backup: `build-vulkan-clean/pre-npc-watchtarget`.
  JKA module: `b0cd350bea6529a604d54badd0f98a28e7a6d13155880229186d8a4b7a401cf1`;
  JKO module: `7e96a77a9ee22124dac5f30e1e5a5dbf6598e92fd807f543d9eee180cae4959b`.
- Live JKA pass: user restarted from the level beginning, saved after Kyle began
  working on the door, then loaded with his facing/pose intact. Log
  `/tmp/jka-tralala.log` records `auto_t3_byss`, save/reload of `jedi_109`, a later
  `jedi_110` save and normal shutdown, with no reported save/chunk errors.
  Unrelated startup messages mention two UI parsing warnings and missing
  `tutorial_video_7`/`tutorial_video_8` cinematics.
- Deferred separate animation issue: Kyle's saber floats behind him for roughly
  two seconds before any manual save/load, also observed by the user in Quest.
  Its cause is not diagnosed; do not conflate it with watchTarget restoration.

### Morgan outer-shell overlap; JKA input accepted (2026-09-20)

- User accepts JKA dual-saber Force activation; no input changes this round.
  Morgan's chest improves with authored culling, but front-facing shoulder joints
  and wrists still show through outer surfaces.
- Add a color-masked stage-zero depth pass across the eligible GLM shell before
  its normal color/glow stages. Reuse exact skinned buffers and retain authored
  alpha, culling and coverage. Exclude disintegration and two-sided materials;
  ordinary models/world rendering remain on their previous paths.
- This intentionally removes self-overlap independent of surface order, rather
  than attempting to match legacy shader sorting for one camera angle. Adds one
  draw per eligible surface/view, no second skinning pass or new render target.
  Numeric overlap tests and source guards added. Live Morgan retest pending;
  see [shell overlap follow-up](alpha-wave-run.md#shell-overlap-follow-up).
- Both renderer builds and all 16 CTest suites pass. Refreshed generated cvar
  source-line references. Only renderer libraries deployed, with matching
  build/installed hashes; backup `build-vulkan-clean/pre-morgan-shell`.
  JKA: `ff24cef018bdefc9bcd2c877119faaf2a4d0410ec92802db7e965a3a45fa557a`;
  JKO: `e00b19a6ff013eb85ac66b6c934007bda7354bc7524613bdaac6d10785fdbc2f`.

### Morgan self-occlusion and dual-saber Heal follow-up (2026-09-20)

- User sees good explosion effects in both games, with individual cross-fades
  difficult to isolate. Morgan's subtle alpha looks stable in both Quest and
  Vulkan; no exaggerated pulsation is needed. Self-occlusion remains a separate
  defect, not proof that alpha evaluation failed.
- Restore authored culling only for GLM alpha-wave shells with depthWrite and no
  opaque stage (Morgan). Preserve alpha depth writes, additive glow and explicit
  two-sided declarations; do not change global winding or ordinary models.
- Fix missing motion-attack release on deactivation/UI transitions. Selected
  Force and gesture ownership stays as accepted. Add Heal rejection/trigger
  diagnostics because the available JKA logs predate the reported lockout.
- Focused live tests and evidence: [alpha wave follow-up](alpha-wave-run.md).
  Both corrections await headset verification; no new effect added this round.
- Built both games/renderers; all 16 CTest suites passed. Deployed both engine
  executables, both renderer libraries and JKA game module; all five installed
  hashes match the build. Backup: `build-vulkan-clean/pre-morgan-force`.
  Renderer SHA-256: JKA
  `ba13364cce7648e71517567d4d46c92d0e1b31c7101300d4b2dbec6721e6cb89`, JKO
  `c38b481497ff09d9bb0ccdd3d44f8a7b672a0e9e48ed2f98b783cbcd6f405506`.
  JKA module: `fbd6bf9bd72e6e2c17f4aaf42d5963fbb345f24455cd79d172a6e544423f5ec5`.

### Lightning accepted; authored alpha waves (2026-09-16)

- User accepts Lightning direction in both games after extensive saber/firearm
  tests, gaze aligned/diverted and head held still. No further aiming work in
  this batch. Earlier logs confirmed fork generation.
- Next independently testable material omission: alphaGen wave. Geometry
  deformation remains pending because it requires coordinated geometry/depth/
  lighting work; it was not enabled partially or changed incidentally here.
- Parse valid periodic alpha waves and preserve their parameters. Wave output
  is clamped/byte-quantized as RB_CalcWaveAlpha; it replaces generated alpha,
  not RGB. Explicit wave flags gate changes in color, fog and UI. Materials
  without waves retain their previous opacity, including Yavin water tuning.
- Alpha-wave FX also require their own shaderTime for image/alpha cross-fades.
  Only those materials split dynamic batches by start time and select animMap/
  oneShotAnimMap frames, identically for color and glow. No added render target,
  geometry deformation, gameplay/input change or palette adjustment.
- Standard wave evaluator was moved unchanged into a tested header; RGB/UV
  wave equations are unchanged. Five numeric cases and source guards cover
  alpha replacement, lifecycle, adjacent-frame cross-fades and scope isolation.
  Headset acceptance pending: [alpha wave run](alpha-wave-run.md).
- Both renderers/GLSL rebuilt; all 16 CTest suites passed. Backups in
  `build-vulkan-clean/pre-alpha-wave`. Installed/build SHA-256 matches: JKA
  `2a06b3248103e03b084e6a5ebbaf3813f62d561ea1361738bed9b7bfcff540b3`,
  JKO `76e34de33e7bf89a922cc166814e65566a47a6776beadb7364d463f81ef80050`.
  Only the two renderer libraries deployed. Accepted Lightning game modules
  and controls are untouched.

### Force Lightning follows casting hand (2026-09-16)

- Fork test log confirms branches are generated, but user mostly perceived the
  existing wide fan following gaze. Do not mark fork appearance accepted yet.
- JKA's saber FX followed actor/view angles and firearm FX used camera axes;
  JKO firearm FX used torso angles. Damage used adjusted offhand angles but
  cached hand origins and a body-centered cone. Shared BG_CalculateVRLightningPose
  now uses the current offhand pointing pose for both visual paths and damage
  origin/aim. It excludes NPCs, cutscenes, third-person and remote cameras.
- Retains authored level-3 fan, range, target filtering and damage amounts.
  No gesture, activation, material, renderer or other Force-power changes.
  Local VR skips the extra animation-driven right-hand fan. Diagnostics sample
  actual cast origin/angles and player view every two seconds while casting.
- Source guards cover both games, hand-pose policy, both FX paths, cone origin,
  line trace and visibility trace. Headset acceptance pending; focused steps in
  [electricity run](electricity-run.md).
- Both game modules built and all 15 CTest suites passed. Backup in
  `build-vulkan-clean/pre-lightning-hand-aim`. Installed/build SHA-256 matches:
  JKA `f63933e4f321d92042eefb6538d47eeb30a035df66013bd9282ca6ab8b5346b0`,
  JKO `ae6a7f8ceda43c66b00cd7b0128be842bddf26f958cad0e37d2342c477f6b206`.
  Launchers verified to sync these modules by content before starting. No
  renderer, executable, asset or saved-game schema changes in this deployment.

### Devices accepted; forked electricity implementation (2026-09-16)

- User accepted electrobinoculars, light-amplification goggles, Tenloss and
  E-11 scopes in both JKA/JKO, including zoom and battery depletion. One JKA
  log was truncated; this is user visual acceptance, not log verification.
- Next audit item: RF_FORKED was discarded by Vulkan. The shared FX generator
  now adds at most three recursive forks, restricted to the first fifth of a
  parent strand, with 7% opportunities and midpoint +/-80-unit targets as in
  legacy DoBoltSeg. Radius inherits the parent at attachment; RF_GROW applies
  once to the root and RF_TAPERED applies to each strand. Existing trunk shape
  and 64-segment cap remain; total geometry is bounded to 256 line segments.
- Local unsigned RNG avoids global state, entity mutation and eye/glow drift.
  Game damage, controls, materials, depth/blending and global lighting are not
  changed. No extra pass, GPU readback or per-fork draw call is introduced.
- Numerical tests cover topology, seed determinism, trunk compatibility,
  growth/taper, degenerate input and caps; wiring guards cover both games and
  shared color/glow generation. Headset acceptance pending:
  [electricity run](electricity-run.md).
- Both variants built; all 15 CTest suites passed (including four new geometry
  cases and two source-contract checks). Installed/build SHA-256 matches: JKA
  `0755e08df4ab2b58db8fe0174853ea3c638ea4a81712430a98b4ad43bf3b83ec`,
  JKO `4ed11f497b1a7ed1e8084054e5cbcf0bf8122eaba20367a09333043b91189cff`.
  Scope-tested renderers backed up in `build-vulkan-clean/pre-forked-electricity`.
  Only renderer libraries deployed; no game module, executable or asset update.

### Binocular optical zoom; JKA goggles accepted (2026-09-16)

- User confirms JKA light-amplification goggles work. Their intentionally
  unzoomed view and depleted-battery inventory behavior are unchanged. JKO
  goggles and the broader scoped-fog regression run remain pending.
- Electrobinocular FOV changed (log requests range 5120 to 192), affecting fog
  but not magnification: Vulkan optical projection only recognized Tenloss.
  Full-view `gfx/2d/binMask` now enables frame-local optical projection through
  the same tangent ratio. Both axes use the same scale, preserving proportions.
  BeginFrame clears the flag; goggles, security cameras and Force Speed remain
  separate, as do rifle-specific HUD aspect/aim corrections.
- Added numeric optical-zoom coverage and source guards for frame lifetime and
  projection wiring. No game module, input bindings, fog curve or asset edits.
  Follow-up: [goggles/scoped fog run](goggles-scoped-fog-run.md).
- All 14 CTest suites pass. Installed/build renderer hashes verified: JKA
  `ac5ae557d2d224fb2b66e392517d086f8cfd15e9d30116a655922621d30fd6e4`,
  JKO `95fef6d19535e8869b3d8bfea9e038af8d36af1ebc7baee29b40db9f6b64e684`.
  Backup: `build-vulkan-clean/pre-binocular-zoom`. Subsequently accepted in both
  games; see the newer checkpoint above.

### Goggles/scoped fog implementation, marks accepted (2026-09-15)

- User confirms blaster marks and fading in JKA and JKO; the Hoth footprint
  and motor fixes were already accepted. Projected-mark focused acceptance is
  complete; this does not assert animated model gore or moving-brush support.
- Restore LAGoggles and SetRangedFog exports with view-local requests and
  world/portal snapshots. Goggles preserve the game-side battery gate, replace
  baked-light RGB with fullbright, and use warm special fog. Menu models remain
  outside the override. JKA zoom range uses the legacy FOV*64 formula with
  authored distanceCull/linFogStart; zero or new frame/load cannot leave a stale
  request. Ordinary non-ranged fog and JKO scope callers are unchanged.
- Five numeric/state tests and source guards added. All 14 suites pass; both
  renderer variants and shaders build. Headset acceptance pending:
  [goggles/scoped fog run](goggles-scoped-fog-run.md). No new assets/cvars or
  input changes. Backup before deployment: `build-vulkan-clean/pre-view-fog`.
- Deployment verified 2026-09-16: installed/build hashes match, JKA renderer
  `a47079d37d47913c10905d27236c6843f5f9b2b5467d0a28beeb0db0eb4b4db5`,
  JKO renderer `33ed40736353bcf6f66f485f2902890759c3d621004907507f49017ec2ccd8a2`.
  All 14 CTest suites rerun successfully. Game modules, executables and assets
  unchanged in this deployment. Headset results still pending.

### Hoth footprints and mounted motor cleanup accepted (2026-09-15)

- User confirms both left and right footprints now appear. The stock left
  material mirrors the shared right-foot texture through `tcMod transform`;
  dynamic color/glow batches now apply authored affine transforms to private
  streamed UVs. Geometry, winding and mark projection remain unchanged. Full
  ordered tcMod support in other renderer paths remains a separate audit item.
- User confirms killing the gun operator stops the whirring, and repeated
  mount/aim/dismount retains normal audio. `ExitEmplacedWeapon` now clears the
  aiming loop for death as well as voluntary exit; `eweb_die` also clears it.
- Force input was not changed: the reported grip/trigger failure was withdrawn
  after the user clarified that they were using a single saber, not dual sabers.
- Log evidence: successful left/right traces and nonzero fragments for both
  sides; normal client and audio shutdown. Separate warnings: missing tutorial
  videos 7/8 and two UI menu parsing warnings. All thirteen CTest suites passed
  before deployment, including affine-UV and motor-cleanup regression checks.
- Deployed SHA-256: JKA renderer
  `3a40c157344ea7429c3c06ad20daa5c18711d70a19096e459b02ff7a526b29d7`,
  JKO renderer
  `7092419049279a3a2207a12e57e0e48dea686e2a6d5fd8a25f610ec9bb11dd9f`,
  JKA game module
  `eadd68c614900b4b9bc5d8fb6eb738228a28d9ba05146a2cf6c303a6f2f16dfd`.
  Backup: `build-vulkan-clean/pre-footprint-motor`. No broader JKO acceptance
  or wall-impact/fading acceptance is inferred from this Hoth follow-up.

### Projected impact marks and footprints (2026-09-14)

- Replace `MarkFragments`' zero-result stub with CPU clipping adapted from
  legacy `tr_marks.cpp`. Keep the convex footprint's edge planes, 32/20-unit
  end caps, 0.5-unit clipping epsilon, facing cutoffs and 64-surface query cap.
  Reject invalid/degenerate input and respect both caller buffer limits. No
  gameplay trace, GPU readback, camera/PVS dependence or vertex displacement.
- Cache positions/indices from the existing uploaded BSP tessellation, with
  static-world surface ranges, bounds and planar normals. Exclude compiled
  NOIMPACT/NOMARKS/FOG surfaces, plus geometry already excluded from drawing.
  Inline doors/fixtures are not marked at their untransformed authored position.
  Deduplicate BSP-leaf candidates with a query serial, independent of rendering
  visibility. Missing BSP nodes use a bounded-output surface fallback.
- Patch triangles retain the renderer's current tessellation, aligning the
  mark query's facing normal to its authored patch normals. This changes no
  scene winding/culling, terrain LOD or GPU buffers. Destroying/reloading a
  world releases the CPU cache. Cache memory and the first sixteen query
  surface/triangle/fragment counts are logged as `rd-vulkan-marks`.
- `CG_ImpactMark` still owns UVs, allocation, fading and lifetime, for both
  games. Dynamic-effect drawing now applies each shader's existing
  `polygonOffset` flag and resets it for non-offset shaders. No global
  `r_offsetfactor`/`r_offsetunits` adjustment, shader substitution or blend change.
- Six new unit tests cover flat clipping, adjacent-triangle area, sloped
  receiver planes, projection rejection, invalid inputs and guarded output
  capacities. Source guards cover static ownership/material exclusions,
  export wiring, cache release and per-batch depth bias. A standalone probe
  (`tools/check_mark_geometry.cpp`, ASan/UBSan) passed 6090/6090 sampled indexed
  BSP triangles from JKA hoth2/t1_rail/t2_rancor and JKO kejim_post/ns_streets.
  That is geometry evidence, not a headset or complete legacy-render comparison.
- JKA Hoth footprints accepted 2026-09-15; remaining headset checks are in the
  [focused run](projected-marks-run.md). JKA
  footprints require `cg_footsteps 3` and a material flagged for tracks; marks
  require `cg_marks 1`. Existing defaults are unchanged. No claim of skin-gore
  parity or new moving-door attachment support.
- Verification/deployment: all thirteen CTest suites pass. Installed renderer
  SHA-256 matches build output: JKA
  `11429ef77edd67e82751478eb268c72e3a6b5b9c9e596454476eb0246dec28fb`, JKO
  `5bf05599e3f03d25d748b89e57bbe3c5d2ff602934c87f4e85db0fed9d38db7f`.
  Backups: `build-vulkan-clean/pre-projected-marks`. Executables, game modules,
  PK3 archives and user configuration were not replaced.

### Weather-linked fizz and storm fog flashes (2026-09-14)

- Headset acceptance: user confirms the new weather-linked effects work, with
  rain, shelter protection, saber combat, stereo comfort and performance intact.
  This closes the focused JKA run; do not infer a separate JKO campaign test.
- Replace two renderer export stubs: `GetChanceOfSaberFizz` and
  `SetTempGlobalFogColor`. Rain/acid/light rain return 0.10, heavy rain 0.14,
  snow 0.015. Multiple precipitation layers average, matching the legacy
  water-cloud contract; mist contributes neither gravity nor a denominator.
  The query has no mutable state or per-eye random sampling. JKA retains its
  game-side probability/shelter/no-effects guards and stock `saber/fizz` asset
  (steam, short flares and three rainfizz sounds). This also restores the
  probability input used by acid-contact debounce. No new collision scheme or
  frame-rate normalization of the original game-side RNG is introduced.
- JKO already uses `WF_RAINING`, outside contents and blade-length probability
  for fizz; retain that path rather than replacing it with JKA rules.
- A separate fog override preserves the authored base and depth. Nonzero input
  activates/replaces it; zero releases it. `fx_rain` documents and sends byte RGB
  (default 200 200 200), so normalize it to 200/255 instead of sending 200 to a
  normalized shader. Accept normalized callers too; reject invalid/nonfinite
  inputs. Background clear and world/model fog passes use the same effective
  color. No global palette, lightmap, particle-blend or postprocess changes.
- Begin-level/reset and world destruction discard the override, including
  save/load into an existing world. Weather `clear` retains legacy particle/wind
  semantics; it does not impersonate a storm entity's explicit flash-off call.
  The original `fx_rain_think` game-time schedule controls flashes; Vulkan
  does not advance a separate animation for each eye. No artificial head shake.
- Asset audit: `t1_rail` has `fx_rain` flags 20 and global `textures/fogs/rail`;
  `vjun1` has flags 8 and global `textures/fogs/vjun1`. The former implicitly
  enables heavy rain's lightning scheduler; acid rain alone does not enable it.
- Unit tests cover presets, repeated queries, mixed layers, bounded/tinted
  colors, repeated flash/restore, rejected input and level reset. Source guards
  check exports, both fog consumers and unchanged game-side effect guards.
  Runtime logs report `fizzChance` when adding weather and the first sixteen
  storm flash/restore calls per level. The accepted test protocol is retained
  in [the focused run](weather-linked-run.md).
- Build/deploy verification: all twelve CTest suites pass. Both installed
  renderer hashes match their build outputs: JKA
  `1bc6f472d89eba11828652a1755bc3812e331b4abd9fba33bdd6fa77134c06dc`, JKO
  `e9b34263210efe7cea8b2daa89698ae6bd8e64cb5d1e845a189cbe7b698d7679`.
  Previous renderers are in `build-vulkan-clean/pre-weather-linked`. No game
  module, executable, PK3 or user configuration changed in this deployment.

### Dual-force wheel cancellation and time ownership (2026-09-14)

- User reports permanent selector-like slow motion after a dual-saber gesture
  while Force Sense is active. Root cause: the chord only cleared the shared
  `vr.item_selector` flag. Cgame had already set `timescale` to `0.22`; only
  `itemselectorselect` restored it. Hiding a wheel is not the same as closing
  its cgame lifetime. Force Sense's 5/10/20-second expiry uses `level.time`, so
  leaked slow motion also extends its real-time duration by about 4.5 times.
- Add `itemselectorcancel` to both games. It releases the temporary time
  override and clears the wheel timer/highlight without selecting or activating
  a power, gadget or quick-save action. The grip-trigger chord queues this
  command once when dismissing an open wheel.
- A shared, tested time-ownership helper captures pre-wheel time only once,
  restores it on exit, and does not overwrite an externally changed timescale.
  Selection shares cancellation cleanup, including early-return paths.
  Hidden/console/loading frames release the time override; death and shutdown
  cancel. Frame fallback preserves the highlight until queued select/cancel
  commands execute, because controller polling can precede command execution.
- Diagnostic `jkxr-selector-time: release <old> -> <new>` is emitted when
  `vr_controller_debug` is enabled. No renderer, Force Sense lifetime, movement
  or gesture-threshold changes. Four unit cases cover repeated wheel frames,
  chord handoff, prior/external timescales and cancellation before first draw;
  source-contract checks cover both cgames and the queued-command boundary.
- Verification: all 11 CTest suites pass. Executables and game modules deployed
  to `/usr/lib/jkxr` with matching build/install SHA-256 pairs. JKA executable:
  `790fb9f20f52417019b17144167e9d139d82d1407f529cc38d10d5eaf5c27e53`;
  JKO executable:
  `6aecc7f175c601ebb582e68fda6a0c9c75fd79466ca28ea0726cf2711a878550`.
  Previous files retained in `build-vulkan-clean/pre-selector-cleanup`.
- Headset acceptance confirmed by user: repeated dual-saber gestures with
  Force Sense active, including both button-release orders, return immediately
  to normal speed without conflicts or a stuck state. Grip-only selection,
  sustained powers and normal Force Sense behavior remain working.

### Explicit dual-force chord and mixed crystal surfaces (2026-09-14)

- Acceptance update: user confirms the `t3_rift` crystal correction was already
  tested successfully. Do not continue listing it as awaiting confirmation.
  The subsequent dual-force selector time leak is resolved and accepted in
  the entry above.
- User rejects temporal gesture/selected-power arbitration: motion keeps
  reserving the gesture and prevents sustained lightning. Remove both the
  350 ms delay and motion-intent threshold. Trigger alone now starts/holds the
  selected power immediately. Grip already held at trigger press instead
  selects gesture mode with arming haptic; one gesture per hold. Latch mode
  until release or cancellation, with no release-time fallback cast.
- The chord cancels an open force wheel without committing its selection and
  consumes grip until release, avoiding reopening the wheel during casting.
  Grip alone still opens the wheel. Primary saber and single-saber controls
  are unchanged; offhand attacks remain suppressed while casting. Diagnostic
  logs record chord arming and selected-power press/release independently.
- Crystal scene corrected by user: the loaded save `jedi_093` and renderer log
  identify `t3_rift`, not `vjun3`. Stock `crystal_pilar.md3` contains one
  stone surface with opaque environment + alpha albedo stages and 20 additive
  `textures/rift/env_crystal` surfaces without depth writes. Whole-model
  early/late classification incorrectly sends those crystal surfaces through
  the solid finishing phase before background material/fog finishing passes.
  Those passes can paint over a nearer non-depth-writing crystal.
- Split standalone MD3 scheduling at surface/material ownership: opaque and
  depth-writing material stages stay early together; wholly blended surfaces
  finish late. Apply the same selection for fog and entity shader overrides.
  Preserve all existing culling, projection, depth tests and blend factors;
  no world, Ghoul2, water or per-map palette changes. A CPU ray probe of the
  authored model at eight horizontal angles found no stone-backface masking
  case, so the speculative culling change was NOT made.
- Replace temporal input tests with explicit chord/hold/cancellation cases.
  Add compositing regression coverage for mixed stone/crystal materials and
  no duplicate early/late drawing. Headset acceptance remains required:
  sustained lightning/heal, chord gestures, grip-only force wheel, and crystals
  from both sides with nearby pillars as well as genuinely occluding walls.
- Verification: all eleven CTest suites pass. Installed executable/renderer
  pairs match SHA-256: JKA executable
  `d1e50f65583faa441d54476d9f76611528e7a20b47d4e6a056759664df380e89`, JKO executable
  `df3eba98146cac5268d097f0a3bc530b771089f444a5d30d7388f34265128b7c`, JKA renderer
  `6ae32af11bfacb473ee1d9bfbfbf4ad38c0b24a027bba4fb57a845193f76c387`, JKO renderer
  `4d32ce30513effdbd1c2b0e8d8db8b7f624b8ef0941daaf20d2e2b2d4ddf94ee`.
  Backups: `build-vulkan-clean/pre-force-chord-crystals/`. No game-module or
  asset replacement needed for this revision. Runtime acceptance is pending.

### Sustained dual-saber powers and dedicated typing sound (2026-09-14)

- User confirms dual-saber push/pull and ordinary combat, persistent console
  prompt and no console drift. Rejects the reused opening sound as a key click;
  reports the tap-only trigger route cannot sustain lightning/healing.
- In dual casting mode, a steady 350 ms hold now asserts the ordinary
  `+useforce` button until release. No power-specific substitute command or
  game-logic bypass: preserve the game's selected-power semantics, including
  channeled powers. Short taps still generate one button impulse on release.
- Movement exceeding max(2.5 cm, one quarter of the configured full gesture
  distance) reserves that hold for gesture recognition; slower developing
  gestures may complete beyond 350 ms. Completed/incomplete gestures do not
  also activate the selected power. Once sustaining starts, moving to aim
  cannot produce push/pull. Menus/death/tracking loss/context changes cancel
  sustaining and require release before rearming. Offhand damage suppression
  and the primary saber's ordinary combat remain unchanged.
- Replace virtual-key sound with original `sound/interface/console_key.wav`:
  24 ms, mono PCM16 at 44100 Hz, band-limited noise transient, -14 dBFS peak.
  Deterministic generator in `tools/generate_console_key_click.py`; shared asset
  source in `z_vr_assets_base`. Opening/closing audio and key haptics unchanged.
  Compared pack contents against both installed games: only the click is added.
- Add four state tests for sustaining/release, gesture reservation, blocked-input
  cancellation and incomplete-gesture release; check the generated WAV and
  virtual-key sound routing. Prompt preservation and console movement gating
  are not modified. Live acceptance pending for the revised trigger arbitration
  and typing sound in both games.
- Verification: all eleven CTest suites pass. Both installed executables and
  both copies of the shared PK3 match build SHA-256: JKA
  `cae214da526c2e3704ba2124a36b2829157f5e9e96d2b1f97d395dc6534526d2`, JKO
  `8125cd0777f51f38243fb73fbefbdf2982c260697766ddd8b34c1b3c10c8564f`, shared PK3
  `357da6c5cfdb3d137ccb5b9eee5d133afbbf23c9c399e45c4d22dd48f2a90bcd`.
  Backups in `build-vulkan-clean/pre-sustained-force/`. Launchers content-sync
  the sound pack at next start. Game modules and renderers are unchanged.

### Dual-saber casting and console input comfort (2026-09-14)

- Replace the dual-saber gesture veto with an explicit offhand-trigger casting
  state when `vr_force_motion_controlled` is enabled. Fresh press arms with a
  short offhand haptic and clears pre-press gesture history. Existing radial
  displacement, palm direction, cooldown and usable-target priority remain.
  One dispatched push/pull consumes the hold; releasing cannot also activate
  the selected power. A gesture-free tap <=350 ms activates the selected power
  on release through the existing one-frame button impulse path. A longer
  unsuccessful hold does nothing on release. Single-saber/direct-trigger paths
  remain immediate; disabled gestures retain the old direct-trigger behavior.
- Console/menu, death, tracking loss, camera/vehicle, selector and weapon-context
  changes cancel casting. A held trigger cannot arm again until released.
  Suppress the offhand velocity attack and JKA offhand damage trace while the
  modifier owns the trigger; keep blade history current to avoid a deferred
  swept hit. Primary saber attacks remain available. No saber-color/glow tuning.
- Spatial-console toggles and forced closes preserve unfinished prompt text,
  cursor and edit scroll. Submitted commands still clear normally; no across-
  process persistence added. Flat-console `con_autoclear` behavior is unchanged.
  Virtual-key activation (including modifiers and repeats) plays the existing
  interface button sound alongside the short key haptic. Spacers/hover are silent.
- Controller filtering previously left headset-derived positional movement and
  final keyboard/queued commands live. Clear VR movement before command assembly,
  then clear final movement/buttons/generic actions while the console owns input,
  including its closing animation. Keep head tracking and ordinary game physics;
  this is not a world pause, invulnerability, or suppression of external forces.
- Five state-machine unit cases cover tap, consumed hold, long hold, cancellation,
  entry while held and time reset. Five source-contract checks cover prompt,
  click/repeat routing, final command suppression and offhand damage isolation.
  These do not replace live headset testing of gesture feel and input timing.
- Live checklist: dual-saber swings without trigger do not cast; trigger+push/pull
  casts once, no selected-power activation on release; tap activates selected power;
  right saber still attacks while casting. Console clicks are audible, unfinished
  text survives close/reopen, and stationary-ground use produces no lateral drift.
  Verify single-saber controls and console behavior separately in JKA and JKO.
- Verification: both executables and game modules build; all ten CTest suites
  pass after regenerating the cvar reference. Installed SHA-256 pairs match:
  JKA executable `aa7cd2cf2067758cbcd324a08792351332d1f54bb3ec3fdbb209c0ab7690975e`,
  JKO executable `b1adea07246922090db9579eacb351cf4dc05747974c0b798496caac79a9bb21`,
  JKA module `9b451850df88e20b3462478e1d5fc8e62bd26686f8b8e9cc42283d5df0a851f8`.
  JKO module remains byte-identical to its previous build. Backups are under
  `build-vulkan-clean/pre-force-console/`; launchers sync installed modules on
  startup. Renderers/assets are untouched. Headset acceptance remains pending.

### Dual-saber visual-effects audit (2026-09-14)

- User reports weaker/different effects on the left saber. Static audit:
  `CG_Player` submits both hands through `CG_AddSaberBladeGo`/`CG_DoSaber`,
  with per-blade glow/core entities and dynamic lights. No hand-specific
  exclusion was found in Vulkan dynamic-effect or light submission. This does
  not prove the observed runtime output is identical.
- Saved settings select identical `single_1` hilts, orange primary and blue
  secondary, with bloom off. Stock `sabers.shader` marks both colored glow
  materials as glow sources, but neither core line. Vulkan additionally treats
  `orange_line` as a source for the scepter beam. That material-wide exception
  also affects ordinary orange sabers: their core receives radius scaling
  (1.12 * 1.18 with saved settings and bloom off) and optional bloom extraction;
  blue cores do not. Both colored halos retain their authored glow treatment.
- Do not equate a color-specific difference with missing offhand rendering.
  Pending: identify the reported effect (halo, surface light, trails/contact,
  haptics), compare matching colors/hilts, then swap colors between hands.
  No renderer tuning or binary deployment was made for this audit.

### Weather texture upload during frame recording (2026-09-14)

- First `t1_rail` weather test crashes on load. Latest system core: PID 2021551,
  2026-09-13 23:57:52 CEST, SIGSEGV in RADV. GDB unwinds to `VK_RenderEyes`;
  disassembly at its return address shows the failing call is
  `vkCmdEndRenderPass`, not a subtitle/font call. Installed release binaries
  have symbols but no local-variable debug information.
- Log reaches the first frame with heavy rain (1000 particles), mist (70), and
  constant wind (-5000,0,0). The new weather batch builder lazily registers its
  textures from inside the render pass. `VK_CreateTextureFromPixels` resets the
  same command pool that owns both eye buffers, records/submits an upload into
  the active buffer, and ends it. Continuing the old frame then has invalid
  render-pass state. This is an introduced lifecycle regression, not evidence
  of a driver defect or corrupt save. CPU weather math tests did not cover it.
- Prepare all weather textures once before the stereo pool reset/eye recording
  loop. Drawing only consumes prepared handles; failed resources are skipped
  and reported rather than retried through an upload inside a render pass.
- Add a recording-phase guard across BOTH eyes to each shared-pool upload
  entry point (buffer, new texture, cinematic update). It rejects/logs an
  unexpected late upload before allocation/reset, preserving the frame's
  command buffers. No uploader/pool architecture rewrite or weather visual
  tuning is included. Keep the separately deployed text-placement fix.
- Four source-contract tests verify preflight order, draw-only weather paths,
  guard placement, and rejection of an injected copy of the lazy-upload bug.
  These are structural regression checks, not an end-to-end VR test. Rebuild
  both renderer variants and run the full test suite; then retry the same
  `t1_rail` save from a fresh launch before continuing the extended weather run.
- Verification: both renderers rebuilt, all nine CTest suites pass. Installed
  files match build SHA-256: JKA
  `af5c4a7a554e0367c7aaa6ca36b4bdf9acbfc2cd205e59e76d634ebe55fea936`,
  JKO `5439fde559087970904fd1e5ce0349a6897e618fe85395854ca1516bb0f74090`.
  Original renderer libraries and crash log retained under
  `build-vulkan-clean/pre-weather-upload-fix/`; extracted core at
  `/tmp/jkxrl-rail-weather.core`. User subsequently confirms working weather
  on `t1_rail`, `hoth2`, and `vjun1`; loading `t1_rail` is no longer blocked.
  `t2_wedge` mist remains visually unconfirmed, possibly too subtle. Separate
  JKO weather acceptance remains outstanding.

### Scripted text and dialogue centering (2026-09-13)

- Before the extended weather run, user reports white text in `vjun3` too high
  or too far right. Exact spoken/message text was not supplied; inspect both
  caption and scripted center-print paths rather than keying a fix to this map.
- Both `CG_CenterPrint_f` handlers place default script messages at y=120 in
  640x480 coordinates. Move that default to y=216, just above the gaze center.
  Explicitly positioned center messages (e.g. camera instructions) are unchanged.
- Caption and center-print lines measure width at final `FONT_SCALE`, but
  `cgi_R_Font_DrawString` also scales the starting coordinate through
  `CG_AdjustFrom640`. Halving the already-scaled width before that transform
  shifts the visible midpoint right by half-width times (1-originScale).
  Undo origin scaling for the horizontal half-width only, using the actual
  HUD transform. Leave stereo/asymmetric offsets to their existing paths.
- Apply the correction only to captions and center-print text in both games.
  Caption height, glyph size, timing, language handling and line breaks remain
  unchanged. Do not change the global font wrapper, menus, scrolling crawl,
  objective-notification placement, HUD projection or weather implementation.
- Two pure-helper tests (52 assertions) cover short/long lines, HUD scales
  1/0.5/0.4/0.25, both eye offsets, unscaled panels and invalid-scale fallback.
  Both game modules build. Live acceptance remains pending: check the `vjun3`
  white messages, short/long dialogue captions, and ordinary cinematic captions
  during the already planned weather run. No new launch options are required.
- Verification: all eight CTest suites pass (105 unit cases). Both installed
  game modules match build hashes; the launchers content-sync them on next run.
  Backups are under `build-vulkan-clean/pre-text-placement/base/`. Installed
  renderer hashes are unchanged from the wind/layers deployment.
- User reports the subtitle placement issue appears fixed during the extended
  weather run. No additional text-layout adjustments requested.

### Wind and independent weather layers (2026-09-13)

- User accepts Vjun2 locked/unlocked door indicators. Resume the atmospheric
  omissions audit with shared wind state and independent weather batches.
- Replace the singleton snow/rain selection with up to five additive layers;
  implement the three stock moving-mist presets. Restore constant, variable,
  gusting and bounded local winds plus normalized gameplay queries. Preserve
  authored strengths and use deterministic time-based updates shared by eyes.
- Preserve exact acid-damage queries and level reset semantics. Weather
  visibility is tested per particle, including from sheltered cameras; remove
  the obsolete coarse snow cache. Do not change BSP fog, palettes, materials,
  vegetation scaling, or movement rules to implement the missing exports.
- Pure helper tests cover command/state contracts and simulation timing. Both
  renderer variants build; all eight CTest suites pass (103 unit cases, including
  14 weather cases / 12,163 assertions). Runtime visual acceptance is pending.
- [Implementation boundaries and focused test routes](weather-wind-run.md):
  primary JKA cases `t1_rail` (rain + mist + strong wind) and `hoth2` (snow +
  constant/gusting wind), optional `t2_wedge` (light mist), regression `vjun1`.
  Follow with a separate JKO `yavin_swamp` check. Verified shipped BSP requests;
  do not mistake absent storm fog flashes/fizz for regressions in this batch.
- Deployment: only the two renderer libraries under `/usr/lib/jkxr` replaced;
  backups retained in `build-vulkan-clean/pre-wind-layers/`. SHA-256 build and
  installed pairs match: JKA
  `214fa1859fafbca8a83b34eee6864478bbdc5df251afd1132da34b8bf293d6f8`,
  JKO `af88782464c09f9fa8986e0d3d63026f4688e3642e5a314e9aecab288e4c2383`.

### Brush shader state and door indicators (2026-09-13)

- User accepts the Force-use blue rings and the Vjun2 updraft. Next report:
  Vjun2 door indicators remain green even where the original displays red.
- The game already transmits this state: `MOVER_LOCKED` initialization sets
  `EF_SHADER_ANIM` and frame 0; unlocking sets frame 1, relocking sets frame 0.
  Cgame maps this to `RF_SETANIMINDEX` and `refEntity.skinNum`. Vulkan's mesh
  model path honored it, but `VK_RecordInlineModelSurfaces` omitted the selector.
  Its material stage therefore used world time and reached the final green frame.
- Stock `textures/vjun/door1_onoff` and `door2_onoff` use `oneshotanimMap 1`
  with red then green. Vjun2 door models *139 (`door_ready`) and *140
  (`kyleDoor1`) explicitly start locked (spawnflags 16). This is state selection,
  not a color-profile, decal sorting or depth-bias defect.
- Pass the brush entity's explicit animation index into the existing indexed
  stage draw. Keep the selector local to that draw, never mutate the shared
  material. Frame zero is an explicit selection, not the time-based sentinel.
  Unflagged brushes continue their authored time animation. Single-shot clamping
  and looping remain unchanged for finite ordinary inputs; undefined non-finite
  or overflowing time conversions now have bounded results in the tested helper.
- Shared renderer implementation covers both JKA/JKO and other flagged brush
  indicators, including shader-switchable statics. Door opening/locking logic,
  cgame flags, vertex data, water, lightmaps, blend modes, sorting, decal bias and
  authored shader files are unchanged. This does not add new unlock conditions.
- Four production-helper tests cover explicit red/green state at early/late
  times, relocking, independently selected entities sharing a material,
  deterministic repeated-eye selections, bounds, and 1,000 timed samples compared
  against the prior loop/single-shot calculation. User subsequently accepts
  door indicators reflecting locked/unlocked state.
- Test existing Vjun2 saves with locked and available doors; if the story unlocks
  a door, verify its indicator changes then. Reload a save to verify restored
  state, and inspect another level's doors and ordinary animated panels.
  No additional cvars or replacement assets are needed.
- Verification: both renderers build; all eight CTest suites pass (95 unit
  cases, including 4 shader-animation cases/2,031 assertions). Installed
  renderer hashes match build outputs; originals retained under
  `build-vulkan-clean/pre-door-indicators/`. No engine/game modules or assets
  were replaced in this batch. Visual acceptance is recorded above.

### Force-use hints and Vjun2 updraft (2026-09-13)

- User accepts breakable windows across multiple JKA levels, including more
  than the requested `t2_dpred`/`t1_rail` checks. Do not infer acceptance of the
  five separately documented unsupported JKO panes from this result.
- Restore the shipped `gfx/hud/force_swirl` material (three additive stages,
  counter-rotation at 195/-300 degrees/second) to the VR off-hand target ray.
  The old 2D crosshair drew this material; the 3D crosshair only drew its ordinary
  marker. No evidence establishes why upstream omitted the rings.
- Both games check the actual traced mover for `func_door` Force activation or
  `func_static` Push/Pull flags, matching available powers and player distance.
  Rings are depth-tested world sprites, bounded in size, slightly offset from
  the hit surface. They do not add a full-screen layer or change targeting,
  gestures, energy costs or scripted activation rules. Existing
  `cg_crosshairForceHint` enables/disables them independently of the ordinary
  Force cursor; default remains 1. Heal/Speed selection does not suppress hints
  because the Push/Pull gestures remain available. Cinematic/scope/vehicle and
  weapon-stabilization exclusions remain in effect.
- Vjun2's updraft is gameplay `trigger_push`, not a missing weather renderer
  effect. The stock player volume is model `*143`, flags 21 (PLAYERONLY, LINEAR,
  RELATIVE), speed 550, target `t385` at (768,192,2912); volume bounds are roughly
  (576,0,32)..(960,384,2366). NPC volumes *76/*79 use flags 28. The existing VR
  body filter incorrectly interprets bit 4 as USE_BUTTON on every trigger,
  skipping the player's LINEAR push trigger whenever gesture-use is enabled.
- Restrict both hand dispatch and body suppression to `touchF_Touch_Multi`
  triggers with USE_BUTTON. Push/hurt/teleport triggers keep their normal body
  contact path; a hand-use pulse cannot invoke a linear push trigger anymore.
  Do not change authored speed/target, gravity, fall damage or jump behavior.
- Correct the same swept-touch loop's deduplication key: `EntitiesInBox` slots
  can reorder between sampled positions. Track stable entity numbers, so a
  previously touched trigger cannot mask a different trigger that later occupies
  the same result slot. Apply the shared policy and identity helper to both games.
- Four production-helper tests (31 assertions) cover class-specific flag meaning,
  Vjun2 flag combinations, reordered/overlapping trigger lists, fresh move state,
  Force hint power/range/target rules and bounded marker size. These do not
  substitute for traversal and visual acceptance in the headset.
- `vr_controller_debug 1` records `jkxr-push-trigger` at most once per second:
  entity/model/flags, player origin, resulting velocity and trigger-pushed flag.
  Pending test: enter the Vjun2 shaft from multiple positions, then check a real
  hand-operated use fixture and ordinary jumping. Point the off hand at a Force
  lever/door (Vjun2 or the Yavin2 course), confirm blue rotating rings and that
  pointing away/being out of range removes them. Test `cg_crosshairForceHint 0/1`
  and check ordinary weapon/Force crosshairs remain usable. JKO follow-up needed.
- Both game modules build, all eight CTest suites pass (91 unit cases), and
  installed module hashes match the build. Backups are in
  `build-vulkan-clean/pre-vjun-trigger-hints/base/`; launchers copy updated
  modules into their per-user runtime paths by content comparison. Renderer,
  executables, asset packs and save format were not changed by this batch.

### Lighting and glass output contracts (2026-09-11)

- User accepts save selection and Vjun acid rain as working. This does not
  establish acceptance for the other atmospheric omissions in the audit.
- Replace unwritten `GetLighting` output with an allocation-free, bounds-checked
  trilinear query of the loaded BSP grid and current light styles. Return
  ambient/directed RGB and a normalized world-space direction; match the Quest
  default raw-grid range and 0.5/1 ambient/direct scales. No model minimum-light
  boost, dynamic lighting, local-axis rotation or display color conversion.
  Missing/malformed grids return false with initialized 255 RGB and a finite
  sun/up direction. Empty wall samples return zero lighting and a finite
  direction. Clamp axes independently at grid edges; do not wrap into another
  row. The visible model/world shader paths are deliberately unchanged.
- `GetBModelVerts` resolves renderer handles to inline BSP model indices.
  Cache the two largest planar faces separately from existing hologram-facing
  normals. Recover four outer corners from compiler-inserted boundary/interior
  vertices and retain authored normal direction. Choose the broad face facing
  the gameplay camera. Allow eighth-unit BSP coordinate rounding, but reject
  degenerate, grossly warped and non-quadrilateral faces. Cache lifetime follows
  the world; no per-frame mesh rebuilding or GPU readback is needed.
- Preserve the existing void ABI: unavailable panes produce zero outputs and a
  bounded warning. Both games initialize outputs and validate the returned quad
  before invoking `CG_DoGlass`. On failure, break sound, collision removal and
  target activation still run; invalid coordinates never seed shard generation.
- Ten production-helper tests cover interpolation, style changes, empty corners,
  malformed inputs, edge clamping, quad reconstruction, BSP quantization,
  opposite viewing directions and invalid tessellation seeds. The read-only
  `tools/audit_glass_assets.py` drives `tools/check_glass_geometry.cpp` against
  stock assets, without copying their contents into the repository.
- Asset coverage: 80/80 stock single-player JKA panes have supported broad faces;
  JKO has 287/292. Remaining JKO cases are `ns_streets` inline models
  `*106`, `*107`, `*172`, `*173` (five-sided panes), plus the complex `ns_hideout`
  model `*26`. These still break but lack generated shards. Complete polygonal
  pane support separately; do not claim full glass parity or restore unsafe
  first-four-vertices truncation for these cases.
- Diagnostics: `rd-vulkan-light-query` reports the first two point queries per
  world; `rd-vulkan-glass-query` reports the first sixteen glass requests with
  renderer handle, inline model and validity. `rd-vulkan-glass-cache` counts all
  inline faces, not only `func_glass` entities. These logs establish execution,
  not headset visual acceptance.
- Pending headset test: JKA `t2_dpred` or `t1_rail`, shoot breakable glass and
  inspect shards, sound, collision removal and stereo stability. JKO
  `kejim_post` or `kejim_base` is the follow-up. Load another save/map and repeat.
  Check ordinary lighting, NPC behavior, accepted water/shadows and performance;
  no new cvars, asset packs or menu files are required.
- Verification/deployment: both variants build; all eight CTest suites pass
  (87 unit cases, including 10 query cases/73 assertions). Renderer and game
  modules installed under `/usr/lib/jkxr`, with matching build/install SHA256
  pairs; originals retained in `build-vulkan-clean/pre-output-contracts/`.
  Content-comparing launchers synchronize the game DLLs into the user search
  paths on the next run. Executables and save serialization are unchanged.
  JKA renderer hash: `438ba7b2d20a5f97a904832ad9ac3cd68d2506f4bdb8f12731aa4ed0d88293c4`;
  JKO renderer: `0f22bb7e97febf7e36cc97d3739a97f50333645340024824190c1ae382a809f8`.

### Save-menu selection identity (2026-09-10)

- User reports selecting a Vjun1 save but loading another/latest save. The
  captured log records menu loads of `auto_vjun1`, `jedi_071`, `jedi_074`,
  `jedi_074`, plus a separate load without the UI selection message. The explicit
  server load names match the UI messages; old logging did not record the painted
  highlight, so it cannot reconstruct precisely which row the user saw.
- Confirmed UI defects: double-click script runs before committing the clicked
  row to the feeder; its timer is global, without row/list/button identity.
  Refresh synchronization picks the registered shell menu even if it is hidden
  and the active list is in `ingameloadMenu`. Both defects can separate load
  selection state from the visible menu.
- Commit selection before double-click activation, require the same list/row/
  button, consume the click pair and clear history on menu close. Synchronize
  both shell/in-game lists after refresh for both games. Preserve the selected
  filename across directory reordering, use filename to break equal-time ties,
  and keep the selected row in view.
- Load reads the committed cursor used by list painting, bounds-checks it and
  snapshots its full filename before closing menus. It never substitutes the
  hover cursor or automatically falls back to an unrelated save. Invalid feeder
  and delete selections are also bounded. Selection/load logs include menu,
  row, filename, map and date for the next run. Save bytes/serialization, respawn
  semantics and the explicitly separate Resume Mission action are unchanged.
- Six production-helper tests (28 assertions) cover commit-before-activation,
  different rows/lists/buttons, triple clicks, close/reopen, timeout/backwards
  clock and invalid/empty selections. These are not a full UI/headset test.
- Pending run: select a recognizably older Vjun1 save and use Load; repeat from
  inside gameplay and after scrolling. Try selecting another row immediately
  before double-clicking the target row. Verify the exact saved position/state,
  not just the level name. Preserve `/tmp/jka.log`; pre-fix evidence is copied
  to `/tmp/jka-save-selection-before.log`. JKO uses the same UI implementation
  and needs a follow-up selection smoke test. No need to create/delete saves.
- Accepted 2026-09-11: the user reports savegames working correctly after the
  fix. The separate JKO smoke check remains useful; no JKO-specific result was
  stated in this acceptance.

### Remaining effects audit (2026-09-10)

The Vjun rain omission prompted a cross-check of legacy GL/GLES weather commands,
Vulkan renderer exports, gameplay callers and shipped BSP/shader references.
[Effects parity audit](effects-parity-audit.md) records confirmed omissions,
partial contracts, inherited limitations and candidate test levels. Full
functional parity is not yet established. Prioritize live unwritten lighting
and glass outputs, then complete atmospheric commands/callbacks and remaining
marks, goggles and material semantics in isolated regression-tested batches.
The audit is documentation-only; the acid-rain test deployment is unchanged.

### Vjun acid rain and outdoor damage (2026-09-10)

- The captured JKA log sends 45 weather zones followed by `acidrain`, but
  Vulkan previously ignored that command. Its `IsOutside` and
  `IsOutsideCausingPain` exports also returned constant false/zero. The existing
  `P_WorldEffects` damage loop therefore never received an exposure signal.
- Implement rain/light/heavy/acid presets using the shipped `gfx/world/rain.jpg`
  and shared stereo particle stream. Acid rain uses green, long vertical
  streaks; particles fall in world space and are rejected inside shelter,
  solids and water. Rain remains visible through doorways from an indoor camera.
  Weather does not inherit the vegetation draw-distance multiplier. Existing
  snow placement and its indoor-camera suppression are retained.
- Match the legacy inside/outside volume convention: explicitly marked outside
  maps require `CONTENTS_OUTSIDE`; otherwise `CONTENTS_INSIDE` excludes weather.
  The shipped Vjun1 and Hoth2 maps use the latter convention. Gameplay exposure
  queries use exact collision contents, not neighbouring cached particle cells.
  The float pain API returns an exposure gate, not a damage amount. Existing
  damage timing, armor and protection logic remain in the game DLL unchanged.
- Support `outsidepain` toggling. Legacy `clear` removes precipitation/wind but
  preserves outside pain; level reset clears both, including shelter caches.
  Do not force rain based on a Vjun map-name prefix: Vjun2/3 do not have the same
  shipped weather-zone setup. Honour the commands authored by each map/script.
  This does not implement rain splashes, heavy-rain fog or thunder effects.
- Both renderers compile; six weather helper tests (48 assertions) cover preset
  selection, shelter conventions and world-space fall/wrapping. All eight CTest
  suites pass. These checks do not establish headset visual/gameplay acceptance.
- Pending headset run: Vjun1 opening cinematic and outdoors show green rain;
  exposure causes damage and shelter stops it; rain remains outside when viewed
  through a doorway. Check Hoth snow and a subsequent dry level for regressions
  or leaked acid damage. Keep `/tmp/jka.log` for `rd-vulkan-worldfx` diagnostics.
- Accepted 2026-09-11: the user reports acid rain working. Other weather commands
  and the broader Hoth/dry-level matrix remain separate acceptance items.

### Thermal-detonator release grace (2026-09-10)

- User accepts mounted pitch reversal and standing activation/dismount, then
  reports thermal throws falling nearby when the trigger is released after
  the arm stops or starts returning. Both game DLLs previously estimated speed
  and direction from fixed weapon-offset history slots 2 and 5 at projectile
  creation, not at input release. The sample interval therefore depended on
  framerate and the release/spawn delay could select recovery motion. No
  measurements yet establish additional WiVRn input latency.
- Shared thermal-only input history now measures tracking-space hand positions
  over approximately 50 ms (up to 120 ms at low frame rates). Stick locomotion
  and head translation do not enter that velocity estimate. While primary or
  alternate fire is held, retain outward strokes of at least 0.8 m/s, using
  motion away from the chest to distinguish recovery. At release select the
  strongest outward sample within `vr_thermal_throw_grace_ms`, default 250,
  archived and runtime-clamped to 0..400. Zero disables past-stroke retention.
  Without a recent qualifying stroke, retain measured gentle-toss/drop motion;
  there is no minimum-speed boost, automatic forward throw or target snapping.
- Freeze the selected tracking-space velocity at release. Both game DLLs
  consume it once, within 750 ms, converting to world units with the existing
  world-scale and 4.4 throw multiplier. Projectile origin remains the current
  hand with the existing start-position collision trace. Damage, fuse, bounce,
  alternate-fire impact behavior and NPC/flatscreen throwing are unchanged.
  Non-controller bindings retain a finite/time-validated legacy fallback.
- Clear gesture state for menus/console, weapons/handedness changes, death,
  backwards server time, invalid tracking, >120 ms sample gaps or implausible
  tracking jumps (>12 m/s). A held button across invalid context must release
  before a new gesture is armed. Runtime release fields append to the shared
  VR structure, not playerState or save data. No renderer/assets changes.
- Thirteen production-helper tests (142 assertions) cover 20..143 Hz input,
  on-time/late release, fast inward recovery, expired strokes, gentle toss/drop,
  disabled grace, weaker recent strokes, separate charges, invalid contexts,
  tracking jumps, time discontinuities, duplicate timestamps and sidearm/
  underhand direction. All six CTest suites pass. Both engines and game DLLs
  built/deployed with matching SHA-256; backup is
  `build-vulkan-clean/pre-thermal-grace/`.
- `vr_controller_debug 1` emits `jkxr-thermal-release` (selected/current speed,
  stroke age and assistance) and `jkxr-thermal-spawn` (captured/fallback and
  release age). Headset acceptance pending: normal and deliberately late
  releases standing and moving; release during early recovery; gentle tosses
  and deliberate drops after a pause; primary/alternate fire, both games and
  no stale throw after a menu, weapon switch or new charge. Physical comfort
  and gesture-intent classification still require user testing.

### Mounted pitch stops and activation handoff (2026-09-10)

- User confirms comfortable, accurate stick aiming, but reversal at either
  pitch stop hesitates and a standing hand reach can mount then immediately
  dismount. `/tmp/jka.log` shows actual E-Web pitch stopping at -15/+10 while
  the accumulated target continues beyond those bounds. Mount transitions also
  retain BUTTON_USE with upmove=0 and no physical jump/button press. This
  establishes held-use leakage, not an unintended jump or a posture requirement.
- Protocol correction: `CL_FinishMove` subtracts the snapshot pitch delta before
  encoding the command. Therefore mounted input holds a desired world pitch;
  server command-angle correction alone cannot constrain its accumulator.
  Player movement now publishes the actual quantized pitch limits through
  appended runtime VR fields. Clamp the stored target both before and after
  each stick step, and initialize it from actual snapshot pitch on entry.
  Existing gun limits, sensitivity, fine response and walker control are unchanged.
  These fields are not part of playerState or saved-game data.
- On mounting, release owned hand-use commands and forbid gesture rearming
  using the current mounted snapshot. Both games now require a fresh use/jump
  edge after the existing 500 ms mount delay to dismount. Entry holds and
  presses during that delay must release before reactivation; a fresh jump can
  still exit independently of held use. Reset edge state for another gun,
  another mount timestamp or backwards level time.
- Nine mounted-control unit cases (45 assertions) cover both gun pitch ranges,
  prolonged limit holds and immediate reversal, changed bounds, held use/jump,
  mount-delay presses and re-entry, in addition to stick response and timing.
  All six CTest suites pass. Both engines and game DLLs rebuilt/deployed with
  matching SHA-256; backup is `build-vulkan-clean/pre-mounted-stops/`.
  No renderer, PK3 or user configuration changes. Diagnostics now report
  `pitchTarget` and published `limits` in `jkxr-mounted-aim` records.
- User confirms standing mount/held hand, sustained pitch stops with immediate
  reversal, and deliberate dismount/re-entry all work without issues.
  Regression checks: fine aim, firing, on-foot controls
  and the accepted walker view should remain unchanged.

### Mounted-gun stick aiming (2026-09-10)

- User accepts the walker view, then requests smoother yaw/pitch control of
  fixed guns in `t2_dpred`. Scope: player `EF_LOCKED_TO_WEAPON` (emplaced guns
  and E-Webs), not AT-ST vehicles or remote camera/panel-turret control.
- The turning stick now integrates yaw/pitch independently of controller or
  headset orientation. Default maximum rates are 90/60 degrees per second,
  archived as `vr_mounted_yaw_speed` / `vr_mounted_pitch_speed` (runtime range
  1..180). Use a 0.15 per-axis deadzone and quadratic response for fine aiming.
  The selected turning stick follows existing handedness/stick-swap mapping.
  Ordinary snap/smooth body turn and walk-speed cycling do not consume this
  stick while mounted. Movement remains disabled by the existing mounted path.
- The stick must center after entry or a menu/console visit. Elapsed-time
  integration skips gaps over 100 ms. Game pitch constraints are unchanged:
  JKA E-Web -15..10 degrees, other JKA emplaced guns -35..30. The original
  reliance on command delta clamping was insufficient; see the pitch-stop
  correction above. Clear its pitch correction on local-player
  dismount before returning to absolute on-foot VR pitch. The jump-release
  transition guard also consumes a held dismount press, as for camera exit.
- Head pitch/roll and yaw relative to the entry pose affect the rendered view,
  not the weapon command. The view follows gun yaw while permitting independent
  head look. Room-scale translation uses the entry reference rather than the
  current controller/head orientation. Triggers and use/jump dismount remain
  unchanged; Force motion gestures are suppressed while using a fixed gun.
- Weapon reticle now traces the model muzzle, ignoring the occupied gun as the
  projectile does. JKA uses its muzzle direction; JKO uses player aim angles,
  matching the respective firing paths. Keep a world-space marker even when
  ordinary weapon crosshairs are disabled. No renderer or projectile changes.
- Four production-helper tests (18 assertions) cover fine response/deadzone,
  entry/UI centering, rate independence and hitch rejection. All six CTest
  suites pass. Both engines and game DLLs built/deployed with matching SHA-256;
  previous files in `build-vulkan-clean/pre-mounted-stick/`. Diagnostics use
  `vr_controller_debug 1`, one `jkxr-mounted-aim` record per second.
- Headset acceptance pending: fine/full stick yaw and pitch; neutral stick
  holds aim while head/controllers move; impact agreement with reticle; reverse
  away from pitch limits without sticking; menu and dismount/re-entry; normal
  on-foot controls, scopes and the accepted walker unchanged. Tests do not
  establish practical aiming comfort or collision behavior at each fixture.

### Drivable AT-ST view and aiming (2026-09-10)

- User reports the `t2_dpred` walker view below the cabin, between its legs,
  with no useful aiming reference. This is the legacy player transformation
  (`G_DriveATST`, `EF_IN_ATST`), not a generic `CLASS_VEHICLE` rider. Entering
  requested third person, but VR input rewrote `cg_thirdPerson` every frame
  from the generic vehicle preference. The first-person AT-ST branch in
  `PM_CheckDuck` uses height 128, versus the full hull height 248. Generic
  vehicle input also zeroed pitch unless HMD-direction control was enabled.
- Choose a dedicated third-person view for this pilot state, derived every
  frame rather than depending on unsaved camera overrides or changing the
  on-foot third-person preference. Chase offset is 240 units behind, 32 above
  the full hull height; a camera-volume trace checks the path behind the hull.
  Remove tracked head yaw from chase positioning while retaining it in view
  and aim. Capture room-scale translation on entry/exit; do not additionally
  apply the on-foot absolute headset-height correction to this chase view.
- AT-ST pilot uses HMD pitch/yaw for aiming, movement stick for travel and
  turning stick for turn. Controller roll no longer steers this walker, and
  turning does not reset the chase reference to the current head direction.
  Generic vehicle settings and controls remain unchanged for other vehicles.
- Always draw a walker weapon aiming marker, tracing the model muzzle and
  server player viewangles used by `FireWeapon`. Keep it at actual world depth,
  not a fixed screen-center overlay. AT-ST projectiles cannot select the normal
  controller-held weapon origin, including mode-transition frames. The existing
  primary/secondary barrel selection and weapon damage behavior are unchanged.
- `vr_controller_debug 1` adds one `jkxr-walker-view` sample per second with
  camera/player/muzzle origins, viewheight, third-person state and aim angles.
  Four production-helper unit cases cover height/range, head-look independence,
  stick rotation and yaw wrapping (12 assertions). All six CTest suites pass.
  JKA engine and game DLL rebuilt/deployed with matching SHA-256 checks; backup
  is `build-vulkan-clean/pre-walker-view/`. No renderer/PK3 changes.
- User confirms the walker change worked well. Regression checklist: enter
  and load a walker save; confirm elevated
  unobstructed stereo view, head tracking without chase-camera orbit, yaw/pitch
  aiming with impacts at the marker, movement/turning, both weapon modes and
  normal on-foot view/controls after exiting. Automated tests establish camera
  math, not authored-level clearance or practical aiming comfort.

### Security-camera gesture arbitration and jump release (2026-09-10)

- User reports improved camera presentation, but hand activation can cast Force
  Push and then cause an unrequested high jump. `/tmp/jka.log` shows a prior
  camera-exit A press producing upmove=127; release occurs with the menu open,
  and upmove remains latched despite both controllers reporting zero buttons.
  Later camera entries immediately exit with the stale upmove. This explains
  jumping without a new jump press, independently of the Force gesture conflict.
- Shared tracked jump input now owns press/release state across UI and camera
  transitions. UI entry releases it, and camera transitions consume an already
  held button until physical release. A dedicated synthetic key identity avoids
  releasing a separately held physical keyboard Space. Ordinary held jumps
  remain supported. `vr_controller_debug` logs `jkxr-jump-input` edges.
- JKA's usable-target reservation previously required the hand to have already
  crossed the use extension threshold. Force motion history could dispatch
  earlier. Reserve an available target before extension; refresh the query
  even with visible use hints disabled. JKO now publishes the same reservation
  from its affordance query. Disabling gesture-use does not reserve Force input.
  Force gestures are suppressed during cinematics, camera feeds and remote
  turret views. No changes to jump physics or rendering.
- Five production-state unit cases cover ordinary jump, UI-consumed release,
  held menu selection, held camera exit, and camera entry with a held button.
  All six CTest suites pass. Both engines and game DLLs rebuilt, installed and
  SHA-256 verified; previous files are in `build-vulkan-clean/pre-camera-input/`.
  Launchers sync the game DLL into the user search path on next launch.
- User confirms the reported activation and unrequested-jump issue is fixed.
  Regression checklist: slow/fast reaches at an available security
  fixture should use it without Force Push; camera switching and jump exit
  should not leave upward movement held, including an intervening menu visit.
  Verify normal jump/held jump and Force Push/Pull away from fixtures. Target
  reservation still uses the existing affordance query, not a swept hand-volume
  detector; automated state tests do not establish physical interaction accuracy.

### Security-camera monitor and controls (2026-09-09)

- User reports a disorienting head-locked security feed and unreadable/missing
  camera-switch instructions. Both cgames identify `misc_camera` by the active
  view entity. Legacy `VR_UseScreenLayer` requests a screen for these views,
  but Vulkan's world-scene guard rejected it and submitted headset projection
  poses for a view whose orientation comes from the remote camera.
- Add `RDF_SECURITY_CAMERA` (256, an unused bit, no struct-size/save change)
  only to actual `misc_camera` refdefs in JKA/JKO. Remote droids, panel turrets,
  mounted guns and cinematics do not acquire this flag merely by being remote.
  The main submitted world selects the mode; temporary portal-refdef swaps do
  not change it halfway through recording.
- Present the feed on the existing 3.2 x 2.4 m quad at 2.5 m, captured in local
  tracking space with yaw only and eye-level placement. Keep its pose while
  cycling cameras. The other swapchain supplies the black surround. Retain
  the existing last-screen handoff on exit, including the captured pose, so
  there is no intentional compositor-background frame between modes.
- World, portal, late-effects and shadow receiver passes use the same symmetric
  camera FOV, derived from the submitted horizontal FOV and the monitor's 4:3
  aspect. No headset IPD offset or HUD eye offset is applied inside the mono
  feed. OpenXR supplies binocular depth for the physical monitor itself. Head
  poses/FOVs used for compositor background submission are not overwritten.
  Normal world/scopes retain their previous projections and stereo offsets.
- Existing input: turning-stick Y beyond +/-0.8 with X within +/-0.2 drives
  `+use`; `camera_aim`/`camera_use` follows `target2` and the first-camera loop.
  Jump/upmove exits the view. Hold can repeat after the authored camera wait,
  so nudge and return to center for deliberate steps. The camera branch now
  releases `+use` even when X leaves the narrow gate and releases its owned
  use state on leaving camera mode. No changes to game camera chain scripts.
- Both camera overlays now draw two centered gold hints at virtual Y=330/354:
  `Turning stick up/down: next camera` and `Jump button: exit camera`. These
  semantic labels remain valid with handedness/stick swapping; right stick
  and A are the normal Quest defaults. Strings are currently English.
- Both engines, game DLLs and renderers built and deployed with matching
  SHA-256 checks. Launchers content-sync the game DLL to the user search path
  on the next terminal launch. No PK3 or user configuration edits. Previous
  six-file set: `build-vulkan-clean/pre-security-camera/`.
  JKA renderer `897a52b8817bf39890ca849afd0ed67a07af12d33bf0307430c7cdd8a0580332`;
  JKO renderer `daa7b1f0e58f0700695c9c648fbf5c30f0fc3f374ba4c5fef27c85c692bf5438`.
- All six CTest suites pass, including new projection aspect/invalid-FOV unit
  cases. They do not replace headset testing of the camera interaction.
  Acceptance pending: open while head is tilted, level it, translate/rotate;
  verify fixed level monitor, clear hints, camera switching without recenter,
  jump exit/re-entry, normal gameplay tracking, and no scene/effects regressions.

### Mission briefing portrait lighting (2026-09-09)

- User reports mostly white animated mission-giver faces with faint outlines.
  JKA's `ui/ingamemissionselect*.menu` uses Ghoul2 Luke/Kyle heads with
  `model_menu.skin`, not cinematic video. The supplied `/tmp/jka.log` shows
  Kyle's menu skin resolving with `unresolvedVisible=0`, a 10-degree preview
  FOV and one light in a `RDF_NOWORLDMODEL` scene.
- `Item_Model_Paint` supplies a radius-500 white light at the preview camera.
  Legacy `R_SetupEntityLighting` folds it into diffuse lighting, clamped before
  modulating the texture. Vulkan incorrectly routed these preview lights into
  the texture-free additive world receiver pass. A nearby front-facing face
  receives almost unit white illumination, saturating its texture to white.
  A representative 80-unit distance reproduces white/near-white output for
  both skin-colored and dark texels numerically; this is not a game screenshot.
- Restrict the dedicated additive model-light route to world scenes. Menu
  models retain their supplied lights through the existing clamped diffuse
  path. The Ghoul2 representative diffuse response is unchanged; this is not
  a new per-vertex lighting implementation. No world shader, global palette,
  material, skin, culling, pose, video or MD3 upload optimization was changed.
- Optional `r_vulkanModelDynamicLightAudit 1` now logs each preview model once
  (within the existing audit cap), with `receiver=clamped-diffuse`, supplied
  light count, `additiveLights=0`, and its diffuse multiplier. World receiver
  diagnostics retain separate entries.
- Both renderer targets built and all six existing CTest suites passed. These
  suites do not automate a full mission-menu visual test. Installed hashes
  match build outputs: JKA
  `590c75ed7cd2e00c839e36e24c09cd0f8206ef8b52837979ebae686c9644aaf6`, JKO
  `853ce3f4100f5db89ce59080cea963f61a2a3172db1ed5d14e432d998c517a00`.
  Previous pair: `build-vulkan-clean/pre-menu-portrait-lighting/`. Only renderer
  libraries were deployed; no menu assets, game DLLs or user configs changed.
- User confirms the portrait is fixed. Broader regression checks remain:
  repeat the affected mission briefing and switch
  speakers/missions where possible; verify skin/hair/eyes and talking motion.
  Also inspect customization/Moves previews and normal world saber lighting.
  Then resume the cross-game hologram checklist, with color profile 0.

### Yavin2 props and hidden surfaces (2026-09-07)

- `maps/yavin2.bsp` identifies `door_lever` as a `misc_model_breakable` using
  `models/map_objects/yavin/switch.md3`. The shipped model has five frames and
  two surfaces; `scripts/yavin2/open_door.ibi` sets STARTFRAME 0 and ENDFRAME 4.
  The Vulkan loader previously retained only frame-zero geometry. The door
  trigger therefore worked while its independently rendered lever stayed still.
- MD3 loading now retains frame-major positions/normals for multi-frame props.
  Rendering selects/interpolates entity frame/oldframe/backlerp and follows the
  original cap-before-wrap and invalid-pair fallback rules. UVs, indices and
  one-frame props retain their existing representation. Animated bounds include
  every pose. Existing diffuse and additive lighting consume the posed vertices.
- World and inline-BSP dynamic-light passes previously lacked the Force Sight
  visibility rejection used by their base passes. They now skip FORCESIGHT
  surfaces unless RDF_ForceSightOn is set, preventing saber lighting from
  revealing hidden tutorial markings. Light strength and ordinary receivers
  are unchanged. Both omissions predate the dependency-cleanup candidate.
- Added MD3 tests for frame selection, capping/wrapping, invalid inputs,
  interpolation endpoints, normal normalization and a five-frame lever sequence.
- User confirmed both the lever and hidden-marking fixes in headset. No game
  scripts or original asset packs changed.
- Follow-up comfort change: JKA's red `SP_INGAME_NEW_OBJECTIVE_INFO` notification
  previously appeared at HUD y=20. Move it to y=180, above the subtitle area.
  Do not change caption placement, HUD projection, font, color, lifetime or JKO's
  already-lower datapad notification. User identified the message and confirmed
  comfortable reading in the new position.

### Wedge holographic map blending (2026-09-07)

#### Matched-angle video comparison

- User provided `/dados/jkxr.mp4` (Quest) and `/dados/jkxrl.mp4` (Vulkan),
  approximately seven seconds each at 1600x1600. Both have limited-range
  bt470bg/smpte170m color tags. Frame strips show both model rotation and
  color pulsation; Quest reaches cyan/pale blue while Vulkan stays more violet.
  The runtime log confirms `authoredCull=1`, so this is the contour candidate,
  not a stale installation. A matching video duration does not synchronize
  game clocks or establish matching pulse phase.
- The Quest was connected by USB. Read-only inspection identified package
  `com.drbeef.jkxr` version 1.1.27, gamma 1.15, intensity 1.07, picmip 1,
  trilinear filtering, and vertexLight 0. Its assets1.pk3 SHA-256 is identical
  to the PC archive: `5f196826e226b1115bd46f54697334ca970c8261dd76864c1600179ff38e4c4f`.
  Its two VR asset packs contain no Wedge hologram/shader override. No headset
  files or settings were modified. Reference copies are in
  `/tmp/jkxr-hologram-reference/`.
- Inspected the exact upstream release source, commit
  `69d6bdb842ba4e6f78aa041afcf9e72e74d04694`, in a separate temporary checkout.
  Its [Android TBXR_Common.cpp](https://github.com/Team-Beef-Studios/JKXR/blob/69d6bdb842ba4e6f78aa041afcf9e72e74d04694/Projects/Android/jni/OpenJK/JKXR/android/TBXR_Common.cpp#L1490)
  explicitly selects `XR_COLOR_SPACE_REC2020_FB`. Its GLES image upload uses
  the gamma/intensity lookup tables before rendering. Vulkan's accepted current
  path uploads raw texture bytes and does not request that wide-gamut color space.
  Gamma and primaries are separate operations; changing one brightness scalar
  cannot reproduce this reference's blue/cyan pulse response.
- Added `tools/audit_hologram_video.py`: read-only ffmpeg/NumPy/Pillow diagnostic
  for these recordings. It samples the dominant blue projection color at 10 Hz,
  then compares it with the original shader's five-second palette for one through
  five overlapping faces. Tests four combinations of raw/legacy texture upload
  and sRGB/Rec.2020 primary interpretation. Uses sRGB swapchain transfer functions
  separately from a linear D65 primary conversion; see
  [W3C conversion reference](https://www.w3.org/TR/css-color-4/#color-conversion-code).
- Mean nearest-palette RGB error on the 0-255 scale:

  | Video | Raw/sRGB | Legacy upload/sRGB | Raw/Rec.2020 | Legacy upload/Rec.2020 |
  | --- | ---: | ---: | ---: | ---: |
  | Quest | 22.80 | 21.20 | 8.79 | **4.13** |
  | Vulkan | **1.83** | 15.39 | 12.86 | 25.77 |

  This is a phase-independent palette diagnostic, not perceptual error or an
  exact compositor reconstruction. It uses a fixed ROI, a gray background,
  simplified overlap counts, and clipping to sRGB. Capture compression, changing
  pose/coverage, and unknown display gamut mapping explain possible residuals.
  It strongly supports color processing as the remaining discrepancy; it does
  not prove every detail of timing or culling is now identical.
- Runtime deliberately unchanged following the comparison. Next action should
  be a separately switchable Quest color-compatibility path with explicit texture
  and output stages, initially opt-in. Do not globally change colors by default
  or add a Wedge-only arbitrary tint. Prior accepted crawl/water/Force Sense color
  adjustments must be audited before enabling a reference profile more broadly.

#### Opt-in color-profile experiment

- Four restarted comparison runs received, with ten-second recordings at
  `/dados/wedge-questcolorprofile-{0,1,2,3}.mp4`. Each corresponding
  `/tmp/jka-color-{0,1,2,3}.log` confirms the expected upload/gamut switches.
  All used picmip 0 (the captured Quest config used 1); no settings are adjusted
  automatically to conceal that remaining reference difference.
- Direct phase-independent comparison against `/dados/jkxr.mp4`, using the
  fixed hologram ROI and dominant color at 10 Hz: symmetric nearest-palette
  RMS RGB distances are 20.77 / 12.97 / 14.26 / 3.82 for modes 0 / 1 / 2 / 3.
  The new `--profile-videos` option in `tools/audit_hologram_video.py` reproduces
  this calculation. These are 8-bit color distances, not perceptual scores or
  spatial/temporal image matches. Frame strips also show mode 3 has the closest
  cyan-to-pale-blue pulse range. The comparison supports retaining mode 3 as
  the full experimental profile, NOT changing the default across both games.
- The earlier single `/tmp/jka-color-0.log` run changed the cvar in the console
  without restarting. Its comparisons were not valid because every requested
  change was latched. The four new restarted runs supersede that attempt.

- User approved a reversible compatibility experiment, not a global default
  palette replacement or a Wedge-specific tint. `r_vulkanQuestColorProfile` is
  non-archived and latched: exit/relaunch between modes. Mode 0 is the accepted
  renderer, 1 enables disk-texture preparation, 2 enables completed-image gamut
  conversion, 3 enables both. Invalid modes or unavailable legacy UNORM views
  disable the whole requested profile with a warning. Startup logs the active
  mode and both individual switches after swapchain format fallback is resolved.
- Texture bit: emulate the measured Quest 1.1.27 gamma 1.15 / intensity 1.07
  lookup on disk-loaded color images, preserving alpha. Honor the current
  `r_picmip` without changing it. Apply the lookup after the selected picmip
  reduction, then generate lower RGB mips from corrected pixels; keep retained
  higher-resolution levels corrected for Vulkan's clamp sampler too. Synthetic
  white/transparent textures, generated lightmaps, cinematic frames, histories
  and effect render targets are excluded from this upload operation.
- This first experiment is NOT complete GLES image-policy emulation: the current
  Vulkan disk-image cache shares mipmapped images between world/UI registrations,
  and that policy is unchanged. Quest's no-mip UI gamma-only uploads and generated
  lightmap preparation are not separately reproduced here. The Wedge disk
  materials use the tested mipmapped path. Retain these limitations when
  interpreting broader scene comparisons; do not hide them with per-asset tuning.
- Output bit: render into one additional full-resolution UNORM target per eye,
  then fetch each pixel without filtering. Decode sRGB transfer, convert linear
  Rec.2020 primaries to linear sRGB, clip to the available gamut, re-encode sRGB,
  preserve alpha, and write through the swapchain's UNORM view. This is an
  approximation of the Quest presentation, not a claim to reproduce its display
  or compositor gamut mapping exactly. The matrix is shared with CPU tests.
- Conversion is after world, shadows, bloom, menus/console/HUD, and Force Speed
  compositing; history capture stays before conversion to avoid repeated gamut
  conversion across temporal samples. Clear-only black frames bypass the pass.
  Existing swapchain usage flags and scene/material order are unchanged. No
  profile targets/pipeline or extra draw are created in mode 0 or texture-only
  mode 1. Output modes add one full-screen draw and one RGBA8 image per eye;
  at 3096x3243 the pair is approximately 77 MiB, excluding allocation overhead.
- Tests cover mode/fallback gating, byte lookup, alpha preservation, picmip order,
  lower-mip propagation without repeated gamma, narrow/one-pixel images, matrix
  orientation and neutral-axis preservation. Both renderer targets and the new
  fragment shader compile; all three CTest suites and Vulkan 1.3 SPIR-V validation
  passed. Both renderer binaries deployed to `/usr/lib/jkxr` and SHA-256 matched
  the build outputs: JKA `64c889df2aa169552476e60c92052592d8e64bf96b18ec7126c7ee159eaad2e4`,
  JKO `8a3c054276f91bac26f4d84e9cb90c60fbbfe7c4eef8a99b0fc967850479360a`.
  Previous pair preserved in `OpenJK/build-vulkan-clean/pre-quest-color/`.
  Headset acceptance and live Vulkan validation remain pending.
- First comparison: identical `t2_wedge` save/view, 7-10 seconds of hologram pulse
  per mode, restarting for 0/1/2/3. Keep all other settings fixed. Record pulse
  colors/contour contrast, surrounding walls, any clipping or eye disagreement.
  Then audit the preferred profile against Yavin river and temple pool, white
  armor/skin, crawl/menu colors, Force Sense, saber glow, scopes and Force Speed
  before considering broader adoption. Mode 0 remains the recovery path.

#### Wedge cutout illumination and edge-on console (2026-09-07)

- User supplied `/dados/wedge-glow.mp4` and `/dados/console-glitch.mp4` separately
  from the four color-profile captures. Address these as independent bugs, not
  by altering the profile, global palette, culling conventions or shadow settings.
- The X-braced material visible in the illumination recording matches
  `textures/vjun/grate`, referenced by the shipped `t2_wedge.bsp`. Its shader has
  an opaque alpha-tested GE192 stage with depthWrite and a depth-equal lightmap.
  The BSP dynamic-light replay used the surface texture as an alpha mask, but
  `world.vert` then added the light's XY position as UV offset and its axis-scale
  fields as turbulence. This can shift the grate's mask while the saber moves.
- BSP cutout lighting now reuses the existing model cutout receiver pipeline:
  exact-depth test, no depth write, destination-color additive response, white
  texture and no repeated alpha test. The authored base pass owns UVs, tcMods
  and coverage. Opaque BSP retains its existing additive response. Inline BSP
  gets the same policy and excludes surfaces without an opaque pass. Preserve
  cutout polygon bias while matching depth, then restore the prior unbiased
  state. No extra render pass, target, or per-light draw is added. The startup
  dynamic-light diagnostic reports `cutout-coverage=exact-depth`.
- Console cause: CPU projection divided by W, rejected any rectangle with a
  corner behind the eye, then sent that individual rectangle through a 2D
  head-locked fallback. This created the duplicated/glitched text and keyboard
  fragments when looking sharply sideways. It also interpolated glyph UVs
  affinely because every vertex reached Vulkan with W=1.
- Console rectangles now retain homogeneous XY and signed W for GPU clipping
  and perspective-correct interpolation. A valid captured pose never takes the
  head-locked fallback. Only the initial not-yet-captured pose may use that path.
  Placement, accepted size, world lock, input hit testing and animation remain
  unchanged. Capture diagnostic reports `projection=homogeneous`. Unit tests
  cover signed/zero W, a full yaw sweep, asymmetric projection parity,
  non-finite data and perspective interpolation. The user subsequently confirmed
  the illumination fix but rejected the console candidate: its background looked
  like a hinged door. See the GPU reproduction below; CPU math tests alone did
  not cover the installed shader's actual corner selection.
- Candidate verification: both renderer targets built, all three CTest suites
  passed, console vertex shader passed `spirv-val --target-env vulkan1.3`, and
  `git diff --check` passed. Deployed renderer hashes match build outputs:
  JKA `7f2f26cd6f6ea3f9a82d289e27b56ea7150105ee29f12d3443a4486a1bf36408`,
  JKO `c6fbeb228a21056361dfb612a914f78d466530799469e07900eb9414d6a0ed8d`.
  Previous pair is backed up under
  `OpenJK/build-vulkan-clean/pre-wedge-console-coverage/`. No game DLLs, PK3s,
  user configs, profile defaults, or global material/culling rules changed.

##### Console GPU corner-selection regression (2026-09-08)

- `/dados/console-glitch-2.mp4` shows large background rectangles and small
  keyboard/text rectangles disagreeing as the user turns away. A standalone
  Vulkan raster test reproduces this using the vertex SPIR-V extracted from
  the installed renderer, without a headset or game assets. A whole panel and
  the union of 96 coplanar tiles should cover the same pixels and interpolate
  identical global UVs. On the RX 7900 XTX (RADV/NAVI31), the old shader fails:
  at yaw -45 degrees, 3,610 coverage pixels disagree, including 2,488 interior
  pixels, and 3,440 UV pixels differ. Lavapipe does not reproduce this failure.
- Changing clip Z from zero to W/2 made no difference and was discarded.
  Replacing dynamic corner-array/vector indexing with explicit selection of
  each XY/W triple fixes the reproducible AMD failure. This establishes a
  driver/compiler-sensitive failure in this shader path, not a general claim
  that dynamic indexing is invalid Vulkan. Preserve homogeneous signed W,
  perspective interpolation and GPU clipping; do not restore CPU division or
  the captured-console head-locked fallback.
- `tools/check_console_gpu.cpp` uses the production vertex shader and an RG
  UV/B coverage probe fragment. It checks 292 views: full yaw sweeps, pitch,
  roll, asymmetric left/right FOV and eye offsets, and a close plane crossing
  the eye. Front/behind-eye visibility is asserted. Coverage differences are
  permitted only within one pixel of a raster boundary; interior mismatches
  fail, as do UV differences greater than two 8-bit channel levels. Both AMD
  and Lavapipe pass the replacement; the extracted old shader fails this same
  test. AMD validation-layer execution and SPIR-V validation also pass.
- Reproduce with `cmake -S OpenJK -B OpenJK/build-vulkan-clean
  -DBuildVulkanGpuTests=ON`, build `ConsoleGpuTests`, then run
  `ctest --test-dir OpenJK/build-vulkan-clean -R vulkan-console-raster
  --output-on-failure`. Use `VK_DRIVER_FILES` to select another installed ICD.
  No window/OpenXR is needed; exit 77 skips unsupported machines. The test is
  opt-in so ordinary builds do not require an accessible Vulkan device.
- Both renderer targets built and all four CTest suites passed. Installed
  hashes match build outputs: JKA
  `73d3f8a75d57a8210adfe3100dc0e45306b452a3dbf9dcc8c70cbab3959b6061`,
  JKO `7344fb7337e3d058acc9aaaec3a8fa6449f6271500697a004d28c53b0dfdd706`.
  The previous pair is in `build-vulkan-clean/pre-console-corner-selection/`.
  Only the spatial-console shader branch changed at runtime; confirmed cutout
  illumination, color profile 0 default, console size/anchor, input, ordinary
  HUD and world rendering are unchanged. No game DLLs, assets or configs were
  replaced. User subsequently confirmed that the console is working great;
  the console visual fix is accepted. Proceed to Wedge performance collection.

##### Remaining Wedge acceptance

- Baseline collection is prepared in `tools/capture_vr_performance.py`; follow
  [the run protocol](wedge-performance-run.md). This first run does not change
  renderer code, shader output, visibility or LOD. The existing phase counters
  suffice for initial bottleneck classification. Host captures and cleanup
  are automated; Envision WiVRn and headset settings remain user-controlled.
- The 2026-09-08 baseline is complete: settled outdoor/combat game submission
  rates are approximately 71/77 Hz versus 90 Hz in the hologram room. Dominant
  CPU model preparation includes uncached mapped-memory reads in MD3 prop
  lighting, not a loss of GPU character skinning. A CPU-local vertex-preparation
  candidate is built, byte-equivalence tested and deployed for repeat-route
  comparison. Measurements, exact windows, benchmark limits and deployed
  hashes are in the run protocol.
- Repeat capture on 2026-09-08 confirms the user-perceived improvement: all four
  settled sections reach approximately 90 game submissions/s. Outdoor CPU
  recording falls from 8.62 to 3.41 ms and model preparation from 5.08 to
  0.094 ms. Views/model counts differ and combat is lighter, so these are not
  exact controlled A/B speedup ratios. The room already reached 90 Hz before.
  No quality reduction or further renderer edits accompany this acceptance.
  Full intervals, workload caveats and host metrics are in the run protocol.
  WiVRn presentation/latency and experimental profile 3 costs remain separate,
  unmeasured questions, not inferred bottlenecks.
- REQUIRED FOLLOW-UP / user reminder: after the current rendering fixes AND
  the `t2_wedge` performance investigation/fixes have been verified, remind the
  user to test hologram animations throughout BOTH JKA and JKO. This is a
  milestone-triggered checklist item, not a timed reminder. Before requesting
  that run, inventory shipped map entities, material/model/video references and
  cinematic scripts to identify the holograms and their animation triggers.
  Supply exact map names, recognizable room/scene locations, how to reach or
  trigger each sequence, and what to check (pulse/color cycle, switching views,
  transparency, occlusion, stereo stability and disappearance/shutoff). Include
  Wedge's map projection and JKO's Mon Mothma sequence as confirmed cases, but
  do not present those as an exhaustive list. Distinguish asset references from
  confirmed in-level uses and explicitly flag any inventory gaps. Agree the
  color profile for the run; mode 3 is still an opt-in whole-image experiment,
  not a hologram-only default. Do not silently promote it globally.
- Inventory completed to the documented asset-scan scope on 2026-09-08; see
  [hologram animation review](hologram-animation-review.md). The cross-game
  headset review is still pending. Confirmed map uses and route/coverage gaps
  are distinguished rather than claiming an exhaustive list from shader names.

#### Earlier material and coverage corrections

- Latest comparison: the user-overwritten `/tmp/jkxr.jpg` and `/tmp/jkxrl.jpg`
  show the wall-shaped dark patch is gone, but Quest has stronger contour
  contrast. The contours pulsate. A 21-sample CPU sweep over the authored
  five-second texture-scroll cycle ranges from roughly 0.566 to 1.0 mean blue
  (two-sided, frame zero, fixed gray background). Snapshot brightness alone
  cannot establish the remaining color error; leave gamma, intensity, texture
  uploads and the two original scroll rates unchanged.
- The remaining face-selection contract was missing: the shader has no `cull`
  directive and defaults to legacy CT_FRONT_SIDED. The existing Vulkan world
  blend pipelines drew both sides. The source MD3 coverage audit reports about
  twice the overlapping fragments in that mode. With ONE/SRC_COLOR, this can
  suppress or even reverse the contrast between one and two overlapping forms.
  A regression fixture demonstrates that reversal without changing blend factors.
- Parse authored cull metadata and apply it **only to late, fully blended,
  standalone MD3s**, including their fog coverage. Preserve explicit two-sided
  materials. BSP, solid/mixed MD3s, Ghoul2, view/menu model scenes and all existing
  pipeline states remain unchanged. Precreate the small blend/cull pipeline set
  during renderer initialization; never compile those pipelines while recording
  a gameplay frame. Destroy/reset the set with the other renderer pipelines.
- Winding check: MD3 indices are unchanged. The world projection inverts Y,
  while Vulkan defines signed framebuffer area with a negative shoelace factor;
  with COUNTER_CLOCKWISE frontFace, legacy FRONT/BACK cull selection is retained.
  See [Vulkan polygon rasterization](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-polygons-basic)
  and legacy `GL_Cull` in `rd-vanilla/tr_backend.cpp`. Unit tests cover both
  windings, both authored sides, explicit two-sided, and the unchanged non-late path.
- Acceptance pending for this contour candidate: watch the same room for at
  least 10-15 seconds from a stationary position, then walk around it. Compare
  overlap contrast through bright **and** dim phases, not one still frame.
  Verify complete silhouettes, stereo agreement, no returning wall-shaped band,
  ordinary props, and accepted Yavin waters. If a consistent brightness mismatch
  remains across the cycle, examine the legacy pre-upload gamma/intensity tables
  separately; do not hide it with an arbitrary hologram brightness multiplier.
- Verification: both renderer targets and UnitTests rebuilt; all three CTest
  suites and `git diff --check` passed. Original-asset coverage at frames
  0/20/40/60 and yaw 35/125/215/305 preserved identical covered-pixel counts
  with either face selection; only overlap counts changed. Installed both
  renderers with matching build/install SHA-256: JKA `5e36746ea420c80a...`,
  JKO `256c574049bbb255...`. Previous installed pair is backed up under
  `build-vulkan-clean/pre-wedge-contours/`. Headset acceptance remains pending.

- `t2_wedge` places `models/map_objects/wedge/holo_map.md3` at (2560,512,338).
  It has 80 frames, one surface and material `models/map_objects/wedge/blue.tga`.
  The run log confirms the material and all three stages loaded successfully.
- `shaders/wedge.shader` defines a blue JPEG tint with DST_COLOR/ZERO, then
  two scrolling `power38` JPEG layers with ONE/ONE and ONE/SRC_COLOR. The last
  pair was unsupported and silently became SRC_ALPHA/ONE_MINUS_SRC_ALPHA.
  Both JPEGs have alpha=1, so that fallback erased the room and earlier layers,
  producing the flat opaque silhouette in the user's screenshot.
- Add ONE/SRC_COLOR as an explicit Vulkan blend mode in world and UI pipelines:
  RGB = source + destination * source; alpha = source.a + destination.a * source.a.
  Existing modes, depth policy, geometry, stage order and image assets stay intact.
  This corrects every authored use of that pair, not just a map-name exception.
- Share the unchanged existing blend-state construction with unit tests. Test
  every old factor tuple plus the new mode, including three-layer compositing
  with alpha=1 and different backgrounds, and the separate alpha equation.
  Registration logs `blend=one-src-color` for affected materials. The shipped
  hologram has zero UVs throughout, so its scrolling layers change sampled color
  over time; do not promise newly invented detailed scanlines or wireframes.
- Headset acceptance pending: revisit this room, observe the projection from
  several sides for 10-15 seconds, check translucent layering/animation and
  stereo stability. Check nearby power effects and ordinary room surfaces, then
  smoke-test Yavin river/temple water (their accepted materials are unchanged).
- User confirmed substantially improved blending, but `/tmp/jkxrl.jpg` shows
  a dark central region with the background wall's texture visible through it;
  `/tmp/jkxr.jpg` (Quest) has a brighter, more uniform projection. The angles and
  animation times differ, so do not tune global color from these two captures.
- A read-only coverage/compositing audit of the original MD3 reproduces neither
  the wall-textured dark band nor its severity by switching two-sided/single-sided
  rendering. `tools/audit_hologram_coverage.py` accepts the original assets1.pk3,
  optionally a frame/yaw/time and output prefix, using NumPy/Pillow only as
  development dependencies. It is an orthographic CPU diagnostic, not headset
  validation. Leave culling and shader colors unchanged for this follow-up.
- The world recorder completed *all* MD3 stages before the BSP translucent pass,
  which includes opaque walls' texture/modulation finishing stages. That ordering
  is needed for solid models' light layers below water, but a wholly blended model
  writes no depth: the later wall modulation can repaint it from behind. Split
  standalone MD3s with no effective opaque stages or explicit depth writers into the late-effects
  phase, after world finishing layers and receiver shadows. Retain ordinary MD3s,
  mixed solid/blended models, Ghoul2 characters, inline models, menu model scenes,
  world water ordering, and all blend factors on their established paths.
  Classification uses effective shaders (including entity overrides), not names.
  Apply existing model fog after the deferred model when the map has global fog.
- Added tests that early/late model selection is disjoint, with an explicit
  room-lightmap / wall-modulation / three-stage hologram composition fixture.
  The fixture demonstrates the old order's darkening and requires one late draw.
  Runtime logs `rd-vulkan-model-composite: late blended MD3 ... draws=3` for this
  hologram. This is not a general solution to sorting intersecting translucent
  objects; that remains separate from protecting effects from solid-wall passes.
- Follow-up headset acceptance pending: inspect the central structure against
  different walls, rotate around the projector, verify both eyes and animation,
  then confirm ordinary room surfaces, saber effects and Yavin water remain intact.

| Tag | Commit | Scope |
| --- | --- | --- |
| `vulkan-m1` | `59bc5ad` | First playable Vulkan renderer |
| `vulkan-m2` | `a1f01ca` | Attachments and menu scene ordering |
| `vulkan-m3` | `3384672` | Dynamic effects |
| `vulkan-m4` | `b0a8017` | Effects and VR interaction |
| `vulkan-m4-cpp17` | `82b1c19` | Verified C++17 baseline |
| `vulkan-m4-pre-m5` | `4e6287a` | Cinematic parity before shadow work |
| `vulkan-m4-yavin-parity` | `aee0e4d` | Yavin water, texture, and lightgrid parity |
| `vulkan-m4-portal-sky` | `f854ea2` | Authored portal-sky composition |
| `vulkan-m4-portal-decals` | `4961d34` | Portal depth and polygon-offset decal stability |
| `vulkan-m4-stereo-submit` | `5ea40ae` | Batched two-eye command submission |

New fixes should be committed by subsystem after focused verification rather
than accumulated into large uncommitted checkpoints. Annotated tags identify
the headset-accepted recovery points used before broader renderer work.

## Release roadmap (2026-09-06)

### Runtime/package cleanup gate (2026-09-07)

- Recovery point: `d985688`, tagged/pushed as `vulkan-m5-vegetation-credits`.
- Removed the dormant engine-owned GLX session, frame submission and action
  implementation. The Vulkan renderer remains the sole OpenXR owner. Preserved
  the existing pose conversion math, game-state/cvar initialization order,
  controller profile dispatch and renderer-routed haptic channel mapping.
- Preserved the existing 90 Hz input timing reference. Changing input timing or
  sensitivity is not part of dependency cleanup.
- Renderer interface is now version 22 after removing unused graphics imports
  and the context descriptor. Both engines and renderers must be deployed together.
- Direct CMake defaults build both SP games with Vulkan. Unsupported multiplayer
  and legacy renderer flags are rejected; old `cl_renderer` values migrate to
  the appropriate Vulkan library before loading it.
- CMake/manual/package installation agrees on `base/` for game modules. Launchers
  retain content-based home-path synchronization and the X11 desktop default,
  but respect an explicit SDL driver override. No game data, saves or user
  configuration are deleted.
- Clean Release build: `OpenJK/build-vulkan-clean`. CTest includes installer and
  ELF dependency checks through `tools/check_linux_runtime.py`. Engine ELFs link
  SDL3; renderer ELFs link Vulkan/OpenXR. No direct GL/GLX/GLU/GLES/SDL2 dependency
  or SDL_GL/glX undefined symbol is present in the six runtime binaries.
- Isolated startup tests in both games accepted the new renderer API, migrated
  old renderer settings, and exited normally with the expected error when given
  an intentionally missing OpenXR runtime. This is not a headset rendering test.
- Installed local test package `r610.d985688.cleanup1-1`, assembled from the
  working-tree build rather than remote git sources. All 16 installed files
  match the staged package byte-for-byte. Pacman removed the previously owned
  vanilla renderers and root-level game modules. Prior installed runtime is
  backed up in `OpenJK/build-vulkan-clean/pre-cleanup-installed.tar`.
- Next acceptance: one game at a time, launch, cinematics/gameplay transition,
  load a save, movement/turning, haptics, spatial console, normal quit/relaunch.
  The visual output and input response should remain unchanged.
- Remaining cleanup after this runtime gate: remove the now-unbuilt legacy
  renderer/source/SDK trees and stale auxiliary project manifests, update the
  remaining public-facing documentation, then repeat a pristine package build.
  Their source presence is not a runtime dependency and is not yet claimed gone.

Agreed order:

1. Commit, annotate and push the accepted vegetation/credits checkpoint as
   `vulkan-m5-vegetation-credits`. Yavin has full-level headset acceptance;
   cross-level and JKO vegetation validation remain ongoing.
2. Finish Vulkan-only dependency and packaging cleanup. Remove obsolete
   OpenGL/GL ES and SDL2 implementation/build dependencies, while retaining
   historical attribution and migration records where appropriate. Verify
   clean builds, runtime dependency inspection, package installation and both
   terminal/Steam launchers, including module/asset synchronization.
3. Perform additional measured x86-64 optimization rounds before release.
   Before starting this phase, ask the user for their ARM-oriented measurement
   details, offered on 2026-09-21, and agree on benchmarks/acceptance criteria.
   Measurements on x86 are not proof of ARM performance; distinguish portable
   workload improvements from architecture-specific results. This does not
   move the ARM64 port ahead of the PC release.
   Use reproducible CPU/GPU timings for crowded cinematics, demanding levels,
   dense vegetation, shadows and optional bloom. Preserve accepted visual and
   input behavior; create checkpoints between independently verified changes.
4. Merge the development branch into `main` after those gates, provisionally
   tag the release `v0.6`, and publish PC packages for wider testing. Include
   installation/upgrade instructions, runtime requirements, checksums, known
   issues and the scope of testing. Do not claim full campaign validation
   before it has occurred. Do not bundle proprietary game data.
5. Continue the user's JKA/JKO campaign playthroughs and accept external code
   contributions and bug reports. Publish subsequent fixes as `v0.6.1`,
   `v0.6.2`, and later patch releases.
6. ARM64 comes after the PC release and broad optimization, tentatively in
   `v0.7` or `v0.8`, when hardware and validation are available. Follow the port
   with ARM64-specific optimization; neither version number is a commitment.

The `main` merge and public release are later actions, not part of creating
the vegetation checkpoint. Full physical ragdolls and other optional future
features need not block the PC release unless testing exposes a release blocker.

## Renderer shader-stage contract

The authoritative behavior is the parsed Raven shader, executed in authored
stage order by `rd-vanilla`:

1. Resolve each `map`, `clampmap`, `animMap`, `videoMap`, or `$lightmap`.
2. Compute `rgbGen`, `alphaGen`, and the complete ordered `tcMod` chain.
3. Apply the stage's exact blend factors, alpha test, depth test/write, culling,
   polygon offset, and fog interaction.
4. Draw every active ordinary stage in authored order.
5. Draw `surfaceSprites` after the ordinary stages.

Splitting stages into Vulkan passes must not reorder stages whose framebuffer
dependency crosses a pass boundary. Any optimized or combined pipeline must be
mathematically equivalent to the sequence above.

There is also a batching-order difference that must remain under audit. The
legacy path combines compatible surfaces and renders stage 0 across the batch,
then stage 1, and so on. The Vulkan world path currently iterates BSP surface
batches first and completes all selected stages for each surface. These orders
are equivalent only when surfaces do not overlap and no later stage depends on
framebuffer contents produced by another surface. Do not change this globally
until a captured material trace or focused A/B proves it affects the scene.

### Rejected broad BSP experiment

Do not enable shader `cull` directives globally on static BSP batches without
first proving the winding contract for every BSP surface type. An August 2026
experiment exposed mixed winding and intentionally two-sided scenery: reversing
the assumed Vulkan face convention restored most walls but still inverted pipe
geometry in `t1_sour`. The same experiment combined dynamic stage `depthFunc`
state with hard-coded offsets for selected signs, so its remaining flicker and
performance behavior could not be attributed safely. It was rolled back as a
unit.

Revisit coplanar flicker in isolated steps: capture the exact surfaces and
authored stage sequence without changing rendering, place one candidate behavior
behind an off-by-default switch, and compare `t1_sour`, `t2_rancor`, Yavin, and a
closed combat area before making it the default. Never use a material-name list
or global face culling as the acceptance criterion.

The small black screens below the `t1_sour` towers retain a subtle flicker in
both original OpenJK and Quest JKXR. Their candidate five-stage desert display
material contains sine, square-wave, inverse-sawtooth, and scrolling additive
layers, so this is a material timing or stage-composition issue rather than
polygon-offset calibration. Headset acceptance with the current material path
reported no conspicuous flicker; keep the screen black and do not restore the
previous white flashes.

### Legacy color-space contract

Original JKXR requests an sRGB OpenXR swapchain but explicitly disables
`GL_FRAMEBUFFER_SRGB`, while ordinary textures are uploaded as unsized RGBA.
Consequently, authored texture sampling, multipass blending, and framebuffer
writes operate on the stored byte values rather than a linearized sRGB path.
The Vulkan M1 implementation instead used sRGB textures and an sRGB framebuffer
view. Opaque rendering can hide this because decode and encode approximately
cancel, but translucent multipass materials do not produce the same result.

`r_vulkanLegacyColorPipeline 1` restores the JKXR contract with UNORM ordinary
textures and a mutable UNORM render view over the runtime's sRGB swapchain. If
the OpenXR runtime rejects mutable-format swapchains, initialization falls back
to the prior sRGB-linear path and reports that decision in the log.

### Legacy texture-sampling contract

Explicit material maps are mipmapped by default in the GL renderer and use
`GL_LINEAR_MIPMAP_NEAREST`; UI and cinematic images use non-mipmapped sampling.
The initial Vulkan path uploaded only level 0 and clamped both samplers to it.
Static Vulkan images now receive a complete RGBA mip chain. The repeat/world
sampler may select those levels, while the clamp/UI sampler remains fixed at
level 0. This is especially important for animated translucent materials such
as the starting river, where minification changes both sampled color and alpha.
Focused comparison showed no visible near/far river change, however, so missing
mips were a renderer-contract defect but not the cause of its color/opacity
mismatch.

The world sampler must also use trilinear mip interpolation. Both legacy
renderers default to `GL_LINEAR_MIPMAP_LINEAR`; selecting a nearest mip level in
Vulkan keeps distant terrain transitions artificially sharp through translucent
materials even when the mip chain itself is present.

### Yavin water materials

The starting river in `yavin1.bsp` is definitively
`textures/h_evil/lakewater`. Its shader in `hiddenevil.shader` has four stages:

| Stage | Image | Blend |
| --- | --- | --- |
| 0 | `textures/h_evil/wf3` | `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA` |
| 1 | `textures/h_evil/wfn2` | `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA` |
| 2 | `textures/h_evil/waterf1` | `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA` |
| 3 | `$lightmap` | `DST_COLOR`, `ZERO` |

The temple pool is a different material,
`textures/common/Water_Yavin2`. It combines a constant-alpha base using
`ONE`, `SRC_ALPHA`, a lightmap modulation stage, and an additive stars/detail
stage. River and temple tuning must never share a material-wide alpha override.

Current status:

- Starting-river color, submerged-boundary suppression, and mipmapped surface
  motion now match the Quest reference. Temple water remains independently
  tuned and must be checked whenever river ordering changes.
- A river-only reproduction of the GL loader's `r_intensity` and software
  `r_gamma` transfer did not improve the match and coincided with a stronger
  green cast, so it was reverted. Applying one transfer in isolation while the
  rest of the frame remains on the current Vulkan color contract is not valid.
- `r_vulkanYavinRiverOpacityScale` adjusts only the three alpha-blended
  `lakewater` stages. It must multiply each sampled PNG alpha; clamping the
  stage's default `1.0` constant before texture sampling made every value above
  one a no-op. A scalar above one still did not reproduce the reference's
  low-frequency veil, so the default remains the authored `1.0`.
  `r_vulkanYavinRiverStageMask` isolates `wf3` (bit 0),
  `wfn2` (bit 1), `waterf1` (bit 2), and `$lightmap` (bit 3) so the pass that
  preserves terrain detail and introduces the green cast can be identified.
- Stage-mask testing established that the green contribution enters with the
  final BSP-lightmap stage. `r_vulkanYavinRiverLightmapGamma` therefore controls
  only that stage; it does not alter temple water or general world lighting.
- Both `yavin1` and `yavin1b` contain no global BSP fog. A trial camera-distance
  alpha approximation produced no perceptible match and was removed; river
  coverage remains a property of the authored `lakewater` texture stack.
- Reference screenshots show a low-frequency pale blue-gray extinction layer
  beneath the moving detail. It softens submerged geometry boundaries at every
  viewing distance; neither BSP contains local fog brushes that could supply
  it. A base pass before the material stack was almost entirely consumed by
  the three animated layers and then tinted by the final lightmap. Vulkan now
  applies the veil after the complete river material, where it reduces
  submerged contrast without losing the authored motion. The diagnostic mode
  proved all visible river sections use this path. `r_vulkanYavinRiverExtinction`
  controls its coverage and defaults to `0.22`. It never applies to
  `Water_Yavin2`.
- Every `lakewater` surface in `yavin1.bsp` is an `MST_PATCH`. The first Vulkan
  loader incorrectly treated the legacy `r_subdivisions 4` default as exactly
  four segments per quadratic Bezier span. In OpenJK, four is a maximum error
  in world units and subdivision is recursive. Measured river spans require up
  to 16 segments, so the fixed sampler left visibly polygonal water/rock
  intersection contours. Vulkan now derives adaptive power-of-two sampling per
  span from the legacy error test and retains the original 129-sample axis
  limit. This is geometry detail, not texture mip/LOD behavior.
- Native-resolution Quest/Vulkan pairs showed that adaptive patch tessellation
  did not visibly soften the reported submerged facets. The shoreline contour
  is already nearly identical, so tessellation is retained as general BSP
  parity but is not considered the water-compositing fix.
- Legacy draw sorting merges visible world surfaces sharing one shader into a
  single tessellation batch, then renders each material stage across that whole
  batch. Vulkan previously completed all four translucent `lakewater` stages
  for one BSP patch before advancing to its neighbor. Since alpha blending and
  destination-color lightmap modulation are order dependent, this can expose
  patch overlap/triangulation boundaries. River patches are now submitted
  stage-major as one material; color and opacity constants are unchanged.
- Temple pool: base transparency is accepted. Earlier boosts of `3.25` for the
  stars/detail and wake layers now overstate the authored effect under the
  corrected byte-space color path, so both return to `1.0` independently of
  the base transparency.
- `r_vulkanLightmapGamma` isolates the legacy software-gamma operation on BSP
  lightmaps. It defaults to the neutral `1.0`; testing the user's configured
  legacy value (`1.195938`) can establish whether the river's dark green cast
  comes from the previously omitted lightmap transfer without changing diffuse
  textures, menus, or swapchain color handling.
- `r_vulkanMaterialAudit 1` logs parsed stages and first draw selection for both
  materials. The next diagnosis must prove stage availability and execution
  before changing color or alpha.

### World texture filtering

JKXR's GLES renderer defaults `r_ext_texture_filter_anisotropic` to 16. Vulkan
previously used trilinear filtering with anisotropy disabled, causing strong
detail loss on oblique rock and ground textures even at the same headset output
resolution. The Vulkan device now enables `samplerAnisotropy` when supported
and applies up to 16x to the repeating world sampler. Clamp/UI/cinematic
sampling remains non-anisotropic, and BSP lightmaps remain single-level and
clamped as in GLES.

JKA's GLES renderer also defaults `r_picmip` to `1`; the current Linux profile
had archived `r_picmip 0`. Vulkan previously ignored the cvar regardless of its
value. Its world sampler now clamps its minimum LOD to the configured picmip,
which is equivalent to GLES discarding those leading levels without incorrectly
biasing minified surfaces. The renderer default remains `1`, but JKXR's High
Quality UI preset stores `0`; same-resolution Quest comparisons showed the
reference installation using the sharper high-quality result. Use
`+set r_picmip 0` for those comparisons.

`r_vulkanWorldDebug` isolates implicit lightmapped BSP materials at runtime:
`1` draws diffuse texture only and `2` draws BSP lightmap only. Explicit shader
materials remain intact. This distinguishes texture/UV/geometry facets from
lightmap facets without rebuilding or reloading the map; return it to `0` for
normal rendering.

Explicit shader stages now honor the legacy `detail` directive and
`r_detailtextures`. Jedi Academy's GLES renderer defaults this cvar to `0` and
removes marked stages while finalizing a shader; Vulkan previously ignored the
directive and always rendered them. This notably added a 16x-tiled `detail8`
pass to Yavin's `models/map_objects/yavin/ymix` rock material even when the
reference renderer omitted it. Jedi Outcast retains its legacy default of `1`.

BSP vertex colors are preserved from the map data, matching
`R_ColorShiftLightingBytes` under the legacy defaults. Vulkan previously added
a brightness floor to ordinary vertices and replaced very dark colors with a
value derived from each vertex normal. That made dark, vertex-lit Yavin rocks
brighter and exposed their triangle boundaries. Authored vertex alpha is also
retained instead of being forced opaque.

Model stages using `rgbGen lightingDiffuse` are distinct from ordinary vertex
color stages. Vulkan now loads the BSP `LIGHTGRID` and `LIGHTARRAY` lumps,
trilinearly samples ambient light, directed light, and direction at each model's
lighting origin, applies the legacy ambient scale and minimum light, transforms
the direction into model space, and streams diffuse vertex colors for MD3
models. Previously these stages were parsed as unlit texture stages, leaving
Yavin's standalone `rock_b.md3` boulders much brighter than the GLES scene.
`rd-vulkan-lightgrid` and the bounded `rd-vulkan-model-lighting` log records
provide runtime verification.

The same lighting rule also applies when a model surface names an image that
has no explicit shader definition. Legacy `R_FindShader` registers model images
with `LIGHTMAP_NONE` and synthesizes a one-stage `CGEN_LIGHTING_DIFFUSE`
material. Vulkan now does likewise at the model registration boundary. This is
observable on `tree09_b.md3`: its scripted leaf surfaces already requested
diffuse lighting, while the unscripted `tree09.tga` and `tree09c.tga` trunk
surfaces had previously remained fullbright. Packed MD3 normals use the legacy
256-step angular decode rather than treating byte value 255 as a duplicate of
zero.

## Dynamic and animated lighting contract

The engine submits dynamic lights between `ClearScene` and `RenderScene`.
Those lists belong to a particular world, portal, or screen-scene submission;
they must not be read from the mutable current scene after submission. The
Vulkan backend snapshots each list with its scene and swaps portal lights with
the portal refdef and entities while rendering the portal view.

Opaque BSP surfaces receive a bounded additive dynamic-light pass after their
base material. Surfaces marked `SURF_NODLIGHT` or `SURF_SKY` are excluded, and
PVS plus surface AABB/radius tests reject unrelated draws. The fragment pass
preserves an opaque stage's alpha mask, attenuates by radius and surface facing,
and does not alter translucent material ordering.

Model materials using `rgbGen lightingDiffuse` sample the BSP lightgrid for
their base ambient and directed lighting. Opaque MD3 and GLM surfaces then
receive the same spatial additive dynamic-light response as BSP geometry. Each
scene light is transformed into model space, culled against the complete model
bounds (with the submitted Ghoul2 radius as an animated fallback), and evaluated
against the rendered position and normal. This lets a saber illuminate the near
face of a large boulder or tree even when the model origin lies outside the
light radius. Opaque `lightingDiffuse` stages preserve the legacy default depth
write, then model light is replayed as a texture-free, exact-depth contribution.
Fully opaque surfaces use the same direct additive response as BSP, while
alpha-cutout receivers use destination-color modulation. This restricts light
to material pixels that survived alpha testing, prevents foliage cards or model
UVs from appearing in the glow, and keeps solid props at a brightness consistent
with adjacent BSP. The old coarse dynamic tint is omitted while this pass is
active so a light is not counted twice;
`r_vulkanModelDynamicLights 0` restores that fallback.

Applying weighted, bone-transformed base lighting to every Ghoul2 vertex raised
CPU command-recording time to 40-70 ms in populated scenes while GPU stereo work
remained 2-5 ms, and produced no visible benefit in the reference scenes.
Animated Ghoul2 surfaces therefore retain the verified position-only skin
stream and a per-entity hemispherical lightgrid tint, while the additive pass
uses their already-skinned position and normal stream. Disintegrating entities
temporarily retain the legacy dynamic tint because their vertex visibility
changes during the effect. `r_vulkanModelDynamicLightAudit 1` logs each bounded
model receiver once per level, and `r_vulkanTiming 1` reports the resulting
`model-dlight-draws` separately.

The optional `r_vulkanBloom 1` path keeps authored emissive geometry separate
from receiver lighting. It renders depth-tested saber, beam, flare, and other
recognized glow sources into a full-resolution per-eye target, then builds
half-, quarter-, and eighth-resolution separable blur bands. The tight band
retains the full single-scale contribution while progressively softer medium
and broad bands add lower-intensity energy; normalizing their weights would
incorrectly attenuate a source that each downsample has already spread. They
are screen-composited after world lighting and shadows but before HUD, scope,
and Force Speed processing. This preserves sharp authored cores and
solid-geometry occlusion while adding a multi-scale aura;
`r_vulkanBloomIntensity` controls its strength and `r_vulkanBloomRadius` its
spread. `Energy Bloom` exposes the archived `r_vulkanBloom` switch in both
games' startup and in-game More Video menus and defaults to Off for fresh
profiles. Off skips the source, six blur, and three composite draws entirely;
recognized authored energy effects receive an 18% geometry-radius increase to
retain a modest halo without offscreen processing.

Animated BSP lighting may author up to four independent lightmap or vertex
color styles per surface. Vulkan retains their handles and style metadata and
applies the current packed `SetLightStyle` RGBA value to the primary slot.
Secondary slots require independent UV/color attributes. An initial
implementation appended those attributes to the shared vertex type, expanding
every animated-model vertex from 56 to 80 bytes and materially regressing
crowded scenes. Secondary composition is therefore deferred until it has a
BSP-only attribute stream; animated models must keep the compact format.
Bounded `rd-vulkan-lighting` messages confirm dynamic world draws and the first
light-style updates. On world load, `rd-vulkan-lightstyles` inventories all
authored style slots even when secondary composition is deferred. Yavin, Hoth,
and `t1_sour` use only style 0. `kor1`, `kor2`, `t1_fatal`, and `t3_bounty`
contain secondary or custom styles and are future acceptance maps for the
dedicated stream.

Acceptance test:

1. A saber, projectile, explosion, or other submitted light produces a smooth
   colored radial contribution on nearby BSP and `lightingDiffuse` models,
   without duplicating the material texture or becoming an eye-filling quad.
   In `yavin1`, verify the large dark boulder behind the gameplay spawn and the
   nearby tree trunks and alpha-tested leaves respond locally as the saber
   approaches them; distant parts of each model must remain unchanged.
2. Verify primary light-style updates do not alter unrelated surfaces. Once the
   BSP-only secondary stream lands, load `kor1`, `kor2`, or `t1_fatal` and
   verify the authored effects animate identically in both eyes.
3. Portal sky, Yavin river and temple water, `t1_sour` decals, menus, and
   cinematics retain their verified composition.

This checkpoint deliberately does not include stencil shadows, translucent
shadow masks, soft-shadow blur, or the later legacy glow/bloom target.

### Timing protocol

`r_vulkanTiming 1` enables renderer-local timing without changing render
behavior. Every 120 successful stereo frames, `rd-vulkan-timing` reports
average/maximum CPU command-recording time, queue submit/wait time, total GPU
time bracketed across both eye command buffers, active scene-light count, and
opaque stereo model candidate/culled/draw counts.
The accompanying `rd-vulkan-phases` line separates stereo CPU recording into
sky, static BSP, dynamic-light, surface-sprite/weather, model, and dynamic-effect
work, and reports the static BSP stage-draw count. This breakdown includes
authored portal-sky passes and is intended to distinguish open-level BSP costs
from crowded animated-model costs.
`rd-vulkan-model-phases` further divides model work into culling, bone
evaluation, CPU vertex skinning, Vulkan submission, and unclassified setup.
The ranked `rd-vulkan-skin-model` lines report cache hits, newly skinned
surfaces, vertices, and elapsed time for the eight most expensive models.
GPU timestamps are optional: if the selected graphics queue does not expose
them, the report explicitly falls back to CPU-only timing. Disable the cvar for
ordinary play because query collection is diagnostic instrumentation, not the
external end-to-end frame-pacing measurement.

The Vulkan backend rejects ordinary model entities whose conservative local
bounds are entirely outside an eye's left, right, top, or bottom clip planes
before Ghoul2 bone evaluation and surface recording. It deliberately does not
apply near/far rejection, and first-person/depth-hacked models bypass it.
`r_vulkanModelCull 0` disables this optimization for an A/B if a model is
suspected of disappearing at an edge; the default is `1`.

Static root-BSP opaque and global-fog submission uses grouped indexed indirect
draws when the Vulkan device exposes `multiDrawIndirect`. Groups preserve exact
shader, lightmap/style, surface-flag, and vertex-lighting state; per-surface PVS
and Force Sense visibility is represented by each indirect command's instance
count. Translucent materials, inline models, and the Yavin river's stage-major
ordering remain on their established direct paths. The initial `t1_rail`
profile motivating this path measured roughly 9 ms of CPU recording for about
32,000 stereo BSP stage draws while total stereo GPU time remained near 7-8 ms.
After indirect submission, BSP recording fell to roughly 3-4 ms and GPU stereo
time remained about 4-6 ms. Looking backward from the moving train exposed
roughly 180 models and raised model work to about 40 ms, of which 35-39 ms was
CPU vertex skinning. GLM bone indices and normalized weights are therefore
decoded once at model load instead of being unpacked again for every visible
vertex on every frame; pose evaluation, surface selection, and material output
remain unchanged.

### Measured no-shadow baseline

The pre-shadow baseline was captured on 2026-08-31 with a Quest 3 connected
through the Envision WiVRn build. WiVRn rendered at 150% (3096x3243), 50%
foveation, 140 Mbit/s, normal supersampling and sharpening, fixed 90 Hz, and no
spacewarp. The game ran with `r_vulkanTiming 1`; `pidstat` and `amdgpu_top`
sampled the host once per second. The reported effective rate is derived from
the wall-clock interval between each 120-frame renderer report. It is not a
headset compositor or delivered-frame measurement.

| Game and scene | Effective frames/s | CPU record | CPU skin | Stereo GPU |
| --- | ---: | ---: | ---: | ---: |
| JKA crowded `yavin1` ship cinematic | 22.1 | 39.6 ms | 38.0 ms | 2.3 ms |
| JKA `t1_rail`, looking backward | 41.0 | 18.3 ms | 11.0 ms | 5.3 ms |
| JKA `t1_rail`, looking forward | 53.7 | 13.5 ms | 6.6 ms | 4.9 ms |
| JKA `t1_rail`, looking sideways | 62.4 | 11.2 ms | 4.3 ms | 4.4 ms |
| JKA `t1_fatal` combat | 48.4 | 16.4 ms | 14.3 ms | 3.1 ms |
| JKO Mon Mothma cinematic | 68.7 | 13.16 ms | 12.75 ms | 0.79 ms |
| JKO `kejim_post` gameplay | 35.5 | 24.72 ms | 22.60 ms | 2.27 ms |
| JKO `ns_streets` gameplay | 75.1 | 7.35 ms | 5.61 ms | 2.45 ms |

JKA's crowded ship process consumed about 93% of one CPU core while total GFX
activity averaged about 21%. JKO showed the same imbalance: the Mon Mothma
cinematic used 93% of one core with 21% total GFX activity, and `kejim_post`
used 90% of one core with 24% total GFX activity. WiVRn media-engine activity
held near 74% throughout both games. JKO `ns_streets` rose to 33% total GFX
activity but needed only 57% of one CPU core on average. The hottest sampled
GPU-junction temperature was 70 C.

The dominant limit is CPU Ghoul2 vertex skinning, not Vulkan execution or GPU
fill. The stable Mon Mothma view is especially diagnostic: Kyle and Jan alone
consume about 12.75 ms of CPU skinning while their complete stereo GPU work is
under 0.8 ms. Apparent GPU headroom therefore does not justify an expensive
per-caster CPU submission path.

Shadow implementation is subject to these gates:

- `r_vulkanShadows 0` must preserve the verified no-shadow command path and
  stay within 3% of the corresponding baseline effective rate.
- Camera-independent shadow data is generated once per stereo frame and reused
  by both eyes. No shadow-map pass may be duplicated per eye.
- Animated casters reuse the frame's decoded pose and skinned vertices. Shadow
  collection must not invoke a second Ghoul2 skinning pass.
- At the initial quality level, shadow CPU recording may add at most 0.35 ms on
  average and 0.75 ms to a 120-frame report maximum. Stereo GPU time may add at
  most 1.5 ms on average and 2.0 ms to a report maximum in `t1_rail`.
- Shadow timing is reported as dedicated CPU collection/recording and GPU-map
  and filtered-mask phases so regressions cannot hide inside general model time.
- Resolution, caster/light limits, filtering, and a complete off switch are
  runtime cvars. Allocation and pipeline creation happen outside ordinary
  frame recording.

The OpenGL Ultra implementation is a visual reference, not the Vulkan design.
It constructs silhouette edges on the CPU, extrudes stencil volumes, copies the
eye color, and builds a blurred screen-space mask through a mip chain. Vulkan
instead uses a programmable, stereo-shared light-space depth map. Animated
caster draws bind the same skinned vertex ranges already prepared for ordinary
model rendering; no silhouette topology and no second deformation are built.
Each eye reconstructs receivers from its depth buffer into a reduced-size
mask. Softness is applied while sampling the light-space depth map, before the
translucent composite. This avoids blurring shadows across unrelated receiver
surfaces while keeping filter cost independent of caster count.

The first gated increment implements only map creation and submission. It is
off by default through `r_vulkanShadows 0`; enabling it with `cg_shadows 2` or
higher records the shared map but deliberately does not alter scene color yet.
`r_vulkanShadowMapSize`, `r_vulkanShadowCasterLimit`, and
`r_vulkanShadowDistance` bound its memory and work. The
`rd-vulkan-shadow-timing` report must show near-zero cache misses before the
receiver-mask stage is allowed to land.

The Quest 3/WiVRn map-only acceptance run reached zero cache misses in every
reported sample after adding the selected-caster compute preparation pass. The
crowded JKA ship recorded 12 casters and about 185 draws with roughly 0.30 ms
CPU preparation, 0.30 ms CPU map recording, and 0.07 ms GPU map time. Its
largest sampled crowd used 18 casters and 297 draws at comparable cost. This
passes the cache and recording gates and permits work on the visible receiver
mask without changing the established map architecture.

The first visible receiver increment preserves eye depth only while shadows are
enabled, reconstructs world positions into one half-resolution `R8_UNORM` mask
per eye, and composites black at a restrained default opacity of `0.32` before
HUD, subtitles, scopes, menus, and other screen-space rectangles. The
`r_vulkanShadowOpacity` cvar permits live tuning from zero through `0.8`.
Filtering is deliberately disabled for this gate so projection, depth bias,
stereo stability, and raw mask cost can be judged before softness can hide an
error. `rd-vulkan-shadow-timing` reports the combined GPU mask cost for both
eyes separately from the stereo-shared map cost.

The first stable-light experiment used shader `sun`, `q3map_sun`, and
`q3map_sunExt` directives, with the normalized legacy renderer default
`(0.45, 0.30, 0.90)` as fallback. Anchoring that direction independently of
the HMD removed the severe cinematic flicker, but broader testing rejected it
as the final lighting model: indoor actor shadows did not follow their local
authored lights. The legacy renderer already samples that information from the
BSP light grid for each entity.

Quest 3/WiVRn acceptance confirmed that the fixed light basis keeps direction
consistent through the crowded JKA ship cinematic and remains stable under
HMD motion, scripted camera movement, and turn-stick yaw. Animated shadows
track their casters, ordinary rendering and performance remain intact, and the
only observed defect in the raw mask was minor hard-edge aliasing.

The next quality pass retains each caster's world-space light-grid direction
before model lighting converts it to local space. A temporally stabilized
consensus of nearby unique actors drives the shared map; map sun remains only
the no-caster fallback. Dynamic lights are deliberately excluded from this
direction consensus so a moving saber or projectile cannot rotate every
character shadow. As in the legacy projection, only each sample's horizontal
bearing is retained: it is normalized and combined with a fixed vertical
component at a `0.3:1.0` ratio. Feeding raw light-grid elevation into a
directional map was rejected because near-horizontal indoor samples produced
implausibly long shadows. The orthographic footprint is fitted around the
selected casters and snapped to shadow texels instead of always spanning the
full collection distance. This preserves the bounded, stereo-shared
architecture while materially increasing effective map resolution in ordinary
rooms and cinematics.

The first filtered quality step specializes the receiver pipeline at creation:
mode `0` retains the accepted single depth comparison, while mode `1` initially
used a stable 3x3, 1-2-1 tent PCF in light space. After the fitted-map pass,
mode `1` uses a 5x5, 1-4-6-4-1 tent to suppress the remaining staircase edges
without adding another render target. `r_vulkanShadowFilter` selects the
mode and defaults to `1`; setting it to `0` provides an immediate raw-mask A/B
without rebuilding resources. The existing `gpu-mask` timing includes the PCF
cost. Quest 3/WiVRn acceptance confirmed visibly softer edges, stable direction
and projection, attached animated shadows, no cross-surface dark halos, and no
perceptible performance regression. At 3096x3243 per eye, the filtered mask
typically measured about 0.13-0.15 ms for both eyes, compared with roughly
0.08 ms for the raw mask; the shared map remained around 0.03-0.07 ms.

Receiver shadowing must finish before late non-depth-writing dynamic effects
and weather. In particular, saber glow, beams, flares, and particles are light
sources or emissive effects and must never be darkened by the shadow composite.
HUD, subtitles, scopes, menus, and other screen-space rectangles remain later
still.

Authored Yavin water is also a late receiver overlay. Its translucent stages
are withheld from the scene-depth pass and replayed after shadow composition,
before emissive dynamic effects. Character shadows therefore darken the solid
pool floor and are then covered by the water layer, matching the legacy visual
ordering without moving unrelated model or world material stages.

A single light-grid consensus cannot reproduce the legacy renderer's distinct
per-actor shadow bearings when nearby samples point in opposing directions;
their horizontal components can cancel into an almost vertical shared map.
`r_vulkanShadowAudit 1` records each unique caster's model, origin, raw sample,
bounded projection direction, direction group, and the resulting consensus
once per loaded map. The `academy1` audit demonstrated the failure directly:
Luke's useful `(0.010, -0.287, 0.958)` bounded direction was averaged with 17
surrounding actors into the nearly vertical `(-0.007, -0.038, 0.999)` result.

The grouped quality increment assigns actors to four stable world-space bearing
sectors and averages only compatible directions. Each occupied sector renders
to one layer of a stereo-shared depth image using its own fitted, texel-snapped
matrix. Per-eye receiver draws accumulate those layers into one mask with a
maximum blend, so overlaps do not multiply shadow opacity. Group identity never
depends on either eye or the camera, animated casters still reuse the single
compute preparation pass, and no actor/model-name special cases are involved.

Quest 3/WiVRn acceptance confirmed distinct, plausible bearings for Luke and
the surrounding Padawans, stable animated shadows, maximum-blended group
overlaps, correct pool-water and saber-glow ordering, and no perceptible
performance regression. The next isolated quality mode is selected with
`r_vulkanShadowFilter 2`. It samples a deterministic 32-point Vogel disk in
light space and derives its radius from the fitted orthographic matrix, keeping
the penumbra near two world units rather than making softness depend on map
footprint. Modes `0` and `1` retain the accepted raw and 5x5 paths for A/B
diagnosis. The disk is camera-independent and runs before composition, so its
result must remain binocularly stable and cannot blur across unrelated receiver
surfaces.

Quest 3/WiVRn acceptance of mode `2` found the softened edges clean, stable,
free of visible sampling rings or grain, correctly ordered with Yavin water and
saber glow, and without a perceptible performance loss. Direction groups and
their maximum-blended overlaps remained correct. White-armored character
response is deliberately evaluated after this baseline; no model-specific
attenuation belongs in the filter itself.

Stormtroopers and snowtroopers intentionally retain their normal geometry in
the shared shadow maps, so their cast silhouettes and ground shadows are
identical to other actors. Their cgame submissions instead carry the semantic
`RF_LIGHT_SHADOW_RECEIVER` flag. After scene depth is complete, Vulkan reuses
the frame's cached skinned vertices to mark visible flagged pixels in a
full-resolution one-channel receiver-response target. The final composite
attenuates only the added projected shadow at those pixels; it does not replace
authored model lighting, inspect texture names, or brighten unrelated pale
materials. `r_vulkanShadowWhiteArmorScale` controls the remaining projected
shadow response and defaults to the headset-tested `0.1`; `1.0` is the exact
unattenuated A/B.
The response pass is included in the existing `gpu-mask` timing interval.

Final Quest 3/WiVRn JKO acceptance covered the Mon Mothma cinematic,
`kejim_post`, `ns_streets`, and the Desann encounter. Shadows remained stable
and binocularly consistent, white armor retained its intended response, and
holograms, saber glow, cinematics, and other effects preserved their ordering.
Across 398 timing reports, animated shadow submissions had zero skinned-cache
misses. The largest reported CPU mask-record cost was `0.082 ms`; GPU map and
stereo mask peaks were `0.124 ms` and `1.196 ms`, respectively. A matching
`r_vulkanShadows 0` control emitted no shadow-map, mask, or semantic-response
work. These results close the initial Vulkan soft-shadow milestone for both
games.

The first shadow acceptance loop must repeat the crowded JKA ship, all three
`t1_rail` view directions, `t1_fatal`, the JKO Mon Mothma cinematic,
`kejim_post`, and `ns_streets` with shadows off and on. Visual acceptance alone
is insufficient if any budget above is exceeded.

## Scoped aiming contract

Weapon traces in scope mode follow the stabilized headset/weapon forward axis.
The scope reticle must therefore be projected at that same optical direction in
each asymmetric OpenXR eye projection. It must not inherit `cg_hudStereo`,
which intentionally places the ordinary HUD at a finite binocular depth and
causes the reticle to disagree with the shot ray. Vulkan detects both the E-11
scope artwork and the Tenloss overlay and applies the exact projection-center
offset derived from each eye's tangent FOV.

Acceptance test: aim the E-11 and Tenloss center marks at a small surface point
at medium and long range, fire several shots, change Tenloss zoom, and exit each
scope. The impact axis, scope fusion, circular Tenloss mask, and post-scope
weapon state must all remain correct.

## VR interaction contract

The usable hint and activation must test the same source. With a gesture held,
that source is the corresponding tracked hand; without a gesture, the primary
thumbstick use follows the headset/player view ray. Extended hands also perform
a latched bounds-contact check so moving onto a fixture after crossing the
gesture boundary can activate it once. The latch clears when the hand leaves
the target or the gesture ends, preventing a multi-use script from firing every
frame. Successful activation retains the controller haptic response.

An advertised use target owns the reaching gesture before Force Push/Pull is
resolved. This prevents one extension from both using a fixture and emitting a
Force power. One-shot `misc_model_breakable` fixtures stop advertising success
after their use script clears `BSET_USE`; touching or directly using the spent
fixture produces the stock panel-failure sound instead of silently doing
nothing. The physical target continues to own the gesture in that unavailable
state, so rejection feedback cannot misfire as Force Push.

The bomb fixtures and usable E-Web in `t1_fatal` are the current acceptance
pair. Model-specific `rd-vulkan-eweb-audit` lines list every E-Web surface,
parent, effective hide flags, shader, and draw state. This distinguishes the
authored destroyed state from a Vulkan multipart-model failure without changing
the Ghoul2 `NODESCENDANTS` behavior shared by character dismemberment.

The fixture interaction contract was hardware-verified on 2026-08-25: reaching
into an available fixture activates it once without emitting Force Push, and a
second reach after deactivation produces the rejection sound.

The E-Web is a static seven-bone Ghoul2 model, not a ragdoll. On destruction,
the game applies `NODESCENDANTS` to `eweb_damage`; the complete root surface
must remain because its `cannon_Yrot` geometry includes the spindle joining the
turntable to the tripod. Filtering that bone leaves the root base visibly
disconnected after destruction.
`r_vulkanEwebCull` isolates its face-orientation discrepancy: `0` draws
two-sided, `1` culls back faces, and the default `2` culls front faces. Only
this model's opaque stages use the selected pipeline. Two-sided destroyed
rendering did not remove the displaced remnant, ruling out face culling as its
cause. Destruction now explicitly freezes `model_root` at frame zero because
resetting `s.frame` alone does not clear a Ghoul2 firing/recoil override.
The cgame rider path also refuses to restart recoil on an E-Web whose health
has already reached zero.
`rd-vulkan-eweb-state` logs every surface's effective flags and complete bone
animation state so this reset can be verified independently of appearance.

Acceptance test: activate one `t1_fatal` bomb once, then withdraw and extend the
off hand over it again. The first action runs the script with haptic feedback;
the spent fixture no longer shows the use hint, the second attempt emits the
failure cue, and neither attempt emits Force Push. Force Push must still work
normally after moving the hand away from the fixture.

## VR movement contract

The movement path is:

`OpenXR action -> normalized stick -> usercmd -> ClientThink/Pmove ->`
`friction/acceleration -> PM_StepSlideMove -> collision trace -> player origin`

The stick vector controls desired direction and proportional speed. Full axial
or diagonal deflection must remain sustained without entering a low-speed state.
Diagonal magnitude is normalized by the original movement code; controller code
must not introduce a second nonlinear clamp.

Evidence from the 2026-08-20 log:

- Full input reaches `Pmove` unchanged during the crawl.
- A failing sample retained about 287 units/s horizontal velocity while producing
  zero displacement.
- Neither the ground nor slide `allsolid` branch fired.
- `pm.numtouch == 0` does not exclude a world collision because
  `PM_AddTouchEnt` intentionally omits `ENTITYNUM_WORLD`.

Therefore the controller normalization and `allsolid` recovery are not the
current root cause. A later focused run found no persistent crawl. Its sole
brief anomaly was a physical wedge against two NPC entities: perpendicular
collision planes caused the stock triple-plane stop, and movement resumed when
the actors moved. The movement audit remains available for another regression
round but no further movement behavior change is currently justified.

The August 27 sustained reproduction superseded the earlier samples. Values
read directly after `xrGetActionStateVector2f` fell from a near-full diagonal to
roughly 0.1-0.3 magnitude while the physical stick remained held. Filtering,
`usercmd_t`, simulation, prediction, and the final rendered view all followed
that attenuated value correctly. The crawl therefore originates at the OpenXR
action-state boundary rather than collision or camera composition.

The locomotion conditioner arms only after magnitude reaches 0.82. If that same
direction then collapses below 0.58 without first crossing center, it preserves
the outer magnitude until the raw signal recovers or the stick is centered for
75 ms. Partial movement beginning from center remains fully proportional, menu
and turning sticks bypass the conditioner, and
`vr_openxr_stick_dropout_guard 0` provides an immediate A/B. Debug transitions
use the `jkxr-stick-dropout` prefix.

Acceptance test: sustained full forward, left/right strafe, and both forward
diagonals for at least 30 seconds each, including turning with the right stick.
Partial deflection must remain proportional.

Smooth turning preserves the original 72 Hz angular response but integrates it
using elapsed milliseconds. The turn rate must therefore remain responsive and
consistent when renderer performance or headset refresh changes; rebuilding the
engine must not restore the legacy per-input-frame increment.

## Tracked-saber damage contract

The tracked controller supplies the physical blade base, direction, previous
pose, and swing velocity. The game then performs swept saber traces, resolves
Ghoul2 and world collision, accumulates victims and damage, applies saber stop
fractions, calls `G_Damage`, and finally invokes NPC pain/death behavior.

The thrown saber uses a separate path and is a regression guard, not evidence
that the direct tracked-blade path works.

### Howlers

Comparison with original JKXR and flatscreen JKA established that direct saber
damage is intentionally suppressed while the player is in
`BOTH_SONICPAIN_START/HOLD/END`: the third-person character covers both ears and
cannot swing. This is not a Ghoul2 failure. In VR, however, tracked hands remain
free and visibly cross the target, while a thrown saber already causes damage.
The legacy animation gate therefore creates a control/render mismatch.

The VR adaptation preserves the original rule for flatscreen and NPC attacks,
but restores normal saber damage when the local player's physical
velocity-triggered swing occurs during sonic pain. The earlier speculative
howler-bounds retry and stop-fraction exception were removed. Diagnostics record:

- nearest howler distance from the physical blade segment;
- howler animation, timers, health, and AI state;
- accumulated victim damage and saber stop fraction;
- health immediately before and after `G_Damage`.

Acceptance test: direct slow and fast horizontal/vertical swings during the
howl, ordinary howler movement, a scripted tree, and an ordinary NPC. Thrown
saber behavior must remain unchanged.

## E-Web wreck diagnostic

The intact E-Web and its destroyed wreck share
`models/map_objects/hoth/eweb_model.glm`. Destruction turns off the
`eweb_damage` surface with `G2SURFACEFLAG_NODESCENDANTS`; the surviving
`eweb_cannon` surface contains the tripod and several independently connected
mesh components.

The following hypotheses have been tested without changing the floating wreck
part and are therefore rejected:

- front-, back-, and two-sided culling selection;
- failure to store the `eweb_damage` surface override in Ghoul2 state;
- accidental use of a different GLM LOD: the E-Web asset contains exactly one.
- deleting root-surface components according to their dominant base/swivel
  bone; that experiment removed valid support geometry and was reverted.
- freezing `model_root` at frame zero and suppressing dead-gun rider updates;
  the hardware result was unchanged, so both changes were reverted.

The captured transition confirms that `eweb_damage` and its `eweb_alpha` child
are hidden after destruction while `eweb_cannon` survives. The remaining mesh
is split between the base, swivel, and three tripod bones. Vulkan now skins
normals with the same weighted bone transforms as the legacy Ghoul2 path; it
previously transformed positions but left normals in bind-pose space.

Temporary connected-component and surface-color instrumentation distinguished
authored wreck geometry from an incorrect bone transform or a second model
submission. It was removed after the renderer fault was identified, so normal
builds carry no E-Web-specific vertex coloring, component graph, or transition
logging.

The August 27 synchronized capture disproved the debris hypothesis. Generic MD3
chunks move, fade, and stop being submitted at their authored four-second
lifetime, while the reported object remains. The component experiment also
showed that the surviving root surface is the authored tripod wreck rather than
a duplicate upper cannon: several connected leg/support pieces span multiple
bone regions and cannot be removed by dominant-bone labels.

The August 29 hardware run disproved the root-recoil hypothesis. It logged
`model_root` frozen at `frames=0..1`, `speed=0`, and the reported assembly was
visually unchanged. Inspection of the seven-bone GLA also showed that frames
zero through two are identical and the later recoil frame only changes
`cannon_Xrot`, while the suspicious root components are primarily weighted to
`base` and `cannon_Yrot`.

The surface-color capture identified the renderer fault. Intact
`eweb_cannon`, `eweb_damage`, and `eweb_alpha` submissions appeared green,
magenta, and cyan respectively, but the post-destruction floating assembly was
normally textured. It therefore was not one of those submitted surfaces. On a
pass where destruction hid every translucent Ghoul2 surface, Vulkan treated
zero draws as an unhandled entity and fell through to the static `hModel` path.
That path redrew default bind-pose geometry. A valid supported Ghoul2 hierarchy
is now authoritative even when all of its surfaces are intentionally hidden in
the current pass; only entities without a handled Ghoul2 model may use the
static fallback.

Acceptance test: inspect and destroy the E-Web in `t1_fatal`, then wait at least
ten seconds. The intact model must remain unchanged; after destruction the
tripod wreck and ordinary short-lived debris remain, but the detached upper
cannon assembly must not persist.

Most `hoth2` E-Webs are map-authored with the invulnerable spawn flag and are
not destruction tests. Only `eweb2` at `(3154, -1220, 1042)` is vulnerable; it
has 500 health rather than the default 250.

## Movement pipeline diagnostic

Movement evidence must distinguish three different cases:

- raw OpenXR stick loss before filtering;
- filtered movement failing to reach a newly built `usercmd_t`;
- a correct command being clipped by game collision or movement state.

`jkxr-stick-pipeline` reports the first mismatch, `jkxr-movement-pipeline`
reports the second, and `jkxr-movement-debug` plus `jkxr-movement-trace` cover
the third. The controller summary labels its command as `prevCmd` because input
processing runs before the next command is built; comparing that old command
to the current stick produced false one-frame mismatch diagnoses.

The two low-speed traces at `(14831.87, -40.03, 440)` in the latest log belong
to `t1_rail`, not `t1_fatal`: full commands reached movement but hit two
perpendicular world planes. They are a collision-corner stop and not evidence
of the original analog crawl lock. The reported `t1_fatal` events require the
new synchronized pipeline diagnostics before their cause can be classified.

The August 29 `t1_fatal` capture identified a separate sustained input failure.
The left stick changed from `(0.077, 0.997)` to `(0.051, 0.402)`, then OpenXR
reported exact zero while the user continued holding it. The old compensator
discarded its latch after 75 ms at center, making the later zero command
indistinguishable from a release. The conditioner now only enters compensation
after an abrupt outer-to-attenuated transition and preserves the latched vector
through zero samples while the controller's thumbstick-touch action remains
active. A touch release, direction change, selector activation, or disabled
movement clears the state. Debug summaries include both controllers' touch
bitfields.

The following hardware capture reported another crawl, but it did not reproduce
that input failure. Full conditioned movement reached full-strength
`usercmd_t`s and acceleration. Every low-speed interval was stopped by world
collision, including pairs of perpendicular planes at exact brush boundaries
such as `(-1296.125, 496.125)`. This is collision-corner trapping, not loss in
the OpenXR, response-curve, or command stages. Future locomotion cleanup should
separate OpenXR acquisition, dropout conditioning, response mapping, and
`usercmd_t` projection into replayable stages. It must retain the original
`pmove` behavior for stairs, slopes, water, wall moves, knockback, and vehicles;
replacing that simulation cannot repair a correctly diagnosed input dropout.

## Force gesture sampling contract

The original Force Push/Pull gesture required one controller update above
`vr_force_velocity_trigger` followed by another update below it. A fast gesture
could begin and end between rendered input samples, so slowing the arm made it
more reliable as frame rate fell.

The primary velocity latch remains, but it is now Force-specific rather than
sharing the saber/melee attack latch. A separate 32-sample timestamped radial
history covers up to 320 ms and is evaluated while the hand is moving, instead
of waiting for velocity to fall and looking back only five rendered samples.
A successful dispatch has a 250 ms cooldown, during which stale motion is
discarded, and an active world-use target owns the off-hand gesture. With
`vr_controller_debug 1`, accepted and rejected candidates log their source,
radial delta, sampled speed, palm direction, history size, and displacement age.

Acceptance test: perform deliberately slow and deliberately fast Push and Pull
gestures against enemies at both good and poor frame rates. Each physical gesture
must trigger at most once; interacting with a fixture must not emit a Force
gesture. Insufficient Force energy feedback remains deferred.

## Controlled recovery protocol

1. Preserve the current dirty tree; do not destructively reset it.
2. Build `vulkan-m4-pre-m5` in a detached worktree as a renderer-only A/B
   baseline for the river.
3. Run one instrumented current build to capture water stage selection,
   step/slide decisions during a crawl, and the howler damage lifecycle.
4. Compare evidence with the legacy renderer and the detached checkpoint.
5. Make one subsystem fix at a time, remove temporary high-volume diagnostics,
   verify its focused matrix, commit, and tag meaningful known-good milestones.

The active Linux launcher loads the engine and renderer from `/usr/lib/jkxr`
and the JKA game module from `/usr/lib/jkxr/base`. A deployment is valid only
after SHA-256 hashes of those installed files match the selected build outputs.
The detached baseline is installed as `rdsp-vulkan-baseline_x86_64.so`; a
command must set `cl_renderer` explicitly during A/B runs so archived config
cannot silently choose the other renderer.

## GLM LOD parity objective

The first implementation checkpoint is complete and awaits visual acceptance.
The Vulkan GLM loader now walks every `mdxmLOD_t` using each block's `ofsEnd`.
LOD 0 remains the authoritative hierarchy and bolt metadata, while every level
retains its own vertices, triangles, bone references, and GPU buffers. A strict
loader rejects the complete model if any level has an invalid table, surface
index, vertex, triangle, or bone reference instead of silently constructing a
partial model.

The runtime selection reproduces the legacy `G2_ComputeLOD` inputs:

- projected radius uses the center VR view, model scale, entity radius,
  `r_lodscale`, `r_lodbias`, `CGhoul2Info::mLodBias`, and `RF_G2MINLOD`;
- the result is cached once per model instance per frame and reused by both VR
  eyes and all material passes, preventing eye-dependent selection or popping;
- `r_vulkanGLMLod -1` selects automatically, while values from `0` upward force
  that level and clamp to the number available for each model;
- `r_vulkanGLMLodAudit 1` logs each model's LOD inventory once, and value `2`
  also logs selection changes with distance, projected radius, bias, level
  count, and forced state; and
- `r_vulkanTiming 1` adds an `rd-vulkan-glm-lod` sample containing per-level
  stereo selections and selected vertex and triangle counts.

An offline structured audit of the installed PK3 assets found 129 JKA GLMs, of
which 87 have multiple LODs, and 71 JKO GLMs, of which 54 have multiple LODs.
All offset chains and surface indices validated. Representative triangle counts
are 4,956/2,668/1,172/645 for JKA Jan, 2,921/1,908/776/476 for a stormtrooper,
and 2,948/1,953/887/509 for Kyle.

The forced-level runtime log proves that distinct geometry is selected even in
scenes where the authored lower LODs preserve silhouettes too closely to be
obvious in a headset. For example, Rosh changes from 3,150 to 866 triangles and
Kyle changes from 2,948 to 509 triangles. This mechanically accepts the loader
and selector. Automatic mode must still be compared with the legacy or Quest
renderer along a fixed near-to-far path, with no stereo mismatch, surface loss,
skin error, animation error, bolt error, or unstable threshold.

## GLM compute skinning objective

Ordinary Ghoul2 deformation is dispatched once per visible surface and pose
before the first eye render pass. Its output vertex range is cached and reused
by both eyes and every material pass. CPU skeleton evaluation, bolts,
attachments, collision queries, and gameplay remain unchanged. Disintegration
uses the CPU deformation path because it mutates vertex position and color in a
way that is intentionally separate from ordinary skinning.

`r_vulkanComputeSkinning 1` enables the path and `0` provides a direct CPU A/B
fallback. A graphics queue without compute support, optional resource creation
failure, a malformed bone range, stream exhaustion, or a surface without its
compute descriptor falls back surface-by-surface to CPU skinning rather than
preventing renderer startup. `r_vulkanTiming 1` reports compute dispatches,
vertices, command-recording time, and CPU fallbacks for each 120-frame sample.

Acceptance requires correct customization models, crowded ship actors,
attachments, ordinary gameplay animation, and Tenloss disintegration. The
crowded ship capture must show a material reduction in CPU skinning time and no
increase in GPU stereo time large enough to erase the frame-time gain.

The first JKA headset acceptance passed without animation, attachment, surface,
stereo, or disintegration regressions. In stable crowded-ship samples, CPU
command recording fell from roughly 41 ms to 7.6 ms and measured skinning work
from roughly 39.5 ms to 5.2 ms, while GPU stereo remained near 2.5 ms. Ordinary
frames reported no CPU fallback; charged Tenloss disintegration exercised the
intentional CPU path and remained visually correct.

The JKO headset acceptance also passed. `ns_streets` and its cutscenes were
visibly smoother, with no reported character, animation, attachment, stereo,
scope, or cinematic regression.

## Animated material and VR FOV contracts

Vulkan now retains every image and frequency from `animMap`, `clampanimMap`,
and `oneshotanimMap` stages. Ordinary stages advance from scene time; model
entities carrying `RF_SETANIMINDEX` select the authored frame with `skinNum`,
matching the legacy renderer. `rgbGen wave` is evaluated independently, so a
charged shield/ammo station can pulse its additive glow while frame 1 selects
the authored black/depleted image.

World scenes with `refdef.override_fov` now rescale the horizontal and vertical
OpenXR tangents independently by the game-to-headset FOV ratio. The asymmetric
optical centers and stereo view poses remain intact. The already accepted
Tenloss scope retains its separate circular zoom path; Force Speed and other
legacy override effects use this new path.

Acceptance requires a charged JKO shield station to be visibly illuminated and
animated, then visibly depleted after its reserve reaches zero. An ammo station
uses the same contract. Force Speed in both games must produce a centered,
binocularly comfortable transition into its widened FOV, hold that projection
for the active duration, and return cleanly to the normal projection without
disturbing HUD, controllers, scopes, or ordinary head tracking. The inherited
game-side envelope had accidentally commented the authored FOV amount out of
its hold branch while retaining it at both transition boundaries; JKA and JKO
now keep the full amount during the hold.

## Force Speed motion-blur contract

Both games expose `Force Speed Motion Blur` in the startup and in-game
Advanced Video menus. It controls the archived `cg_forceSpeedMotionBlur` cvar
and defaults to enabled. Because those menus and their localized strings live
in game-specific PK3s, a valid deployment must rebuild `z_vr_assets_jka.pk3`
and `z_vr_assets_jko.pk3` and refresh the higher-priority OpenJK/OpenJO home
copies as well as the packaged copies.

The cgame supplies a normalized effect envelope in `refdef_t`, using the same
300 ms entry, held interval, and 200 ms exit timing as the accepted Force Speed
FOV change. Scope views and live cinematics explicitly suppress it. While the
effect is active, Vulkan renders each eye's world into a private color target
and blends the preceding image from that same eye over the current scene. This
produces temporal movement trails instead of a current-frame radial zoom. The
history is refreshed after each eye is composed and invalidated whenever Force
Speed stops, while screen-space HUD and controller elements are drawn afterward
at full sharpness.

Rapid physical headset rotation or translation attenuates the history
contribution. In-game locomotion therefore retains the speed trail without
smearing ordinary head movement or mixing eye histories.
`r_vulkanForceSpeedBlurStrength` defaults to `1.0` and is a renderer-side
tuning control; the user-facing menu remains a simple on/off choice.

Acceptance requires a smooth temporal blur ramp into and out of Force Speed in
both eyes, binocularly stable world trails, sharp HUD and controller overlays,
unchanged scope behavior, no physical-head-motion smear, and no effect when the
menu option is off. Ordinary rendering must continue on the direct swapchain
path when the effect is inactive.

The first JKA headset acceptance passed on 2026-08-31: the temporal trail gave
a clear impression of accelerated movement, remained comfortable in both
eyes, and left ordinary rendering unchanged after Force Speed ended. A brief
frame-rate dip seen once in `yavin2` was not reproducible in `t2_rancor` and
had no corresponding GPU-time spike, so it remains unconfirmed rather than a
motion-blur regression.

## OpenXR continuity contract

### Cinematic texture ownership

The legacy cinematic system reuses integer client IDs after a stream stops.
Vulkan material registrations can outlive those decoder clients, so a client
lookup must select the newest active registration rather than the oldest
historical texture with the same ID. Otherwise decoded `videoMap` frames are
uploaded into a stale menu texture while the current in-world display remains
black, as happened to JKO's Mon Mothma hologram.

Raw cinematics also reuse a client ID across different dimensions. There must
be exactly one raw texture owner per client. A dimension change replaces that
texture's image in place while retaining its texture handle and descriptor
sets; it must not append another same-client entry every frame. Repeated
`first raw cinematic frame` messages or growth toward the 4,096-texture limit
during one movie is an acceptance failure.

### Procedural effect primitives

JKO's Valley of the Jedi pool illumination uses the Raven `RT_CYLINDER`
contract: `origin` and `oldorigin` are its endpoints, `radius` and `backlerp`
are the two ring radii, and `axis[0]` is its longitudinal direction. Vulkan
must generate the same wrapped 8-to-40-segment cylinder, including the legacy
tapered-cone case. Aggregate effect diagnostics report cylinder counts so a
submitted but unsupported primitive cannot silently disappear again.

### Ghoul2 entity transforms

A submitted `refEntity_t::axis` marked with `nonNormalizedAxes` is the
authoritative scaled model transform. The game-side `ScaleModelAxis` helper has
already folded `modelScale` into normal scaled Ghoul2 submissions, so the Vulkan
model matrix must not multiply those axes by `modelScale` again. Doing so scales
actors twice while `G2API_GetBoltMatrix` scales its result once, separating
attached geometry from bolt-derived effects.

Vulkan applies `modelScale` to unscaled axes, such as the tracked first-person
saber-hilt submission, and when it has to synthesize missing Ghoul2 axes from
`refEntity_t::angles`. This keeps scaled actor geometry, attached weapon models,
and effects such as saber blades in one coordinate space without changing VR
hilt size.

`re.Shutdown(qfalse, qfalse)` is a soft renderer flush used while connecting,
loading a map, and parsing a new game state. Its explicit legacy contract is to
retain the window and graphics context. The Vulkan renderer must therefore keep
the OpenXR instance, session, reference spaces, actions, Vulkan device,
swapchains, persistent image cache, and most recently released swapchain images
alive. Model, skin, and animation registrations are a per-map epoch and must be
reset: game code requires the normal and cinematic Ghoul2 GLAs to receive
consecutive handles. Transient scene collections and borrowed CGame/UI pointers
are also cleared. A full teardown remains reserved for `destroyWindow == true`,
such as `vid_restart` or process shutdown.

Every renderable OpenXR frame must submit a composition layer. A successful
screen-layer or stereo-projection render remains valid until a successful
render in the other mode replaces it. During synchronous map and cinematic
handoffs, the renderer re-submits that last released image with its captured
pose and FOV. A genuinely empty engine frame renders opaque black into both
eyes. It must never expose the WiVRn compositor background, and diagnostic eye
colors or markers must never substitute for the black fallback.

The log records each soft flush as `soft renderer shutdown reset model epoch`
and reports any retained-image handoff. `ending renderable frame without a
layer` is an acceptance failure. A single game process should initialize
Vulkan/OpenXR only once across ordinary map and savegame loads.

Acceptance requires both games to remain compositor-opaque through startup,
menu-to-load, load-to-cinematic, cinematic-to-gameplay, gameplay-to-load, and
cinematic-skip transitions. The preferred result is the last frame or authored
loading/intermission image; black is acceptable where the engine has no image.
The WiVRn background must never become visible.

## VR console presentation

The legacy console derives its columns from the native render-target width.
That produces hundreds of tiny glyphs at supersampled headset resolutions and
places the edit line outside the comfortable central field of view. Under the
Vulkan renderers, the same console buffer, command history, completion, and
scroll controls instead use a fixed 120-column layout with 40 visible output
rows. They are drawn in a centered translucent panel with the edit line pinned
near its lower center.

While an active world is present, opening the console must not promote the
composed eye image to a mono OpenXR quad. Vulkan retains the ordinary stereo
projection and tags only the console's rectangles. The renderer captures a
yaw-only, level OpenXR pose when the console opens and places its centre six
metres ahead at the captured eye height. The pose is fixed until close; head
translation and rotation therefore reveal a world-locked terminal rather than
dragging a head-locked layer. The accepted panel angular size remains 56% of the
combined horizontal FOV by 35% of the combined vertical FOV, with the virtual
keyboard extending beneath it. Menus and cinematic screen layers retain their
existing captured world-locked behaviour.

Both controller aim poses are intersected with the same spatial plane in the
tracking reference space. Their triggers activate keyboard keys, and all
gameplay input is suppressed while any console phase is visible. Printable
keys enter the normal event queue as `SE_CHAR`; Enter, Escape, Tab, arrows,
Backspace, Delete, Insert, Home, End, Page Up, and Page Down enter as paired
`SE_KEY` events. This keeps console editing independent of platform keyboard
layout and pointer size on both x86-64 and ARM64. Shift is one-shot, Caps Lock
is persistent, and editing/navigation keys implement delayed repeat.

By default, holding Y for 600 ms toggles the console while a shorter press
retains the datapad action. Reaching the threshold consumes the short action.
The Comfort settings and archived cvars expose the policy:

- `vr_console_button`: `0` for Y, `1` for B, or `2` for the handedness-aware
  datapad button.
- `vr_console_hold_ms`: hold threshold, clamped to 300-1200 ms.
- `vr_console_animation`: `1` for the holographic transition or `0` for an
  immediate terminal.

The 180 ms opening transition flashes a thin cyan line at the final pose,
expands the panel in place, fades text during the latter half, and unfolds the
keyboard rows downward. Closing reverses over 120 ms. Only scale and opacity
change: depth, position, and orientation never move or overshoot. A local sound
and short haptic pulse confirm each transition; key presses have a lighter
pulse.

Acceptance requires readable, fused 120-by-40 text and cursor rendering; a
fixed, level world-space pose; correct two-controller pointing; a complete
keyboard with history, completion, editing, navigation, and repeat; no input
leaking into gameplay; preserved short-press datapad behaviour; and an
artifact-free return to the world when the console closes.

Live immersive cinematics retain the scripted camera pose while allowing the
ordinary turn stick to add yaw. The compositor snapshots `snapTurn` at
cinematic entry and applies only its subsequent delta to the scripted camera
basis before composing the HMD's local 6DOF rotation. This preserves entry
recentering and the level-horizon roll contract without causing a first-frame
yaw jump. Pre-rendered screen-layer cinematics are unaffected. Quest 3/WiVRn
headset acceptance confirmed responsive turn-stick yaw, correct HMD 6DOF and
horizon behavior, and no entry jump or eye disagreement.

The active renderer owns the OpenXR session and therefore must also own its
vibration output action. Vulkan adds that action to the same `jkxr_gameplay`
action set used for tracked input, binds it to both controller haptic paths, and
exposes a small renderer API accepting a hand, duration in milliseconds, and
normalized amplitude. The executable preserves the inherited controller
channel mask while routing every existing haptic event to that active action.
The legacy GL-side action remains only as a fallback for its own session.

Force Push/Pull emits one 120 ms, full-intensity off-hand pulse from
`ForceThrowEx`, after health, state, debounce, cinematic, ability, and Force
energy checks have accepted the action. A rejected gesture therefore produces
no false success pulse. `vr_haptic_test [left|right|both] [duration_ms]
[amplitude]` bypasses gameplay and validates the renderer/runtime path directly.
With `vr_controller_debug 1`, every accepted Vulkan submission records its
hand, duration, and amplitude, while OpenXR failures report their result code.
Quest 3/WiVRn headset acceptance confirmed independent left/right diagnostic
pulses, successful Force Push/Pull feedback, and right-hand saber feedback.

Saber activation and surface-contact events are intentionally much lighter
than firearm recoil or Force feedback because the weapon remains continuously
in hand. `vr_saber_haptic_intensity` scales only saber `chainsaw_fire` events,
defaults to `0.20`, and uses a short 50 ms pulse. Melee events sharing the
legacy event name retain their original duration and amplitude.
Quest 3/WiVRn headset acceptance confirmed that the default `0.20` setting is
present but unobtrusive during normal saber use and leaves the other haptic
classes unchanged.

The fallback haptic queue stores durations in milliseconds, while `ToXrTime`
accepts seconds. It converts milliseconds to seconds before constructing the
OpenXR nanosecond duration for every controller profile. The inherited code
performed this conversion only for Vive controllers, leaving Touch/WiVRn
requests three orders of magnitude too long.

The packaged launchers content-compare both the game module and VR asset PK3s
against the engine's per-user `base` directory before every launch. The game
module must be synchronized alongside its executable whenever the shared VR
state changes; otherwise OpenJK can silently load an older module left in the
game directory and produce an executable/module ABI mismatch. Timestamp-only
copying is insufficient for local builds and package replacements.

## Vegetation visibility trial (2026-09-06)

Status: accepted in the user's full Yavin traversal, including the corrected
plant placement near later howlers. Distance/fade, stereo, wind, water,
characters, shadows and exit credits passed. Other levels and JKO vegetation
remain on the cross-level validation list. This deliberately improves beyond
the original Quest behavior.

- Yavin's generated plants are `surfaceSprites`, usually a single two-triangle
  quad, not GLM models with lower-detail meshes. The recorded Yavin build had
  1,855 generated anchors across the map before the planar-placement fix,
  and 2,031 after it. Distance scaling does not generate additional anchors;
  the placement correction restores plants on two previously rejected surfaces
  at the authored density.
- Authored fade distances are short. In addition, multiplying distance alpha
  into a GE192 material alpha test discards the plant before its fade reaches
  zero. Conditional division at the randomized fade boundary also introduced
  a discontinuity. Plant coverage now falls monotonically with smooth endpoints.
- `r_vulkanVegetationDistanceScale` defaults to 3, bounded to 1..4. It extends
  both authored fade distances for vertical/flattened plants only. Effects,
  snow, other oriented sprites, water and model LOD are unchanged.
- `r_vulkanVegetationCoverage` defaults to 1. Generated plant draws with GE128
  or GE192 cutouts preserve their texture mask independently of distance fade.
  A texture-anchored ordered coverage pattern discards both color and depth
  in the fading area. It does not use per-eye screen coordinates or temporal
  noise. Shared material definitions and ordinary world/model alpha tests are
  untouched. Set 0 to compare the old combined-alpha cutoff, independently of
  the distance setting; this does not restore the old discontinuous fade math.
- CPU geometry generation/upload is cached once per batch per frame and reused
  by both eyes. Each eye still needs a draw, and visible portal views can add
  draws. This is not repeated generation of every plant for every eye.
- With `r_vulkanTiming 1`, `rd-vulkan-vegetation` reports average candidates,
  generated plants, uploaded KiB, CPU streaming time, cache hits and stereo draw
  counts. PVS rejection, the existing vertex-stream limit and authored density
  remain in place. Extending distance can increase fill cost: no claim of a
  free performance improvement is made before the headset comparison.

Acceptance run: use the same Yavin starting-hill viewpoint for 30 seconds at
distance scale 1 and 30 seconds at 3, leaving coverage 1 and bloom 0 throughout.
Then approach/retreat slowly at scale 3 and check visible range, fade, distant
shimmer, stereo agreement and wind. Check river, temple pool, actors and shadows
for regressions. Compare GPU-stereo and CPU recording times as well as the new
plant counts; do not compare only FPS or different camera paths. If plant fill
cost proves material, evaluate instancing/spatial batching and distant density
LOD using these measurements, without restoring the original short pop-in.

### First headset results and placement correction

`/tmp/jka-vegetation.log` confirms that trying distance scale 2 between 1 and 3
is valid. Stable starting-view blocks gave approximately:

| Distance scale | Generated plants/frame | CPU plant stream | Stereo GPU, whole scene |
| --- | ---: | ---: | ---: |
| 1 | 228 | 0.025 ms | 2.53 ms |
| 2 | 356 | 0.038 ms | 2.63 ms |
| 3 | 722 | 0.076 ms | 2.85 ms |

These use repeated, stable generated counts and exclude mixed distance-change
blocks. GPU timings include the whole scene and changing character activity;
they are not isolated plant GPU timings. No sprite-stream exhaustion was
reported. All 23 authored sprite stages loaded, with no unsupported stages or
unavailable textures. The user reported no noticeable pop-in/out at scale 3,
and confirmed the credits. Keep 3 as the default.

The reported sparse area near the later howlers has a concrete placement issue:
in `yavin1b.bsp`, planar surface 807 (near howler4) is horizontal, with plane
normal `(0,0,1)`, but its smoothed vertex normals lean toward the surrounding
cliffs. Testing those vertex normals against the 0.5 upward-slope threshold
rejects all four planting triangles. Surface 819 near howler5 similarly loses
one planting triangle. Legacy `RB_SurfaceFace` supplies the BSP face plane
normal to `surfaceSprites`; it does not use those smoothed vertex normals.

The follow-up supplies the plane normal only to vertical/flattened plant
generation on planar surfaces. No shared vertex data, surface winding,
lighting, material, depth, water, weather/effect sprites or triangle-soup/patch
placement rules change. A read-only audit of the shipped map finds exactly two
changed surfaces (807: 0 to 4 eligible triangles; 819: 0 to 1). Total eligible
triangles rise from 69 to 74, with none removed. This is a source-backed fix,
but does not prove it covers every location the user remembers as vegetated.
With timing enabled, new `rd-vulkan-vegetation-build` lines report per-surface
normal source, anchor count and rejected slopes, including empty batches.

Follow-up test: use scale 3, revisit the two later howler corners, check ground
plants and nearby slopes, then verify the original starting hill and water
remain correct. No need to repeat the 1/2/3 comparison. Restart/reload the map
to regenerate its anchors; capture the build diagnostics in the launch log.

Accepted follow-up: `/tmp/jka-vegetation-placement.log` records 132 anchors on
surface 807 and 44 on surface 819, both using plane normals with zero rejected
slopes. Total anchors are 2,031; all 23 sprite stages are supported, with zero
invalid stages or unavailable textures and no stream-exhaustion reports. The
user traversed the entire Yavin level and reports that vegetation is fixed.
The run ends with normal shutdown. Existing main-menu parsing warnings and
missing `tutorial_video_7/8` messages remain unrelated outstanding diagnostics.
Do not treat this as proof of full campaign coverage: test both campaigns before
closing the cross-level acceptance item. No further visual tuning is requested.

The pure fade tests run with the existing Boost suite (`BuildTests=ON`, then
`ctest --test-dir OpenJK/build-linux --output-on-failure`). They cover bounded
settings, non-finite inputs, fade endpoints, monotonicity/continuity across
random phases, and equivalent scaled distances. Both JKA/JKO renderer targets
and Vulkan 1.3 shaders must also compile. These checks do not replace visual
inspection of the coverage pattern in a headset.

The exit credits are rendered menu text (`ui/credits.menu`), not a movie.
Both game asset packs now credit Patola for the Linux port, OpenGL-to-Vulkan +
SDL3 port, and additional features via ChatGPT Codex. Existing contributor
lines are retained. Only that menu differs from the installed asset archives.

Worktree recovery: the previous `/tmp/jkxrl-yavin-parity` checkout disappeared.
Work continues from pushed `2637cfb` in
`/home/patola/workspace/codex/JKXRL-active` on `codex/vulkan-portal-parity`.
The older, dirty `JKXRL` checkout was left untouched. Renderer-only builds use
system OpenXR headers (`-DCMAKE_CXX_FLAGS=-I/usr/include/openxr`) because the
previous ignored include directory was not part of the checkpoint. Executable
and game modules are not replaced by this renderer/asset-only deployment.

### Optional bloom measurement

The completed `/tmp/jka-m5-bloom-off.log` contains 30 full timing blocks; the
21 blocks with active lights/models average CPU record 8.222 ms and stereo GPU
2.623 ms (block GPU averages 2.464..2.708 ms). There are no glow timing reports.
Two pre-existing `ui/main.menu` parsing warnings remain. The earlier bloom-on
summary recorded roughly 1.029 ms in the glow GPU timing bracket. That bracket
also includes HUD work, and the on/off camera paths differ, so this is not a
strict isolated bloom-cost subtraction. Keep bloom optional and off by default.
The previous uncommitted extra bloom recording/draw counters were not in the
recovered checkpoint; the renderer retains its committed GPU glow timings.

## Deferred work

- Full physical ragdolls.
- Complete the automatic GLM LOD visual and performance regression matrix.
- Tune tracked Force Push/Pull thresholds from captured gesture diagnostics as
  needed. A recognized gesture that cannot run because energy is insufficient
  should produce an
  audible rejection cue and controller haptic instead of failing silently.
- Complete headset menu, fallback-halo, and timing acceptance for the optional
  multi-scale Vulkan bloom pyramid before freezing its visual baseline.
- Validate the accepted Yavin vegetation improvements across other JKA/JKO
  levels during full campaign playthroughs. Keep the longer draw distance,
  gradual fade, stereo stability, wind and restored planar placement; do not
  revert to the original Quest pop-in behavior. Profile unusually dense areas
  before considering further density/instancing optimizations.
- Evaluate AI-upscaled cinematics while preserving optional compatibility with
  original game assets and licensing constraints.
- Broad x86-64 optimization before the eventual ARM64 port; ARM64-specific work
  remains last because target hardware is not yet available.
