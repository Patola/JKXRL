# Bounded pre-optimization audit

Date: 2026-09-30. Specular highlights are now headset accepted in both games.
This audit adds no runtime code and deploys no binaries. It replaces candidate
lists with a bounded implementation queue, not a claim of exhaustive parity.

## Method and limits

`tools/audit_remaining_materials.py` reads installed stock assets*.pk3 archives,
shader definitions and non-MP RBSP surfaces. Later archive definitions win.
It does not enumerate loose overrides, mods, dynamic model registration or
script execution. Counts establish authored data, not visibility from every
camera or proof of a perceptible change. Five synthetic tests cover token
matching, UV translation/distortion, invalid ranges/lumps and archive scanning.

Reproduce from the repository root:

```sh
python3 tools/test_audit_remaining_materials.py
python3 tools/audit_remaining_materials.py "/games/SteamLibrary/steamapps/common/Jedi Academy/GameData/base"
python3 tools/audit_remaining_materials.py "/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData/base"
```

| Stock data | JKA (34 maps) | JKO (26 maps) |
| --- | ---: | ---: |
| Secondary-lightmapped surfaces | 2,128 | 5,499 |
| Nontranslated secondary lightmap UVs | 0 | 0 |
| Vertex-lit surfaces with secondary styles | 2,023 | 127 |
| Those with nonzero secondary RGB | 1,728 | 127 |
| Surface-sprite parents assigned to local fog | 0 | 3,183 |

## 1. Image-output contracts

Implemented 2026-09-30; headset accepted, including the immediate post-save
preview refresh (2026-10-02). See [save preview run](save-preview-run.md).
User clarified that external VR screenshot capture already serves their needs:
standalone OpenJK screenshot commands are deferred, not a pre-optimization gate.
The necessary internal save-preview capture and safe callbacks are restored.

Original audit findings: in `rd-vulkan/tr_init.cpp`,
GetScreenShot and LoadJPGFromBuffer are no-ops, SaveJPGToBuffer returns zero,
and TempRawImage_ReadFromFile returns null. These have live callers:

- `client/cl_scrn.cpp`: SCR_PrecacheScreenshot marks the buffer valid after
  calling GetScreenShot, even though Vulkan never writes pixels.
- `server/sv_savegame.cpp`: JKO SG_WriteScreenshot serializes the zero-byte
  encoder result. Read-only inspection of five recent saves (including
  auto_ns_streets, auto_yavin_temple and auto_valley) found empty SHOT chunks;
  older jkii000/jkii002 saves contain nonempty JPEG data.
- SG_ReadScreenshot calls the decoder with uninitialized width/height and
  subsequently uses them. The no-op decoder therefore creates undefined
  behavior on a real save-menu path; no crash is claimed from this audit.
- `ui/ui_main.cpp`: save selection calls SG_GetSaveImage. The preview also
  treats the first pixel's red channel as a validity flag, which is not valid
  for a legitimate image with a black first pixel.
- Vulkan does not register the screenshot console commands used by the UI.

Scope: bounded JPEG decode/encode, explicitly initialized outputs and failure
results, safe malformed/empty thumbnail handling, autosave levelshot loading,
and a bounded on-demand Vulkan readback required by saves. Standalone screenshot
commands were subsequently deferred by the user. Avoid
per-frame CPU readback/stalls. Preserve stock save serialization and existing
files; do not rewrite campaign saves to repair a cosmetic preview. JKA uses
levelshots rather than this JKO embedded-thumbnail path.

Gate: valid old JPEG previews, empty old previews, malformed/truncated input,
new manual/auto previews and screenshots; verify dimensions/orientation and
return to gameplay in both games. Test first-pixel black explicitly. Save-state
round trips must remain intact. Missing thumbnails do not imply corrupt saves.

## 2. Secondary vertex lighting

Implemented 2026-10-02; headset accepted in JKA and JKO across all listed
test scenes. See [vertex lighting run](vertex-lighting-run.md).
Uses bounded updates of affected static vertex RGB when styles change, without
extra draws or changing the shared 56-byte vertex format. Original alpha and
single-style rendering are preserved; a non-archived comparison toggle restores
the previous path. Legacy-math and actual update-command contracts are tested.

Legacy `rd-vanilla/tr_surface.cpp` ComputeFinalVertexColor sums every active
vertex style's RGB, multiplies by its current style color, shifts by eight
and clamps, preserving the original alpha. Vulkan VK_WorldConvertVertex
retains only color[0]; the styled vertex stage evaluates only style slot zero.
Nonzero secondary stock colors therefore cannot reach the rendered result.

Refined live targets: JKA kor2 guard statues, t3_bounty curved bomb-3-area trim,
and JKO artus_mine surfaces 18743..18752 (baked damaged R5 parts). The earlier
kor1 white-fixture candidate uses constant-color stages and is unsuitable for
a visible comparison; its ground_transformer and artus_topside rock_color
secondary contributions are too weak to be useful primary tests. Source-derived
viewpoints and expected light states are recorded in the run document.

Gate: compare the actual legacy sum with static and animated style fixtures;
preserve alpha, single-style colors, fullbright behavior and shared stereo
results. Avoid extra whole-world draws or an unconditional vertex-format cost
without measurement. Regression scenes include the accepted Artus red-pulse
room and ordinary Yavin/Hoth colors. Choose a precise live target from the
affected bounds rather than sending the user on a campaign search.

## 3. Local fog on surface vegetation

Implemented 2026-10-02; headset run 2026-10-03 confirmed affected vines and
unchanged dry controls. The wider-separated follow-up also confirmed clear
distance attenuation. This batch is headset accepted. See
[vegetation fog run](vegetation-fog-run.md) for precise, collision-checked
viewpoints and the isolated `r_vulkanVegetationFog` comparison switch.
Fog is fused into the existing plant draw after cutout/coverage rejection,
without an extra vegetation draw or a shared vertex-format expansion.

All 3,183 local-fog surface-sprite parents found are in JKO yavin_swamp.
Example: textures/yavin/s_rock1_vines has an opaque base and two depth-writing
alpha-cutout hanging-vine stages. Legacy RB_DrawSurfaceSprites supplies fogNum
to SQuickSprite only when the parent shader has a fogPass. Vulkan's generated
vegetation draw path previously supplied no local fog attenuation.

The production fog-brush audit also validates 19 local water-fog brushes in
that map (ASan/UBSan clean). This is vegetation viewed through/intersecting
local water fog, not permission to add generic swamp haze. Parent membership
does not mean every generated plant is submerged; evaluate the actual fragment
against the accepted fog boundary and preserve legacy shader eligibility.

Gate: submerged/partially submerged eligible plants, dry plants, exposed
boundary, stereo motion, goggles and unchanged vegetation density/distance.
Keep the accepted Artus sludge, Yavin river and temple pool as regressions.
Do not fog all independent additive effects merely because this path is missing.

## Closed or deferred candidates

- All 7,627 secondary-lightmapped surfaces have UV mappings representable as
  a constant offset from slot zero. The current implementation already handles
  those; defer a generalized secondary-UV layout unless new data requires it.
- No stock shader definitions for tcGen vector, inverse vertex/entity RGB,
  inverse vertex alpha or alpha-wave noise were found in this scan.
- tcMod entityTranslate has 64 JKA and 41 JKO BSP references. No nonzero brush
  shaderTexCoord input was established. Defer rather than infer a visible bug
  from the declaration. General tcMod transform already works.
- Portal-alpha references (six JKO surfaces) do not change the previously
  agreed deferral of unverified general mirrors/portals. Dynamic model-only
  usage is outside this scan and is not certified absent.
- Other independent FX fog, dormant generators, sand/dust placement searches,
  IK/gore extras and full physical ragdolls retain their existing deferrals.

After these three bounded batches: checkpoint review and measured optimization,
not another open-ended feature inventory. Patola supplied the ARM-oriented
notes on 2026-10-03; corrections, source review and measurement priorities are
in [ARM-aware optimization](optimization-arm-readiness.md). Native ARM64 is
the target, without FEX; desktop measurements are not ARM FPS predictions.
Retain Artus Mine's red-pulse cargo room as
a benchmark alongside crowded cutscenes, t1_rail and t2_wedge. The requested
localized missile-explosion optimization, remaining high white subtitles and
four-second security-camera help timeout remain recorded follow-ups before ARM.

Additional confirmed profiling target: JKO `artus_mine`,
`setviewpos -2560 -100 665 85` (damaged R5 viewpoint). Patola reported a notable
performance cost here, apparently unchanged with `r_vulkanVertexStyles 0/1`.
Capture CPU/GPU frame times at this exact position/yaw during optimization;
this is a qualitative observation, not evidence that vertex lighting caused
the slowdown. Keep it separate from the cargo-room red-pulse test.
