# Third-party notices

KuniVRCPhotEditor includes the decoder and demux components of **libwebp 1.6.0** by Google and contributors, compiled into the executable. No WebP encoder is exposed by this application.

- Official source: https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
- Retrieved: 2026-10-09
- Archive SHA-256: `e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564`
- Source retained in `third_party/libwebp-1.6.0` without upstream source modifications.
- Copyright and BSD license: `third_party/libwebp-1.6.0/COPYING`
- Patent grant: `third_party/libwebp-1.6.0/PATENTS`

Build outputs include this notice, `libwebp-COPYING.txt` and `libwebp-PATENTS.txt`. Include these three files when distributing the executable.

`generate_webp_fixtures.py` generated synthetic test fixtures using the development environment's Pillow. Pillow is not linked to the executable and is not required to build or run the application.
