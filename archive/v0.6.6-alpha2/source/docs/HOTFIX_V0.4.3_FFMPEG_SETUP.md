# V0.4.3 FFmpeg Setup Recovery

Fixes interrupted Setup FFmpeg runs poisoning the next setup attempt.

Changes:
- downloads to `ffmpeg-release-essentials.zip.part`
- validates ZIP before promoting it to the final filename
- validates an existing ZIP before trusting it
- removes stale `.part`, `ffmpeg_extract_*`, and incomplete `ffmpeg/` folders
- retains support for a manually downloaded valid ZIP
