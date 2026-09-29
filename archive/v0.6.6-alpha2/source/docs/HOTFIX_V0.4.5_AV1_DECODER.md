# V0.4.5 AV1 decoder compatibility

- ffprobe now records codec_name/profile/pix_fmt.
- AV1 input checks installed FFmpeg decoders and prefers libdav1d when available.
- If AV1 decoding fails before frame 1, the error explicitly states that failure occurred before DLSSNR.
- Does not use ignore_err/discardcorrupt to hide genuinely damaged bitstreams.

If libdav1d also reports missing sequence headers/corrupt frames, test the input directly with FFmpeg; the source bitstream may actually be damaged or incomplete.
