# HD mod trailer plan

A ~60 s cinematic trailer for the Mall Maniacs HD rebuild: HUD-free fly-bys of
every mall, a montage of staged scenes that never occur in-game, and a closing
**MALL MANIACS HD** logo, set to synthesized music. Everything is produced from
this repository — the engine renders the footage, a Python script synthesizes
the score, and a Python script drives `ffmpeg` to do the edit. No external video
editor, no external audio.

## Revision 2: the epic cut (supersedes the beat sheet and shot lists below)

The first draft (60 BPM, slow fly-bys from each start gate, mostly the ICA)
read as a tour. After review it was rebuilt to be faster and more epic, while
keeping beauty. The sections below describe the first draft wherever they
disagree with this one.

**Score** (`music.py`): 125 BPM, so 1 beat = 12 frames, 1 bar = 48 frames,
and 60 s = 31.25 bars.

| Bars | Frames | Section | Music | Picture |
|---|---|---|---|---|
| 1–4 | 0–191 | Calm 1999 | D major, the mall chime, soft pad | Black, then the ICA gate in the 1999 look; red, red, green start beeps |
| 5–12 | 192–575 | Reveal, then epic | Hit on the green light, D minor (Dm–B♭–F–C), string ostinato, taiko, braams; the chime becomes the heroic brass line | HD wipe and widen as the gates open and players burst out; fly-bys in all five malls; "5 MALLS" |
| 13–16 | 576–767 | Beauty | Drums out, piano and strings | Floating goods in the Aqua tube, `m5_midair`, the cast beneath the Future rocket ("10 MANIACS"), a frozen orbit in Orient |
| 17–24 | 768–1151 | Action | Full drums, snare backbeat | Aqua cart race (Vagnrace, 8 racers, follow camera), item storm, grab frenzy ("1 SHOPPING LIST"), slip, throw-hit |
| 25–26 | 1152–1247 | Build | Riser and accelerating snare/taiko; one silent beat | Cuts of 24, then 12 frames; black on the silent beat |
| 27–31 | 1248–1499 | Logo, to the end | D major slam, decaying to a last high chime | The MALL MANIACS HD logo held over a slow crane up past the Future rocket; fade out from 58.6 s |

**Gate sync.** A round's gates open exactly 7.00 s (175 frames) after it
starts. The cold-open clips start at timeline frame 17, after a black
lead-in, so the gates open on the green beep at frame 192.

**New tools and engine features:**
- **`aerial.py`:** surveys each level (8 AI players for 60 s, logging item
  spawns and player paths with `MANIAC_CINE_SURVEY=1`), then renders tiled
  top-down maps with `zones all`. Output is in `build/trailer/aerial/`, with
  labelled grids in `*_grid.png`.
- **`shotgen.py`:** writes every shot file.
  - `floor()` looks up the surveyed floor height, needed in the multi-floor
    levels.
  - `open_spot()` puts staged action on a surveyed AI path point and aims
    along its travel direction, which keeps the line of sight clear.
  - Move helpers: `line`, `orbit`, `along`.
- **New shot directives:**
  - `follow` — a chase camera on a player, smoothed, optionally relative to
    their heading;
  - `zones all` — draw every zone, for aerial and high shots;
  - `shake` — camera shake;
  - `vel`, `gravity` and `from` on items — thrown and floating goods;
  - `--preview` captures — a third of the size, every second frame (12.5 fps),
    into `clips_preview/`;
  - `hold <n>` — show every n-th frame n times: a lower, steppier frame rate
    on the same 25 fps timeline. Legacy-look shots get `blur 1` and `hold 2`
    from `shotgen.py`, so the 1999 opening runs at 12.5 fps without motion
    blur, like the original.
- **`edit.py` additions:**
  - `sfx` lines: game WAVs mixed over the score, then the whole mix is
    normalised;
  - `black` slates;
  - `whip` (`hblur`) and `flash` (`fadewhite`) transitions;
  - `--out <file>`: render to another file, e.g. an excerpt from a
    shortened copy of the EDL;
  - `--preview`: 640×360 at 12.5 fps, about 1 minute to build. Preview
    captures divide `MANIAC_DOF` (a blur radius in output pixels) by 3, so
    depth-of-field shots look the same as at full size.
- **`cards.py`:** title cards "5 MALLS", "10 MANIACS" and "1 SHOPPING LIST".

**Ending:** after the first preview, the comedy tag (`t1_tag_slip`) was dropped,
so the trailer ends on the logo.

**Status:** final render done: `build/trailer/mall_maniacs_hd_trailer.mp4`.
- **Shots:** all 28 clips rendered with `render_all.sh --final` (1920×804,
  4 sub-frames at 180°) in 14 m 40 s.
- **Assembly:** `edit.py` built the trailer in 5 m 26 s.
- **Check (`edit.py --check`):** 1920×1080, 25 fps, exactly 1500 frames,
  H.264 yuv420p. Audio is AAC 48 kHz, 60.000 s, −13.9 LUFS, true peak −0.9
  dBFS. Letterbox luma is at most 20 (top) and 19 (bottom), with 16 = black.
  All hard cuts are on beats or half-beats except the deliberate frame-17
  start after the black lead-in (gate sync).
- **Still placed from the maps:** the shots were never flown by eye. The
  Orient slip (a patch of void at the lower left) and the Woodshop cart spin
  (the cart is small) are the weakest.

## Deliverable

| Property | Value |
|---|---|
| File | `build/trailer/mall_maniacs_hd_trailer.mp4` |
| Length | 60.0 s (15 bars at 60 BPM) |
| Video | 1920×1080, 25 fps, H.264 High, `yuv420p`, CRF 16, 2.39:1 letterbox (1920×804 active) |
| Audio | 48 kHz stereo AAC 320 kbit/s, integrated loudness −14 LUFS, true peak ≤ −1 dBTP |
| Look | No HUD, 180° camera motion blur (final render), in-engine bloom/SSAO/grade/vignette/DOF, fine grain |

**Native 2.39:1.** The engine's widescreen mode is Hor+ (`MANIAC_MAX_ASPECT`,
default 2.4): a wider canvas keeps the original vertical field of view and shows
more scenery at the sides. Shots are therefore rendered directly at
**1920×804** — real widescreen compositions, not a crop of a 16:9 frame — and
the edit pads them to 1080 with black bars.

**Grid:** 60 BPM in 4/4 means 1 beat = 1 s = 25 frames and 1 bar = 4 s = 100 frames.
Every cut lands on a beat, and every section change lands on a bar line, so all
edit points are integer frame numbers that are multiples of 25. 25 fps is also
exactly one frame per 40 ms game tick (see *Motion blur*).

## Beat sheet

| Bars | Time | Frames | Section | Picture | Music |
|---|---|---|---|---|---|
| 1–2 | 0–8 s | 0–199 | Cold open | Black → the ICA mall in the **1999 look** (640×480, 4:3, scaled to 1072×804 and centred in the letterbox), very slow push-in. Caption *"1999"*. | Mall PA chime motif (A–F♯–D) on celesta, low D drone, room reverb. Almost silent. |
| 3 | 8–12 s | 200–299 | Reveal | Same camera continues; a light-sweep wipe turns on the HD renderer while the 4:3 frame opens sideways to the full 2.39:1 width, revealing the scenery Hor+ adds. Caption *"Rebuilt. Now in HD."* | Timpani roll into a boom on the downbeat; Dmaj9 string swell. |
| 4–8 | 12–32 s | 300–799 | Fly-bys | One mall per bar: ICA, Aqua, Orient, Future, Woodshop. Slow crane/dolly moves, empty aisles, parked carts. Crossfades on bar lines. | Piano eighth-note arpeggios over string pad: D – Bm7 – Gmaj7 – Asus4→A – D/F♯. Chime motif in the piano melody. |
| 9–11 | 32–44 s | 800–1099 | Montage | Staged scenes (see *Montage shots*), hard cuts on beats 1 and 3. | G – A – Bm, then G – A. Low pulse on every beat, strings up an octave. |
| 12–13 | 44–52 s | 1100–1299 | Climax | One continuous fly-through across a whole mall, rising to a wide reveal on the last beat. | Em7 – A7sus4 – A7 with a reverse-cymbal swell into bar 14. |
| 14–15 | 52–60 s | 1300–1499 | Logo | Hard cut on the downbeat: a character sitting on the cash register (`cash_sit.anm`), camera slowly pulling back. The **MALL MANIACS HD** logo blooms in on beat 3; fade to black from 59.0 s. | Final Dadd9 with boom, long reverb tail; chime motif once more, high and soft. Silence by 60.0 s. |

### Fly-by shots (bars 4–8)

Each shot is 4 s plus 12 frames of handles on each side for the crossfades.
Camera height stays low (cart or eye level) with slow, eased moves (no linear
pans). Each shot has a single point of interest: an entrance, a central atrium,
an aisle vanishing point, or a ramp.

| Bar | Level | Move |
|---|---|---|
| 4 | ICA (`scene_ica`) | Low dolly along a shelf aisle toward the checkout |
| 5 | Aqua (`scene_aqua`) | Crane up from floor level to reveal the hall |
| 6 | Orient (`scene_orient`) | Lateral track past columns (parallax) |
| 7 | Future (`scene_future`) | Push-in through a doorway into a lit space |
| 8 | Woodshop (`scene_wood`) | Slow orbit around a parked cart |

### Montage shots (bars 9–11)

Each one is a configuration that never happens in normal play.

| Bar.beat | Duration | Shot file | Shot |
|---|---|---|---|
| 9.1 | 4 s | `m1_frozen_orbit` | **Frozen orbit.** Eight characters frozen mid-action (`s_throw1`, `s_oops`, `s_grab`, `s_flpick1`, `s_throw2`, `s_winner`, `s_pick1`, `s_run`) in a ring at different heights; the camera orbits 80° around them while world time is stopped. |
| 10.1 | 2 s | `m2_cart_turntable` | **Car-ad cart.** A lone shopping cart turning at 25°/s at the ICA entrance; low angle, shallow depth of field (`MANIAC_DOF`) with the background melting away. (The game has no relighting, so "dim" is left to the grade.) |
| 10.3 | 2 s | `m3_lineup` | **Line-up.** All eight characters in a row in the Aqua mall, playing `s_stand` in sync and switching to `s_clap` together at 1.0 s. |
| 11.1 | 2 s | `m4_winner_losers` | **Winner and losers.** Lateral dolly past four losers slumped on the floor (`s_loser` is a sitting pose) to the cheering winner (`s_winner`). |
| 11.3 | 2 s | `m5_midair` | **Mid-air.** A Kalles Kaviar tube (item 12) hanging in the air, slowly tumbling and in focus, with the thrower frozen in the `s_throw2` pose behind it, out of focus. |

These are first-draft framings, made without flying them (contact sheet:
`build/trailer/montage_sheet.png`). Refine them with `--freecam`, which shows
the staging live.

## Rendering: cinematic mode in the engine

This is a **port addition**, compiled only with `cmake -DMANIAC_CINEMATIC=ON`
(default OFF). With the option OFF, the binary and its behavior are unchanged.
The new logic lives in `src/cinematic.c` / `src/cinematic.h`; each hook into
existing code is a small `#ifdef MANIAC_CINEMATIC` call placed at the
architectural seam it belongs to (`cross_platform_plan.md`), so no seam moves.

Invocation: `./build-cine/maniac_rebuild --cinematic tools/trailer/shots/<shot>.txt --out build/trailer/clips/<shot>.mkv`
Add `--final` for the final render with motion blur; without it every capture is a
fast draft (one render per frame).

### Components

1. **Virtual clock** (time seam, `platformTicks()` in `src/platform_sdl2.c`).
   In cinematic mode, game time no longer comes from `SDL_GetTicks()`; the capture
   loop advances it explicitly. Rendering is offline: it doesn't matter how long
   a frame takes, and two runs of the same shot produce identical frames.
   The fixed-step sim loop (`gameRunFrame`, 40 ms ticks from `obj_update_time`) is unchanged and simply
   sees time advance.
2. **Forced render, no HUD** (`gameFrameRender` in `src/gameplay.c`). The capture
   loop renders every frame and sub-frame regardless of whether a sim tick happened, and
   `renderGameHud()` is skipped. The in-world guidance arrows also count as HUD:
   the yellow item arrow (VARUPIL, `g_pGoodsArrowObj`) and the cart/zone arrow
   (VAGNPIL, `g_pCartArrowObj`). `cinematicHideOverlays` hides them just
   before `sceneRender`, and `cinematicRestoreOverlays` restores their hidden
   flags right after, so game state is untouched; a traced AI run is identical
   with and without this. This applies to captures and freecam alike.
3. **Scripted camera** (`cinematicCameraApply` at the start of
   `gameFrameRender`, before zone culling). Each keyframe is a camera position,
   a look-at point and a vertical FOV. Interpolation is Catmull-Rom with the key
   times as knots (Barry-Goldman), so you control the timing and velocity stays
   continuous across keys. Splining a look-at point instead of angles avoids
   wrap-around and roll. `ease <in> <out>` warps time so the camera starts and
   stops from rest (smoothstep speed ramps). The pose is applied with
   `sceneObjSetPos` + `sceneNodeFacePos` on the scene root camera, exactly as
   character select places its camera. FOV sets the camera block's `nWidth`
   (tangent of the horizontal half-angle on the 4:3 base). The game's own value
   is 1.0, a vertical FOV of 73.74°, which is also the maximum: the widescreen
   culling cone assumes it. A shot without keys keeps the game's follow camera.
4. **Free-fly camera and keyframe recorder** (`--freecam`; input is read in
   `platform_sdl2.c` and passed through `platformCineInput`). The shot warms up,
   then the camera takes over from the game camera (or from the shot's first
   key). Controls:

   | Input | Action |
   |---|---|
   | Mouse | Look (cursor is captured) |
   | W A S D | Move in the ground plane |
   | E / Space, Q / Ctrl | Up, down |
   | Shift, mouse wheel | 4× speed, scale base speed (3000 units/s) |
   | Z / X | Narrow / widen FOV |
   | K | Append the current pose as a `key` line to the shot file |
   | Enter | Preview the keyed path from its first key (Enter again stops) |
   | T | Freeze / unfreeze game time |
   | G | Toggle guides: grey rule of thirds, yellow centred 4:3 region |
   | P | Print the current pose to the terminal |
   | Esc | Quit |

   Freecam runs on wall-clock time. Key times record the rhythm you fly: the
   first key of an empty shot is at 0 s, and each later key is the previous key
   time plus the seconds since the last K press. When the shot already had keys,
   the first new key comes 2 s after the last one. Edit the times in the file
   to retime a path. The window opens at 1280 px wide with the shot's aspect
   (1280×536), so the framing matches the capture.
5. **Capture** (graphics seam, `src/gx_sdl2_gl.c`). The window is created at the
   capture size (1920×804) and can stay hidden. At `gxFlip`, after the post
   stack has composited the frame, the backbuffer is read with `glReadPixels`.
   Anti-aliasing uses the existing knobs: `MANIAC_RENDER_SCALE=2` (supersampling)
   plus `MANIAC_MSAA`. Frames are piped (`popen`) into
   `ffmpeg -f rawvideo -pix_fmt rgb24 -s 1920x804 -r 24 -i - -vf vflip -c:v ffv1`,
   producing lossless clips.
6. **Frame timing and motion blur** (capture loop). The sim moves objects once
   per 40 ms tick (25 Hz, `obj_update_time` in maniac.cfg), so the trailer runs
   at **25 fps: exactly one tick per frame**. After the round starts, the capture
   reads the tick phase (`g_nGameTime` against `getGameTime()`) and places every
   frame half a tick after a tick boundary. Moving objects therefore advance by
   exactly one tick per frame, with no beat between sim and video rate.
   - **Draft** (default): one render per frame, about 8× cheaper. Motion is
     already even; fast camera moves just look a bit choppy, like a screen
     recording.
   - **Final** (`--final`): each frame averages `blur` sub-frames (default 4),
     spread over a `shutter` angle (default 180° = 20 ms) centred on the frame
     time. Game and camera time both advance per sub-frame. The shutter is
     shorter than a tick and centred mid-tick, so no sub-frame window crosses a
     tick: characters stay sharp single images and only camera motion blurs.

   Shot time t is frame t × 25 in both modes, so drafts and finals line up.
   Smoother character motion (50/60 fps) would need render-time interpolation
   of object transforms. That is a separate HD feature, not part of this plan.
7. **World-time control.** Per shot, `world_speed` scales the virtual clock seen by
   the sim (`1` normal, `0` frozen) independently of camera time. Slow motion
   doesn't go through the sim (it would judder at 25 Hz). Instead, staged
   characters are posed by scrubbing animations with `sceneObjectAnimStepLerp`
   at an explicit phase.
8. **Staging** (`cinematicSceneApply` at the start of `gameFrameRender`).
   - **Actors are the round's own players.** `players` raises the count to
     up to 8; every level config has 8 `start_positions`. `cast` picks each
     slot's character. With a cast, the round starts with `stateLevelInitN`'s
     own steps, with the cast applied in between: `playerSetupCharacters`,
     the cast, `unloadGameWorld`, then `run <level>`.
   - **Freeze the world.** Staged shots use `world_speed 0`, so no sim tick
     moves anything.
   - **Placement and pose every frame.** Each actor's `pCharSceneNode` is
     placed with `sceneObjSetPos` and turned with `sceneNodeFacePos` (yaw
     0 = facing +z). Its animation set is posed at frame f + fps × t: the
     track cursor is set to the integer frame, and `sceneObjectAnimStepLerp`
     blends to the next keyframe. Poses are therefore exact, scrubbable and
     smooth at any frame rate.
   - **Animations.** The 11 sets gameplay already loads are reused. Others
     (`s_loser`, `s_clap`, `cash_sit`, `s_stand2`, …) are loaded per actor
     the way `playerSetupRound` loads its own: `anmLoadFile` →
     `anmSetAlloc` → `anmSetMeshSlot` for the 3 LOD meshes. `pose` switches
     animation at a shot time.
   - **Props.** Carts are the players' own `pCartSceneObj`. Items are
     `sceneryObjAlloc` objects built from a level item slot's mesh (ids
     1–30 from maniac.cfg `items[]`, e.g. 12 Abba Kalles Kaviar,
     16 Felix Tomatketchup). Item angles use the game's 65536-per-turn units.
   - **Hiding.** Hidden actors and carts join the guidance arrows in the
     hide/restore bracket around `sceneRender`, which saves and restores
     each subtree's hidden byte.
9. **Legacy look** (graphics seam, `env MANIAC_LEGACY_LOOK 1` plus
   `size 640 480`). An approximation of the 1999 `gxSoft` software renderer;
   one switch in `gx_sdl2_gl.c` undoes every HD addition:
   - affine (screen-linear) texture mapping (vertex w = 1);
   - GXSOFT's far-first painter order without the depth buffer;
   - nearest-neighbour texture sampling;
   - no effects stack, alpha-to-coverage or MSAA;
   - the 4:3 canvas (no widescreen).

   There is no lighting to switch off: textured triangles are already drawn
   untinted, like GXSOFT. Example shots: `tools/trailer/shots/t5_legacy_ica.txt`
   and its HD twin `t5_hd_ica.txt`. The edit nearest-upscales the result to
   1072×804, centred in the letterbox. Because widescreen is Hor+ with the same
   vertical field of view, the legacy frame lines up exactly with the centre of
   the HD frame from the same camera. That is what makes the reveal's widening
   work without a visible jump.
10. **Post-processing per shot.** Each shot is rendered in its own process, so
    the `MANIAC_*` post-processing knobs (read once at startup) are set per shot
    from `env` lines in the shot file: bloom, SSAO, a `.cube` grade LUT shared
    by every shot (`MANIAC_GRADE_LUT`), vignette, and depth of field
    (`MANIAC_DOF`, `_FOCUS`, `_RANGE`). Static focus is enough for the current
    shot list. A focus pull would need a small runtime setter, added only if a
    shot calls for one.
11. **Title cards** (`tools/trailer/cards.py`; Python + numpy + Pillow, not
    the engine). The planned `--card` engine mode would draw through the
    640×480 UI canvas and resample the 256-px font texture twice. The script
    instead decodes the game's own `.tpg` art directly, the way
    `tests/render_font.c` does, and writes 1920×1080 transparent PNG overlays
    into `build/trailer/cards/`:
    - **Captions** `caption_1999.png` (×3) and `caption_rebuilt.png`
      ("REBUILT. NOW IN HD.", ×2), set in the menu font `MFONT00.TPG` with
      `menyfont.txt` metrics. They are tinted warm white with a soft dark
      outline and sit at 80% height of the picture band.
    - **Logo** `logo.png`: the menu's own **MALL MANIACS** sign
      (`SIGN100` + `SIGN200`, 512×256 → 1024×512) keyed off black with a
      solid mask. The pole fades into the dark, and the edges the texture
      crops (left tip, top rim) are feathered. An **HD** badge in the orange
      menu font (×4.5, dark outline) sits under "MANIACS". No URL or other
      closing line appears in the video.
    - **Glow** `logo_glow.png`: a bright, wide blur of the logo that the
      edit screens underneath and animates for the bloom-in.
    - **Previews** `preview_*.png`: over black, and over a footage frame
      (`--footage`).

    Font quirks: `menyfont.txt` says `pos0 = 29`, but in `MFONT00.TPG` the
    digits sit in cells 30–39 (cell 29 is empty), so the script uses the
    atlas cells. The game would draw `1` as `0` with this font. A card-only
    kerning tweak pulls letters in after the italic `T`.

### Shot file format (`tools/trailer/shots/*.txt`)

```
shot      flyby_ica
level     0                  # 0 ICA, 1 Woodshop, 2 Orient, 3 Aqua, 4 Future
mode      varujakten         # game type: varujakten, matkrig, frogesport, vagnrace
character 0                  # local player's character index
start     8.0                # warm-up game seconds before the first captured frame
frames    125                # 100 + 2 × 12 handle frames, rounded up
size      1920 804           # capture size (default)
world_speed 1
env       MANIAC_RENDER_SCALE 2
env       MANIAC_GRADE_LUT tools/trailer/grade.cube
env       MANIAC_VIGNETTE 0.25
ease      0.8 0.8            # seconds of ease-in / ease-out
blur      4                  # sub-frames per frame in --final renders (default 4)
shutter   180                # degrees (default 180 = 20 ms, inside one tick)
#   t      position (x y z)     look-at (x y z)     vertical FOV
key 0.0    1500  -900 11500    1500  -900  8000    73.7
key 1.5    1300 -1100  8800    1600 -1000  5300
key 4.0    1500 -1500  5500    1500 -1100  2000    60
players   2                  # staging: player count (up to 8)
cast      4 0                # character per slot
actor 0   400 0 6900  180  anim s_throw1 frame 6 fps 0   # frozen pose
actor 1   1600 0 7300  180  anim s_stand fps 12.5         # live animation...
pose  1   1.0  s_clap                                     # ...switching at 1.0 s
cart 0    hide
cart 1    900 0 7200   30  spin 15                        # degrees per second
item 12   1000 -1500 7000  rot 200 30 0  spin 12 8 0      # Kalles Kaviar, tumbling
```

The round is started the way the menus start it: the game type's `modeInit*`
function, `stateCharSelectOk` with the chosen character, then
`stateLevelInitN`, which runs `playerSetupCharacters` and `run <level>`. With
no keyboard input the local player stands still and the AI players play
normally. The capture window must be on a real display (`DISPLAY=:0`); it
ignores keys and focus changes, and closing it aborts the shot.

Staging lines are only used in montage shots; the full grammar is in the
header comment of `src/cinematic.c`. Coordinates are game
world units; **up is −y** (floor at y = 0, eye height about −900, the game's
follow camera about −2000). Keyframe times are seconds of shot time (0 is the
first captured frame), including the handles. Before the first key and after
the last one, the camera holds that key's pose. FOV is optional (default 73.7).

`MANIAC_CINE_TRACE=1` prints the camera pose of every captured frame to stderr
(`[trace] t x y z ax ay az fov`), for checking a path numerically.
`env` lines are exported before the engine starts; the legacy half of the reveal
is a copy of the cold-open shot file with the legacy-look `env` lines and a
640×480 capture size (`size 640 480`).

## Music: synthesized score

`tools/trailer/music.py` uses Python 3 + numpy (both already installed) and the
stdlib `wave` module. It writes `build/trailer/music.wav` (48 kHz, 16-bit, stereo,
exactly 60.0 s), `build/trailer/music_markers.txt` (bar and beat times with video
frame numbers, used by the edit as a check) and
`build/trailer/music_spectrogram.png` (log-frequency spectrogram with bar lines,
for review). It also prints the peak and the RMS level of each bar. Fixed random
seed, so output is deterministic. No samples and no external synths: every
sound is generated from formulas. A full render takes about 15 s.

### Instruments

| Voice | Synthesis |
|---|---|
| Celesta / PA chime | 2-operator FM (ratio 1:3.5), fast attack, ~2 s exponential decay. The mall PA "ding-dong" motif A–F♯–D is the trailer's theme. |
| Piano | Additive: 10 partials with slight inharmonicity (B ≈ 0.0004), per-partial decay (higher partials decay faster), a short noise "hammer" transient, velocity-dependent brightness. |
| String pad | 7 detuned sawtooth voices per note (±8 cents, random phase), slow attack/release. Low-pass is applied in the frequency domain (numpy FFT; no scipy), and the filter swells by crossfading between darker and brighter renders. |
| Low drone | Sine + soft-saturated octave below the tonic. |
| Timpani / boom | Sine with a downward pitch envelope (110 → 55 Hz) plus a band-limited noise burst; rolls are retriggered hits with rising velocity. |
| Pulse | Short low tom (pitched-down sine with click) on each beat in the montage. |
| Reverse cymbal | Bright noise with a reversed exponential envelope into bar 14. |

### Mix

- **Reverb:** FFT convolution with a synthetic stereo impulse response
  (exponentially decaying decorrelated noise, 3.5 s, darkening over time). Two
  sends: short for the cold open (empty-mall room), long for everything else.
- **Panning:** constant-power. Piano slightly left, celesta right, pad wide, low end centered.
- **Master:** 25 Hz high-pass, a gentle look-ahead compressor (−18 dB threshold,
  2.5:1, envelope from a zero-phase low-pass of the signal power), `tanh` soft
  clip, raised-cosine fade from 59.0 s, peak normalization to −1 dBFS, TPDF
  dither. The render measures about −13 LUFS; loudness is finalized to −14 LUFS
  by `ffmpeg loudnorm` in the edit.
- **Structure:** follows the beat sheet bar by bar; harmony is in D major. The
  score (chords, melody, celesta cues) is data tables at the top of the
  *Score* section of `music.py`.
- **Level arc** (RMS per bar, first render): cold open −24 → reveal −12.5 →
  fly-bys −16…−17.5 → montage −13…−15 → climax −13 → logo −11 → tail −16 dBFS.

`music.py --preview 8-16` additionally writes `music_preview.wav` with just that
range of seconds, for quick listening.

## Editing: `tools/trailer/edit.py` + ffmpeg

The edit is described in `tools/trailer/edit.txt`, an edit decision list
measured in frames at 25 fps. `edit.py` validates it, turns it into a single
`ffmpeg -filter_complex` command and runs it. The full grammar is in
`edit.py`'s docstring. An excerpt of the actual list:

```
frames 1500
audio  build/trailer/music.wav
video 0     c1_cold_legacy   0   cut   legacy      # 1999 look, 4:3 centred
video 200   c1_cold_hd     200   wipe 12  hdbox    # HD replaces the 4:3 picture
video 212   c1_cold_hd     212   open 38           # ...then the frame opens to 2.39:1
video 300   f1_ica          12   fade 12           # fly-bys crossfade on bar lines
...
video 800   m1_frozen_orbit  0   cut               # montage: hard cuts on beats
...
video 1300  l1_cashier       0   cut
overlay 25   175   caption_1999.png     fade 12 12
overlay 212  290   caption_rebuilt.png  fade 12 12
darken  1325 1500  0.6                  fade 25
glow    1350 1500  logo_glow.png        fade 25
overlay 1350 1500  logo.png             fade 25
fadeout 1475 25
```

`video <start> <clip> <in> <transition>`: a shot starts at timeline frame
`start`, reading `build/trailer/clips/<clip>.mkv` from frame `in`. A
transition of N frames plays from `start` to `start + N`, so the outgoing
shot must cover those N frames too; the fly-bys' 12-frame handles exist for
this. `edit.py` checks every shot's frame range against the clip, and checks
that every hard cut lands on a multiple of 25 frames. It refuses to render
otherwise.

How the graph is built (stock ffmpeg filters only):
- **Shots.** Each shot is `trim`med to its exact frames and normalised to
  1920×804 `yuv444p`. `legacy` shots are nearest-upscaled to 1072×804 and
  pillarboxed. `hdbox` crops an HD shot to the same centred 4:3 box.
- **Joins.** Cuts use `concat`. Transitions use `xfade` at the timeline
  offset: `fade`, `wipe` (`wiperight`), `open` (`horzopen`) and `black`
  (`fadeblack`).
- **The reveal.** It is three entries:
  1. the legacy shot;
  2. a 12-frame `wipe` to the HD shot boxed to 4:3;
  3. a 38-frame `open` to the full-width HD shot.

  Because the two shots line up exactly (T5), the second step reads as the
  picture "upgrading" and the third as the frame widening.
- **Look.** `noise=alls=3:allf=t` adds grain on the picture, then `pad`
  adds the letterbox. The grade is done in-engine.
- **Cards.** The card PNGs are looped stills, faded with `fade=alpha=1` and
  composited with `overlay`.
- **Darken.** A black `color` source at 60%, faded in under the logo.
- **Glow.** Screen-blended (`blend` expression `255-(255-A)(255-kB)/255`),
  with strength k ramped over the fade.
- **Bars.** Re-blackened with `drawbox` after all overlays, because the glow
  spills into them.
- **Audio.** Two-pass `loudnorm` (measure, then linear) to −14 LUFS / −1
  dBTP, then `afade` out from 59.0 s.
- **Output.** `libx264 -crf 16 -preset slow -pix_fmt yuv420p`, AAC 320k,
  `-movflags +faststart`.

`tools/trailer/render_all.sh [--final]` renders every clip the list uses.

`edit.py --preview` renders at 960×540 with `-preset veryfast` for fast
iteration (about 5 minutes on this VM). `edit.py --contact-sheet` writes one
still per shot (`select` + `tile`) to `build/trailer/contact_sheet.png`.
`edit.py --check` measures the rendered file for the T8 gate: size, rate,
frame count, loudness, true peak, letterbox luma and cut positions.

## File layout

```
tools/trailer/
  music.py          score synthesizer
  edit.py           EDL → ffmpeg
  edit.txt          the edit
  shots/*.txt       camera keyframes and staging, one file per shot
  grade.cube        shared colour grade LUT (generated by a small script)
  cards.py          title cards and logo overlays from the game's own art
  render_all.sh     renders every shot listed in edit.txt
src/cinematic.c/.h  cinematic mode (MANIAC_CINEMATIC only)
build/trailer/      outputs (clips/, music.wav, contact_sheet.png, final mp4) — not committed
```

## Prerequisites

- `ffmpeg` with `libx264`, `ffv1` and `aac` (installed: 6.1.1, Ubuntu package;
  all filters used below are present).
- Python 3.12 + numpy 1.26 and Pillow 10.2 (already installed; Pillow is used only for
  the contact sheet annotations and as a fallback for the logo).
- A separate build directory for the cinematic option:
  `cmake -S . -B build-cine -DMANIAC_CINEMATIC=ON && cmake --build build-cine`.

## Phases and verification gates

Each phase ends with a verification gate that must pass before the next phase starts.

**Status:** T0–T8 done; a complete draft trailer exists. T2 and T3 were measured at 24 fps, before the switch to 25.
- **T2:** `tools/trailer/shots/t2_followcam.txt` (ICA, 48 frames from 8 s); two
  runs gave identical `framemd5` for all 48 frames.
- **T3:** `tools/trailer/shots/t3_test_dolly.txt`, a 4 s keyed dolly down the
  ICA aisle with uneven key spacing and ease. Traced speed across the key at
  1.5 s is 2169 → 2145 → 2118 → 2091 units/s (no step). Speed ramps from 6
  units/s at the start and down to 30 at the end. Two runs were identical for
  all 96 frames. The test path flies through the idle player (frames 26–31);
  that is content, not a hitch. Freecam was checked with xdotool: strafe,
  mouse-look and up directions, key recording with wall-clock timing, Enter
  preview, guides. The real fly-by framing is still to be flown.

- **T4:** 25 fps, tick-aligned. Traced on an AI player, every frame of a
  50-frame shot advances `g_nGameTime` by exactly 40 ms, in both draft and
  `--final`. In final renders, all 4 sub-frames of every frame share one tick
  and one position, so characters stay sharp. Draft and final renders of the
  keyed dolly were each identical across two runs (100 frames). A final frame
  shows soft camera blur on near railings and signs. An earlier 24 fps version
  with a one-tick (345.6°) shutter worked, but blended two positions per frame;
  it was replaced by this.
- **T5:** `t5_legacy_ica` (640×480, legacy look) compared with a Wine
  screenshot of the original `maniac.exe` at the same moment of an ICA round
  (Spela → Varujakten → first character → first level, about 2 s in). Saved
  as `build/trailer/t5_original_wine.png`, `t5_legacy.png` and `t5_hd.png`.
  - **Matches:** camera framing, the gate and shelf geometry, the affine
    floor-tile warp and the nearest-texel blockiness. The shelf region differs
    only where the random AI shopper stands.
  - **Expected differences:** the original's HUD and item arrow (the arrow is
    hidden as HUD in captures since; the saved `t5_legacy.png` is re-rendered
    without it), plus GXSOFT's occasional single-pixel seam cracks, which GL does
    not reproduce.
  - **Reveal alignment:** the legacy frame scaled to 1072×804 matches the
    centre of the 1920×804 HD frame from the same shot at zero offset
    (edge-map correlation). Both clips share deterministic game state, so the
    AI characters line up too.
  - **Determinism:** two legacy runs were identical for all 25 frames.
- **T6:** staging directives (`players`, `cast`, `actor`, `pose`, `cart`,
  `item`) and the five montage drafts `m1`–`m5` (see *Montage shots*).
  - **Verified on a test shot:** yaw facing, frozen and live poses, a mid-shot
    switch to a non-gameplay animation (`s_clap`), hidden actors and carts,
    and a spinning item prop.
  - **Draft fixes:** `s_loser` is a sitting pose, so losers sit on the floor
    (y +1000). The item needed a 180° turn to show its label. The Aqua
    line-up camera was moved off a floor-clipping spot; a scan confirmed no
    black pixels in any frame.
  - **Determinism:** two runs of `m4` were identical, with 50 distinct
    frames. The unstaged `t2_followcam` trace is unchanged.
- **T7:** `cards.py` renders both captions, the logo and its glow from the
  game's own sign and menu font (see *Title cards*).
  - **Black background:** the logo blooms cleanly.
  - **Footage background:** captions stay legible over the bright mall
    thanks to the outline. The logo needs the footage darkened behind it,
    which is noted for T8.
  - **Fixes during review:** feathered the sign's cropped left and top
    edges, kerned the gap after `T`, and placed digits by the atlas, since
    `pos0 = 29` drew "1999" as "(888". The URL line was removed on request.
  - **Output:** deterministic (identical `logo.png` across runs), and the
    logo stays inside the picture band.
- **T8:** `edit.py`, `edit.txt` and `render_all.sh`, plus draft shots for
  every slot: `c1_cold_legacy`/`c1_cold_hd`, `f1`–`f5`, `k1_climax` and
  `l1_cashier`. The fly-by paths start from each level's follow-camera pose
  (traced) and were adjusted until no shot starts inside gate rails or a
  pillar.
  - **Render:** `build/trailer/mall_maniacs_hd_trailer.mp4` takes about 6
    minutes at full quality; the preview takes about 5.
  - **Gate (`--check`):** 1920×1080, 25 fps, exactly 1500 frames, H.264
    yuv420p. Audio is AAC 48 kHz, 60.000 s, −13.9 LUFS (target −14 ± 0.5),
    true peak −1.9 dBFS. All 8 hard cuts land on multiples of 25. Letterbox
    luma is at most 22 (top) and 19 (bottom), with 16 = black: encoder
    ringing at the band edge. The bars are re-blackened after the glow,
    which first spilled into them.
  - **Not final:** the clips are drafts (no motion blur; run
    `render_all.sh --final`), and the framings are first drafts made without
    flying them. Refining them with `--freecam` is T9.

Directives not implemented yet (`actor`, `prop`) are reported and ignored.

| # | Phase | Gate |
|---|---|---|
| T0 | Tooling | `ffmpeg -version` works; `build-cine` configures and builds; `ctest` passes in both `build` and `build-cine`. |
| T1 | Music | `music.wav` is 60.00 s ± 1 ms, peak ≤ −1 dBFS, bar markers on 4 s boundaries. User listens and approves (iterate on the score here, before any footage exists). |
| T2 | Virtual clock, forced render, HUD off, capture | 2 s of normal gameplay captured from the follow camera with no HUD; two runs give byte-identical clips (`ffmpeg -f framemd5`). |
| T3 | Free camera, keyframes, spline | ICA fly-by renders; camera speed is continuous with no visible hitch at keyframes (check by stepping through frames). |
| T4 | Frame timing, motion blur | Every captured frame advances exactly one sim tick; final sub-frames never straddle a tick (characters sharp); still frames remain sharp. |
| T5 | Legacy look | Legacy frame compared against a Wine screenshot of the original from the same position (`XDOTOOL_NAVIGATION.md`): same aliasing, texture swimming and lighting character. The legacy frame, scaled to 1072×804, matches the centre of the HD frame from the same camera (geometry lines up when the two are overlaid). |
| T6 | Staging | Every montage shot renders as described: frozen poses, synced line-up, item hanging mid-air. |
| T7 | Title cards and logo | Cards render in the game's menu font; logo bloom reads well over both black and footage. |
| T8 | Edit | Final file is 1920×1080, 25 fps, exactly 1500 frames, −14 LUFS ± 0.5; every cut is on a multiple of 25 frames; 138 px letterbox bars throughout (plus side bars during the 4:3 legacy section). |
| T9 | Review | Contact sheet and full preview to the user; revise shots or music, then render the final file. |

## Open decisions

- **Logo design.** Default chosen in T7: the game's own MALL MANIACS menu sign,
  plus an HD badge in the menu font. A new wordmark remains possible.
- **Closing line** under the logo: none. The GitHub URL is deliberately left
  out of the video.
- **Game sound effects.** Off by default; optionally 2–3 accents from `sound/`
  (e.g. `09_Flag.wav` on a montage cut) mixed under the score.
- **Character for the final shot** on the cash register.
