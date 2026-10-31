#!/usr/bin/env python3
"""Assemble the Mall Maniacs HD trailer with ffmpeg (Trailer.md, phase T8).

Reads an edit decision list (default tools/trailer/edit.txt), builds one
ffmpeg -filter_complex graph and renders build/trailer/mall_maniacs_hd_trailer.mp4.

EDL (one directive per line, '#' starts a comment; times are frames at 25 fps):
  frames <n>                         timeline length (1500 = 60 s)
  audio <wav>                        score
  video <start> <clip> <in> [cut | fade N | wipe N | open N | black N | whip N | flash N]
        [legacy | hdbox]
      cut to / transition into <clip> (build/trailer/clips/<clip>.mkv) at
      timeline frame <start>, reading from its frame <in>. A transition of N
      frames plays from <start> to <start>+N, so the previous shot must
      cover those frames too. legacy: a 640x480 1999-look clip, nearest-
      upscaled to 1072x804 and centred; hdbox: an HD clip cropped to the
      same centred 4:3 box (for the reveal). The clip name "black" is a
      black slate (no file, <in> ignored).
  overlay <start> <end> <png> [fade <in> <out>]    full-frame RGBA card (build/trailer/cards/)
  glow <start> <end> <png> [fade <in>]             screen-blended glow under the logo
  darken <start> <end> <amount> [fade <in>]        black over the picture band
  sfx <frame> <wav> [gain_dB]                      a game sound (MM_DATA_DIR/sound/<wav>)
  fadeout <start> <frames>                         final fade to black (picture and sound)

The score and the sound effects are mixed first (build/trailer/mix.wav), then
the mix is loudness-normalised to -14 LUFS / -1 dBTP with two-pass loudnorm.

--preview renders build/trailer/mall_maniacs_hd_trailer_preview.mp4 at
640x360 and 12.5 fps from the --preview clips in build/trailer/clips_preview/
(every second frame of each shot), so EDL frame numbers stay in 25 fps units.

Usage:
  tools/trailer/edit.py [--edl FILE] [--preview] [--contact-sheet] [--check] [--dry-run]
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys

FPS = 25
W, BAND_H, H = 1920, 804, 1080
SOUND_DIR = os.path.join(os.environ.get("MM_DATA_DIR", "/home/wasd/MallManiacsUnmodified"), "sound")
CARDS = "build/trailer/cards"
MIX = "build/trailer/mix.wav"
TRANSITIONS = {"fade": "fade", "wipe": "wiperight", "open": "horzopen", "black": "fadeblack",
               "whip": "hblur", "flash": "fadewhite"}


class Format:
    """Output geometry: full 1920x1080 @ 25 fps, or the 640x360 @ 12.5 fps preview."""

    def __init__(self, preview):
        self.preview = preview
        self.div = 3 if preview else 1
        self.fps = FPS / 2 if preview else FPS
        self.w, self.band_h, self.h = W // self.div, BAND_H // self.div, H // self.div
        self.band_top = (self.h - self.band_h) // 2
        self.legacy_w = round(self.band_h * 4 / 3)
        self.clips = "build/trailer/clips_preview" if preview else "build/trailer/clips"
        self.out = ("build/trailer/mall_maniacs_hd_trailer_preview.mp4" if preview
                    else "build/trailer/mall_maniacs_hd_trailer.mp4")

    def clip_frame(self, frame):
        """EDL frame (25 fps) -> frame index in this format's clips."""
        return frame // 2 if self.preview else frame


class Edl:
    def __init__(self):
        self.frames = 1500
        self.audio = None
        self.video = []      # dicts: start, clip, in, trans, dur, mode
        self.overlays = []   # dicts: kind, start, end, arg, fin, fout
        self.sfx = []        # (frame, wav, gain_db)
        self.fadeout = None


def parse(path):
    edl = Edl()
    for n, raw in enumerate(open(path), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        tok = line.split()
        try:
            if tok[0] == "frames":
                edl.frames = int(tok[1])
            elif tok[0] == "audio":
                edl.audio = tok[1]
            elif tok[0] == "video":
                v = {"start": int(tok[1]), "clip": tok[2], "in": int(tok[3]) if len(tok) > 3 and
                     tok[3].lstrip("-").isdigit() else 0, "trans": "cut", "dur": 0, "mode": "normal"}
                rest = tok[4:] if len(tok) > 3 and tok[3].lstrip("-").isdigit() else tok[3:]
                while rest:
                    t = rest.pop(0)
                    if t == "cut":
                        v["trans"] = "cut"
                    elif t in TRANSITIONS:
                        v["trans"], v["dur"] = t, int(rest.pop(0))
                    elif t in ("legacy", "hdbox"):
                        v["mode"] = t
                    else:
                        raise ValueError(f"unknown token {t!r}")
                edl.video.append(v)
            elif tok[0] in ("overlay", "glow", "darken"):
                o = {"kind": tok[0], "start": int(tok[1]), "end": int(tok[2]), "arg": tok[3],
                     "fin": 0, "fout": 0}
                rest = tok[4:]
                if rest[:1] == ["fade"]:
                    o["fin"] = int(rest[1])
                    o["fout"] = int(rest[2]) if len(rest) > 2 else 0
                edl.overlays.append(o)
            elif tok[0] == "sfx":
                edl.sfx.append((int(tok[1]), tok[2], float(tok[3]) if len(tok) > 3 else 0.0))
            elif tok[0] == "fadeout":
                edl.fadeout = (int(tok[1]), int(tok[2]))
            else:
                raise ValueError(f"unknown directive {tok[0]!r}")
        except (IndexError, ValueError) as e:
            sys.exit(f"{path}:{n}: {e}")
    edl.video.sort(key=lambda v: v["start"])
    return edl


def clip_frames(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-count_packets", "-select_streams", "v:0",
                          "-show_entries", "stream=nb_read_packets", "-of", "csv=p=0", path],
                         capture_output=True, text=True, check=True).stdout.strip()
    return int(out)


def validate(edl, fmt):
    """Every shot must cover its span plus the next transition, and the
    timeline starts at 0. Hard cuts on whole beats are reported, not
    enforced (fast montages cut on half beats)."""
    errors = []
    if not edl.video or edl.video[0]["start"] != 0:
        errors.append("the first video entry must start at frame 0")
    for j, v in enumerate(edl.video):
        nxt = edl.video[j + 1] if j + 1 < len(edl.video) else None
        end = nxt["start"] + nxt["dur"] if nxt else edl.frames
        v["len"] = end - v["start"]
        if v["clip"] == "black":
            continue
        path = os.path.join(fmt.clips, v["clip"] + ".mkv")
        if not os.path.exists(path):
            errors.append(f"frame {v['start']}: clip {path} missing")
            continue
        have = clip_frames(path)
        need = fmt.clip_frame(v["in"] + v["len"] - 1) + 1
        if need > have:
            errors.append(f"frame {v['start']}: {v['clip']} needs frames up to {need - 1} "
                          f"({'preview ' if fmt.preview else ''}clip has {have})")
    for f, wav, _ in edl.sfx:
        if not os.path.exists(os.path.join(SOUND_DIR, wav)):
            errors.append(f"sfx at {f}: {wav} not found in {SOUND_DIR}")
    return errors


def build_graph(edl, fmt):
    inputs, chains = [], []
    dur = edl.frames / FPS

    def add_input(args):
        inputs.extend(args)
        return sum(1 for a in inputs if a == "-i") - 1

    # Shots, normalised to the picture band.
    labels = []
    for j, v in enumerate(edl.video):
        if v["clip"] == "black":
            chains.append(f"color=c=black:s={fmt.w}x{fmt.band_h}:r={fmt.fps}:d={v['len'] / FPS},"
                          f"format=yuv444p,settb=AVTB[s{j}]")
            labels.append(f"s{j}")
            continue
        k = add_input(["-i", os.path.join(fmt.clips, v["clip"] + ".mkv")])
        a, b = fmt.clip_frame(v["in"]), fmt.clip_frame(v["in"] + v["len"])
        f = [f"trim=start_frame={a}:end_frame={b}", "setpts=PTS-STARTPTS"]
        if v["mode"] == "legacy":
            f += [f"scale={fmt.legacy_w}:{fmt.band_h}:flags=neighbor", f"pad={fmt.w}:{fmt.band_h}:(ow-iw)/2:0"]
        elif v["mode"] == "hdbox":
            f += [f"scale={fmt.w}:{fmt.band_h}", f"crop={fmt.legacy_w}:{fmt.band_h}:(iw-{fmt.legacy_w})/2:0",
                  f"pad={fmt.w}:{fmt.band_h}:(ow-iw)/2:0"]
        else:
            f += [f"scale={fmt.w}:{fmt.band_h}"]
        # One timebase for every segment, so concat and xfade outputs can be chained.
        f += ["setsar=1", f"fps={fmt.fps}", "format=yuv444p", "settb=AVTB"]
        chains.append(f"[{k}:v]{','.join(f)}[s{j}]")
        labels.append(f"s{j}")

    # Join: cuts concatenate, transitions use xfade at the timeline offset.
    cur = labels[0]
    for j in range(1, len(edl.video)):
        v = edl.video[j]
        out = f"j{j}"
        if v["dur"] == 0:
            chains.append(f"[{cur}][{labels[j]}]concat=n=2:v=1:a=0,settb=AVTB[{out}]")
        else:
            chains.append(f"[{cur}][{labels[j]}]xfade=transition={TRANSITIONS[v['trans']]}:"
                          f"duration={v['dur'] / FPS}:offset={v['start'] / FPS},settb=AVTB[{out}]")
        cur = out

    noise = 3 if not fmt.preview else 0
    chains.append(f"[{cur}]" + (f"noise=alls={noise}:allf=t," if noise else "") +
                  f"pad={fmt.w}:{fmt.h}:0:{fmt.band_top}:black,format=yuv444p[base]")
    cur = "base"

    for i, o in enumerate(edl.overlays):
        t0, t1 = o["start"] / FPS, o["end"] / FPS
        enable = f"enable='between(t,{t0},{t1})'"
        out = f"o{i}"
        if o["kind"] == "darken":
            fin = f",fade=in:st={t0}:d={max(o['fin'], 1) / FPS}:alpha=1"
            chains.append(f"color=c=black:s={fmt.w}x{fmt.band_h}:r={fmt.fps}:d={dur},format=rgba,"
                          f"colorchannelmixer=aa={float(o['arg'])}{fin}[d{i}]")
            chains.append(f"[{cur}][d{i}]overlay=0:{fmt.band_top}:{enable}:format=auto[{out}]")
        elif o["kind"] == "glow":
            k = add_input(["-loop", "1", "-framerate", str(fmt.fps), "-t", str(dur),
                           "-i", os.path.join(CARDS, o["arg"])])
            ramp = f"clip((T-{t0})/{max(o['fin'], 1) / FPS},0,1)*between(T,{t0},{t1})"
            chains.append(f"[{k}:v]scale={fmt.w}:{fmt.h},format=yuv444p[g{i}]")
            # Screen blend on luma, chroma pulled toward the glow by its strength.
            chains.append(f"[{cur}][g{i}]blend=c0_expr='255-(255-A)*(255-B*{ramp})/255':"
                          f"c1_expr='A+(B-128)*{ramp}*0.5':c2_expr='A+(B-128)*{ramp}*0.5'[{out}]")
        else:
            k = add_input(["-loop", "1", "-framerate", str(fmt.fps), "-t", str(dur),
                           "-i", os.path.join(CARDS, o["arg"])])
            f = [f"scale={fmt.w}:{fmt.h}", "format=rgba"]
            if o["fin"]:
                f.append(f"fade=in:st={t0}:d={o['fin'] / FPS}:alpha=1")
            if o["fout"]:
                f.append(f"fade=out:st={t1 - o['fout'] / FPS}:d={o['fout'] / FPS}:alpha=1")
            chains.append(f"[{k}:v]{','.join(f)}[c{i}]")
            chains.append(f"[{cur}][c{i}]overlay=0:0:{enable}:format=auto[{out}]")
        cur = out

    # Glow and overlays may spill outside the picture band; keep the bars black.
    tail = [f"drawbox=x=0:y=0:w={fmt.w}:h={fmt.band_top}:color=black:t=fill",
            f"drawbox=x=0:y={fmt.band_top + fmt.band_h}:w={fmt.w}:h={fmt.h - fmt.band_top - fmt.band_h}"
            ":color=black:t=fill"]
    if edl.fadeout:
        tail.append(f"fade=out:st={edl.fadeout[0] / FPS}:d={edl.fadeout[1] / FPS}")
    tail.append("format=yuv420p")
    chains.append(f"[{cur}]{','.join(tail)}[vout]")
    return inputs, chains


def mix_audio(edl):
    """Score + sound effects -> MIX (48 kHz stereo float), no normalisation."""
    dur = edl.frames / FPS
    cmd = ["ffmpeg", "-y", "-v", "error", "-i", edl.audio]
    chains = ["[0:a]aformat=sample_rates=48000:channel_layouts=stereo[m]"]
    labels = ["[m]"]
    for i, (frame, wav, gain) in enumerate(edl.sfx):
        cmd += ["-i", os.path.join(SOUND_DIR, wav)]
        ms = int(round(frame / FPS * 1000))
        chains.append(f"[{i + 1}:a]aformat=sample_rates=48000:channel_layouts=stereo,"
                      f"volume={gain}dB,adelay={ms}|{ms}[x{i}]")
        labels.append(f"[x{i}]")
    chains.append(f"{''.join(labels)}amix=inputs={len(labels)}:normalize=0:duration=first,"
                  f"atrim=0:{dur}[out]")
    cmd += ["-filter_complex", ";".join(chains), "-map", "[out]", "-c:a", "pcm_f32le", MIX]
    subprocess.run(cmd, check=True)


def loudnorm_filter(wav, dur, fadeout):
    """Two-pass loudnorm: measure, then apply linearly to -14 LUFS / -1 dBTP."""
    target = "I=-14:TP=-1:LRA=11"
    res = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", wav, "-af",
                          f"loudnorm={target}:print_format=json", "-f", "null", "-"],
                         capture_output=True, text=True, check=True).stderr
    m = json.loads(res[res.rindex("{"):res.rindex("}") + 1])
    af = (f"loudnorm={target}:measured_I={m['input_i']}:measured_TP={m['input_tp']}:"
          f"measured_LRA={m['input_lra']}:measured_thresh={m['input_thresh']}:"
          f"offset={m['target_offset']}:linear=true,aresample=48000")
    if fadeout:
        af += f",afade=out:st={fadeout[0] / FPS}:d={fadeout[1] / FPS}"
    return af + f",atrim=0:{dur}"


def render(edl, fmt, dry_run):
    inputs, chains = build_graph(edl, fmt)
    dur = edl.frames / FPS
    graph = ";\n".join(chains)
    if dry_run:
        print(graph)
        return 0
    if edl.audio:
        mix_audio(edl)
    cmd = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error"] + inputs
    if edl.audio:
        cmd += ["-i", MIX]
    audio_idx = sum(1 for a in cmd if a == "-i") - 1
    cmd += ["-filter_complex", graph, "-map", "[vout]"]
    if edl.audio:
        cmd += ["-map", f"{audio_idx}:a", "-filter:a", loudnorm_filter(MIX, dur, edl.fadeout),
                "-c:a", "aac", "-b:a", "320k" if not fmt.preview else "160k"]
    cmd += ["-r", str(fmt.fps), "-t", str(dur), "-c:v", "libx264"]
    cmd += ["-preset", "veryfast", "-crf", "24"] if fmt.preview else ["-preset", "slow", "-crf", "16"]
    cmd += ["-pix_fmt", "yuv420p", "-movflags", "+faststart", fmt.out]
    print(f"rendering {fmt.out} ({len(edl.video)} shots, {len(edl.overlays)} overlays, "
          f"{len(edl.sfx)} sfx)")
    return subprocess.run(cmd).returncode


def contact_sheet(edl, fmt):
    """One still from the middle of every shot, 6 per row."""
    frames = [round((v["start"] + v["len"] // 2) * fmt.fps / FPS) for v in edl.video]
    sel = "+".join(f"eq(n\\,{f})" for f in frames)
    rows = (len(frames) + 5) // 6
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", fmt.out, "-vf",
                    f"select='{sel}',scale=320:-1,tile=6x{rows}", "-frames:v", "1",
                    "build/trailer/contact_sheet.png"], check=True)
    print("wrote build/trailer/contact_sheet.png")


def check(edl, fmt):
    """T8 gate: size, rate, frame count, loudness, letterbox, cut positions."""
    path = fmt.out
    probe = json.loads(subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-show_streams",
                                       "-of", "json", path], capture_output=True, text=True,
                                      check=True).stdout)
    v = next(s for s in probe["streams"] if s["codec_type"] == "video")
    a = next(s for s in probe["streams"] if s["codec_type"] == "audio")
    print(f"video {v['width']}x{v['height']} {v['r_frame_rate']} fps, {v['nb_read_frames']} frames, "
          f"{v['codec_name']} {v['pix_fmt']}")
    print(f"audio {a['codec_name']} {a['sample_rate']} Hz {a['channels']} ch, {float(a['duration']):.3f} s")
    res = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", path, "-map", "0:a",
                          "-af", "ebur128=peak=true:framelog=quiet", "-f", "null", "-"],
                         capture_output=True, text=True).stderr
    lufs = re.findall(r"I:\s+(-?[\d.]+) LUFS", res)[-1]
    peak = re.findall(r"Peak:\s+(-?[\d.]+) dBFS", res)[-1]
    print(f"loudness {lufs} LUFS, true peak {peak} dBFS")
    for name, y in (("top", 0), ("bottom", fmt.band_top + fmt.band_h + 2)):
        bars = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-vf",
                               f"crop={fmt.w}:{fmt.band_top - 2}:0:{y},signalstats,"
                               "metadata=print:key=lavfi.signalstats.YMAX:file=-",
                               "-f", "null", "-"], capture_output=True, text=True).stdout
        ymax = [int(x) for x in re.findall(r"YMAX=(\d+)", bars)]
        print(f"letterbox {name} bar: max luma {max(ymax) if ymax else '?'} over {len(ymax)} frames "
              f"(16 = black in limited range)")
    beat = 12                                   # 125 BPM at 25 fps
    cuts = [x["start"] for x in edl.video if x["dur"] == 0]
    off = [c for c in cuts if c % (beat // 2) != 0]
    print(f"hard cuts: {len(cuts)}, all on half-beats: {not off}" + (f" (off: {off})" if off else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--edl", default="tools/trailer/edit.txt")
    ap.add_argument("--preview", action="store_true", help="640x360, 12.5 fps, from preview clips")
    ap.add_argument("--contact-sheet", action="store_true")
    ap.add_argument("--check", action="store_true", help="measure the rendered file")
    ap.add_argument("--dry-run", action="store_true", help="print the filter graph only")
    ap.add_argument("--out", help="output file instead of the default trailer path (e.g. for excerpts)")
    args = ap.parse_args()

    edl = parse(args.edl)
    fmt = Format(args.preview)
    if args.out:
        fmt.out = args.out
    errors = validate(edl, fmt)
    if errors:
        print("\n".join("edl: " + e for e in errors), file=sys.stderr)
        return 1
    if not (args.check or args.contact_sheet) or args.dry_run:
        rc = render(edl, fmt, args.dry_run)
        if rc:
            return rc
    if args.contact_sheet and not args.dry_run:
        contact_sheet(edl, fmt)
    if args.check and not args.dry_run:
        check(edl, fmt)
    return 0


if __name__ == "__main__":
    sys.exit(main())
