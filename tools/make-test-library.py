#!/usr/bin/env python3
"""Generate a small tagged test music library (MP3, FLAC, OGG, M4A/AAC, WAV) with cover art.

    python3 tools/make-test-library.py OUTPUT_DIR

Needs ffmpeg and Pillow. Each album gets a distinct generated cover; tracks are short
synthesised tones so the whole library stays small enough to copy into the test VM.
"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ALBUMS = [
    ("The Wombats of Wynyard", "Burrow Songs", 2019, "Indie", "mp3", (214, 92, 60), (250, 220, 170)),
    ("The Wombats of Wynyard", "Night Digging", 2021, "Indie", "flac", (40, 60, 120), (150, 190, 240)),
    ("Marina Vale", "Harbour Lights", 2015, "Jazz", "m4a", (30, 110, 100), (190, 240, 220)),
    ("Marina Vale", "Second Harbour", 2018, "Jazz", "ogg", (120, 40, 90), (240, 180, 220)),
    ("Kettle & Drum", "Steam Powered", 2010, "Rock", "wav", (90, 90, 90), (230, 230, 210)),
    ("Kettle & Drum", "Live at the Foundry", 2012, "Rock", "aac", (160, 120, 30), (250, 240, 190)),
]
TITLES = ["Opening Bell", "Long Road Home", "Quiet Water", "Midnight Kettle", "Last Light", "Reprise"]
NOTES = [261.63, 293.66, 329.63, 392.00, 440.00, 493.88]


def cover(path, artist, album, c1, c2):
    img = Image.new("RGB", (600, 600), c1)
    draw = ImageDraw.Draw(img)
    for i in range(0, 600, 24):
        shade = tuple(int(c1[k] + (c2[k] - c1[k]) * i / 600) for k in range(3))
        draw.rectangle([0, i, 600, i + 24], fill=shade)
    draw.ellipse([120, 100, 480, 460], outline=c2, width=18)
    draw.ellipse([270, 250, 330, 310], fill=c2)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 44)
        small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 30)
    except OSError:
        font = small = ImageFont.load_default()
    draw.text((40, 480), album, fill=(255, 255, 255), font=font)
    draw.text((40, 535), artist, fill=(240, 240, 240), font=small)
    img.save(path, "JPEG", quality=88)


def main():
    out = sys.argv[1]
    for artist, album, year, genre, ext, c1, c2 in ALBUMS:
        folder = os.path.join(out, artist, album)
        os.makedirs(folder, exist_ok=True)
        art = os.path.join(folder, "cover.jpg")
        cover(art, artist, album, c1, c2)
        for n, title in enumerate(TITLES, 1):
            secs = 6 + n
            dest = os.path.join(folder, "%02d %s.%s" % (n, title, ext))
            freq = NOTES[(n - 1) % len(NOTES)]
            src = "sine=frequency=%s:duration=%d,volume=0.3,tremolo=f=%d:d=0.6" % (freq, secs, n)
            meta = ["-metadata", "title=" + title, "-metadata", "artist=" + artist,
                    "-metadata", "album_artist=" + artist, "-metadata", "album=" + album,
                    "-metadata", "date=%d" % year, "-metadata", "genre=" + genre,
                    "-metadata", "track=%d/%d" % (n, len(TITLES))]
            cmd = ["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi", "-i", src]
            embed = ext in ("mp3", "flac", "m4a")
            if embed:
                cmd += ["-i", art]
            cmd += ["-map", "0:a"]
            if embed:
                cmd += ["-map", "1:v", "-c:v", "mjpeg", "-disposition:v", "attached_pic"]
            codec = {"mp3": ["-c:a", "libmp3lame", "-b:a", "128k", "-id3v2_version", "3"],
                     "flac": ["-c:a", "flac"],
                     "m4a": ["-c:a", "aac", "-b:a", "128k"],
                     "ogg": ["-c:a", "libvorbis", "-q:a", "4"],
                     "wav": ["-c:a", "pcm_s16le"],
                     "aac": ["-c:a", "aac", "-b:a", "96k", "-f", "adts"]}[ext]
            cmd += codec + meta + [dest]
            subprocess.run(cmd, check=True)
        print(folder)


if __name__ == "__main__":
    main()
