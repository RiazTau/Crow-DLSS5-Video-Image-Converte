"""Cross-platform source regression test for the V0.6.1.2 AV1 decoder hotfix."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/video/VideoConverter.cpp").read_text(encoding="utf-8")
SETUP = (ROOT / "video/setup_video.ps1").read_text(encoding="utf-8")


def require(cond: bool, message: str) -> None:
    if not cond:
        raise SystemExit("FAIL: " + message)
    print("PASS:", message)


def main() -> None:
    require("kPreflightFrames = 24" in SOURCE, "AV1 preflight tests 24 real frames")
    require('L"-xerror"' in SOURCE and 'L"-nostdin"' in SOURCE, "preflight/decoder are fatal-on-error and non-interactive")
    require("decoder-preflight-last.log" in SOURCE, "AV1 preflight writes a dedicated diagnostic log")
    require("CompactLogText" in SOURCE, "GUI decoder diagnostics are compacted")

    candidate_block = SOURCE.split("const struct Candidate", 1)[1].split("for (const auto& c : candidates)", 1)[0]
    names = ["libdav1d", "av1_cuvid", "av1_qsv", 'L"av1"', "libaom-av1"]
    positions = [candidate_block.index(name) for name in names]
    require(positions == sorted(positions), "decoder order is dav1d -> cuvid -> qsv -> native -> libaom")

    # V0.6.1.2 field fix: codec routing must reject stderr contamination from
    # RunCapture's merged stdout/stderr stream and preserve AV1 preflight routing.
    require('stream=width,height,avg_frame_rate,nb_frames,codec_name,codec_tag_string,profile,pix_fmt:format=duration' in SOURCE,
            "ffprobe stream + format fields use one show_entries expression")
    require("NormalizeCodecName" in SOURCE and "codecName.size() > 64" in SOURCE,
            "codec metadata is strict-token sanitized before routing")
    require('L"stream=codec_name,codec_tag_string"' in SOURCE and 'default=noprint_wrappers=1:nokey=0' in SOURCE,
            "fallback ffprobe metadata uses key=value parsing instead of first-line nokey parsing")
    require('key == "codec_tag_string"' in SOURCE and 'codecTagString == "av01"' in SOURCE,
            "MP4 av01 codec tag independently recovers AV1 detection")
    require('info.codecName = NormalizeCodecName(info.codecName);' in SOURCE and 'if (!av1Input && info.codecName.empty())' in SOURCE,
            "contaminated codec metadata is cleared so AV1 stream sniff fallback runs")
    require("FfmpegStreamLooksAv1" in SOURCE and 'video: av1' in SOURCE.lower(),
            "FFmpeg stream-description fallback can recover AV1 detection")
    require("bool av1Input" in SOURCE and "SelectInputDecoder(ffmpeg, settings.input, info, av1Input" in SOURCE,
            "explicit AV1 routing flag reaches decoder selection")
    decoder_args = SOURCE.split("std::vector<std::wstring> DecoderArgs", 1)[1]
    require("bool av1Input" in decoder_args and "if (av1Input)" in decoder_args,
            "real streaming decoder receives the recovered AV1 routing flag")

    require("Test-HasDav1d" in SETUP, "standard setup verifies libdav1d availability")
    require("ffmpeg-8.1.2-full_build.zip" in SETUP, "standard setup still pins the full Windows FFmpeg build")
    print("V0.6.1.2 AV1 metadata/preflight source contract: PASS")


if __name__ == "__main__":
    main()
