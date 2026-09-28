#!/usr/bin/env python3
"""Generate test albums for MiniDisc writing.

    python3 tools/make-md-test-library.py OUTPUT_DIR

The albums cover what the converter has to handle: 44.1 kHz MP3, 24-bit 48 kHz FLAC
(resampled), mono AAC, Vorbis, and one album of 96 minutes that does not fit on an
80-minute disc. The audio is a synthesised chord with a stepping melody, different on the
left and right channels. Needs ffmpeg.
"""
import os
import subprocess
import sys

ALBUMS = [
    # artist, album, year, extension, codec arguments, sample rate, channels, titles, seconds
    ("The Wombats of Wynyard", "Wombat Radio", 2019, "mp3",
     ["-c:a", "libmp3lame", "-b:a", "320k", "-id3v2_version", "3"], 44100, 2,
     ["Café Tacuba", "Björk's Bells", "Straße Nach Hause", "Midnight Kettle", "Last Light"], [34, 41, 28, 37, 45]),
    ("Marina Vale", "Harbour Lights", 2015, "flac",
     ["-c:a", "flac", "-sample_fmt", "s32"], 48000, 2,
     ["Harbour Lights", "Quiet Water", "Tidal Clock", "Lighthouse Keeper"], [30, 26, 33, 29]),
    ("Kettle & Drum", "Mono Moods", 2010, "m4a",
     ["-c:a", "aac", "-b:a", "96k"], 44100, 1,
     ["Steam Powered", "Foundry Floor", "Copper Pipes"], [24, 27, 31]),
    ("Marina Vale", "Night Digging", 2021, "ogg",
     ["-c:a", "libvorbis", "-q:a", "5"], 44100, 2,
     ["Night Digging", "Burrow Lights", "Dawn Patrol"], [29, 35, 26]),
    ("Slow Tide", "The Very Long Album", 2023, "mp3",
     ["-c:a", "libmp3lame", "-b:a", "32k", "-id3v2_version", "3"], 22050, 1,
     ["Movement %d" % i for i in range(1, 13)], [480] * 12),
]

ROOTS = [220.0, 246.94, 261.63, 293.66, 329.63, 349.23, 392.0]


def expression(seed, channel):
    root = ROOTS[seed % len(ROOTS)]
    third = root * (1.25 if seed % 2 else 1.2)
    fifth = root * 1.5
    melody = "%f*pow(2,floor(mod(t*2+%d,8))/12)" % (root * 2, seed)
    if channel == 0:
        return ("0.22*sin(2*PI*%f*t)*(0.65+0.35*sin(2*PI*0.5*t))+0.12*sin(2*PI*%f*t)"
                "+0.10*sin(2*PI*%s*t)" % (root, fifth, melody))
    return ("0.22*sin(2*PI*%f*t)*(0.65+0.35*cos(2*PI*0.5*t))+0.12*sin(2*PI*%f*t)"
            "+0.08*sin(2*PI*%s*t)" % (third, fifth, melody))


def main():
    out = sys.argv[1]
    seed = 0
    for artist, album, year, ext, codec, rate, channels, titles, lengths in ALBUMS:
        folder = os.path.join(out, artist, album)
        os.makedirs(folder, exist_ok=True)
        for n, (title, seconds) in enumerate(zip(titles, lengths), 1):
            seed += 1
            source = "aevalsrc=exprs='%s|%s':s=%d:d=%d" % (expression(seed, 0), expression(seed, 1), rate, seconds)
            if channels == 1:
                source += ",pan=mono|c0=0.5*c0+0.5*c1"
            dest = os.path.join(folder, "%02d %s.%s" % (n, title.replace("/", "-"), ext))
            meta = ["-metadata", "title=" + title, "-metadata", "artist=" + artist,
                    "-metadata", "album_artist=" + artist, "-metadata", "album=" + album,
                    "-metadata", "date=%d" % year, "-metadata", "track=%d/%d" % (n, len(titles))]
            cmd = ["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi", "-i", source] + codec + meta + [dest]
            subprocess.run(cmd, check=True)
        print(folder)


if __name__ == "__main__":
    main()
