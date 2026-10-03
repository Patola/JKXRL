# JKO save previews

Date: 2026-10-02. Capture/HUD/menu-latency/overwrite label and immediate
post-save selection refresh are headset accepted.

The user confirmed distinct previews without HUD, a visible overwrite button,
and no perceptible pause on menu entry. Runtime readback measured 2-4 ms and
total capture 13-15 ms in `/tmp/jko-save-preview-v2.log`.

Resolved report: a newly saved game's preview stayed black until selecting
another save and returning. Menu save now queues `ui_refreshSaveGames` after
the `save` command, instead of requesting a draw-time refresh when the save is
only queued. Once writing finishes, the UI rereads the directory, selects the
saved filename after sorting, reloads its image, and synchronizes the listbox.
If the file does not exist (failed save), the normal prior-selection/empty-list
fallback remains. No screenshot recapture, renderer, asset or save-format changes.

The production refresh helper is compiled into a focused ASan/UBSan test for
new saves, reordered overwrites, missing targets and empty directories. Both
games rebuilt; build/test logs are `/tmp/jkxr-save-refresh-build.log` and
`/tmp/jkxr-save-refresh-tests.log`. Headset follow-up: create a new save, then
overwrite a disposable save from a different view. In each case, the selected
thumbnail should appear immediately without clicking another row; it must
remain HUD-free and reopening the system menu should remain responsive.
All 28 regression tests passed. Deployed both engines and verified byte equality;
previous engines are in `build-vulkan-clean/pre-save-refresh-20261002-tN9m7l/`.

## Scope

The user already captures images through the VR environment. OpenJK's separate
screenshot/F12 file-writing commands are not required for that workflow and
remain deferred. This batch restores the internal image path needed by JKO's
save/load menu. JKA continues to use its authored level images; it does not
gain embedded save thumbnails. No new screenshot bindings, cvars or assets.

JKO manual menu saves use a fresh left-eye gameplay image requested before the
menu opens. A gameplay render completes the request, copying the acquired image
before OpenXR release and restoring its layout. CPU readback/resampling happens
only on a preview request, never continuously. A center 4:3 crop is stored in
the legacy 512x512 format, which stock menus display at 180x135. Display-ready
bytes are retained without a second gamma pass; RGBA/BGRA are handled explicitly.
Only this explicit capture frame omits screen rectangles (HUD/reticles), in both
eyes; normal gameplay and external screenshot capture are unchanged. There is
no extra permanent target or stereo capture mode. A transient GPU blit crops
and shrinks the eye image to 512x512 before readback: 1 MiB instead of about
38 MiB at 3096x3243. Devices without linear blit support retain the CPU fallback.
CPU conversion first copies mapped memory sequentially into cached memory;
same-size conversion no longer executes the 12-sample resampling loop.
All transient capture resources are freed after reading.

The extra gameplay frame is still synchronous, so a frame-time-sized menu cost
remains. `save-preview-timing` reports frame and total milliseconds;
`rd-vulkan-save-preview` reports read time and GPU-thumbnail/CPU-fallback path.
Do not claim the perceptible pause eliminated before the user's next test.

Capture failure, console saves and autosaves use the authored levelshot when
available. Unsupported optional swapchain readback cannot prevent VR startup.
Quicksaves request a fresh image; menu saves use the image captured before the
menu opened. Loading-screen RGBA and save-encoder RGB have separate validity
and storage so one cannot accidentally be encoded as the other.

JKO JPEG callbacks now have bounded, recoverable errors. Empty/truncated images,
wrong dimensions and oversized declarations cannot use unset dimensions or
null image pointers. The legacy vertical orientation and SHLN/SHOT chunk layout
are unchanged. Empty old thumbnails are consumed and fall back in the menu;
no existing save is rewritten. A valid black first pixel no longer suppresses
the preview. An independently corrupted game-state chunk is outside this fix.

The engine/renderer API is now 24 (two explicit request/read callbacks). Deploy
engine and renderer together. Game modules are rebuilt/synchronized as well;
the corrected JKO menu archive must also be deployed. Both games build. Backup of the preceding
six installed binaries: `build-vulkan-clean/pre-save-preview-20260930-cRxPxb/`.

Follow-up backup: `build-vulkan-clean/pre-save-preview-v2-20261002-V9uyiI/`
(six binaries and the previous JKO archive). The menu's missing overwrite label
was an asset contract failure: menus_vr.sp declared 179 entries but contained
190. The legacy parser read the remaining entries as package metadata, replacing
the MENUS_VR namespace with CONSOLE_ANIMATION_DESC. Correcting COUNT restores
the overwrite label and other VR labels without hardcoding English UI text.
The real StripEd parser regression reproduces the broken namespace before the
fix and verifies both overwrite and the last console label afterwards.

Installed-file equality verified after deployment. SHA256:
- openjk_sp.x86_64: fbf03493cd86ab1631f360092b58c5fcd1551a223f934a333fb14154b22f1d8c
- openjo_sp.x86_64: b8aa5eac66d0d8040d376185abdc1078752d412fb6091965a3c188699874049e
- rdsp-vulkan_x86_64.so: d654f18f1130a1bc0fb9834b662d3c80c1530a421dca4d154f53ef50ca5f4d90
- rdjosp-vulkan_x86_64.so: 10b2544ecfa904dbdbc26c9e1785ec268935710360d7c0c40aad88b8797f7aac
- z_vr_assets_jko.pk3: 4c277caa6e3cac40bc96d68164d76e5e3b9337a8c49515aad5584eb5b8e81303

## Automated checks

- SavePreviewTests: JPEG round trip, legacy orientation, black first pixel,
  RGB/BGRA, crop/resampling, bounded destination, empty/truncated/bad images.
- ASan/UBSan repeat plus decoding JPEGs extracted read-only from existing
  jkii000 and jkii002 saves (249914 and 94749 bytes). Original saves untouched.
- Compiled production SG_ReadScreenshot tests: empty SHOT consumption,
  decoder writing nothing, null image with valid dimensions, read failure,
  valid output and oversized declaration. Allocation cleanup checked.
- Production GPU copy/barriers run in the console raster probe across its 292
  views; existing raster coverage/UV assertions remain unchanged. The GPU
  thumbnail crop/resize is compared against independently sampled full-image
  pixels. Passed on RX 7900 XTX with Khronos validation and no validation errors.
- Full 28-test CTest regression suite, both engine/renderer builds and installed
  file equality checks. Logs: /tmp/jkxr-save-preview-v2-build.log,
  /tmp/jkxr-save-preview-v2-tests.log and /tmp/jkxr-save-preview-v2-gpu.log.
  These do not establish headset acceptance.

## JKO headset run

```sh
JKXR_JKO_GAMEDATA="/games/SteamLibrary/steamapps/common/Jedi Outcast/GameData" \
jkxr-jko +set rd_vulkanDiagnosticWorld 0 +set cg_thirdPerson 0 \
2>&1 | tee /tmp/jko-save-preview-v2.log
```

1. Load a familiar save and face recognizable scenery. Open the ordinary game
   menu and create a NEW manual save. Keep existing campaign saves intact.
2. Open Load and select that save. Its preview should show the scene you faced,
   upright, with normal colors, without floating HUD or a menu/console, blank
   square or older view. Old saves keep their existing embedded images.
3. Face a noticeably different scene, make a second NEW save and alternate
   selection between the two. Their previews should differ. Load each and
   confirm the correct position/state, then return to normal gameplay.
4. Select an older save with a missing thumbnail. A static level image is an
   expected fallback. Select an older nonempty preview if available; it should
   still display correctly. Load a known working old save to check compatibility.
5. Test a quicksave only in a disposable run (it overwrites the quicksave slot).
   Move/turn before saving again and check that the preview updates. On a normal
   autosave/level transition, the static levelshot is intentional.
6. Reopen/close menus a few times; verify headtracking, hands, console and audio
   return normally, the gameplay HUD returns, and compare the menu-opening pause.
   Select a disposable new save and verify OVERWRITE GAME is readable and works.
   Continue using your usual headset screenshot capture.
   Exit normally. No cvar tuning is needed.

Expected diagnostics: `rd-vulkan-save-preview: captured HUD-free left-eye gameplay image`
and `save-preview: ... bytes=<nonzero> source=gameplay` for a successful manual
capture. `source=levelshot fallback` is expected for autosaves/console saves,
but repeated fallback for ordinary menu saves requires investigation.

After JKO acceptance, give JKA a separate short load/menu/save/regression run
with /tmp/jka-save-preview.log. Its previews should retain existing behavior;
do not expect new camera thumbnails there.
