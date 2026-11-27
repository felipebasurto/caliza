# Caliza Estratos asset provenance

This interface does **not** use PS2UI assets, PlayStation/PS2 branding, BIOS data,
commercial game artwork, or the generated reference bitmaps as runtime resources.

## Vector and procedural assets

- The Caliza mark, a limestone block cut into three stepped strata, is drawn at runtime
  by `Logo()` in `caliza/native/estratos_scene.cpp`. The console icon
  (`ps4/sce_sys/icon0.png`), the master PNG and the companion-panel SVGs are generated
  from the same geometry by `ps4/tools/make_brand_assets.py`.
- The README vector illustrations under `ps4/docs/diagrams/` (`caliza-overview.svg`,
  `caliza-architecture.svg`, `caliza-storage-flow.svg`, and `download-button.svg`) are
  original standalone vector assets for Caliza using only geometric primitives and the
  Estratos palette; they contain no third-party trademarks, logos, or commercial artwork.
- The background is procedural: a radial glow sprite and a domain-warped noise fog are
  generated once on the CPU, and the glass cubes are projected and shaded per frame. The
  background stays still apart from a slow float of each cube; reduced motion stops that
  too. Focus bands, hairlines, panels, the controller diagram and the memory card glyph
  are runtime vector geometry emitted through `caliza/native/draw_list.*`.
- The companion-panel QR code on Diagnostics is encoded at runtime with the QR Code
  generator by Project Nayuki (MIT), already vendored in
  `ps5/frontend/third_party/qrcodegen/` and listed in `ps5/frontend/assets/SOURCES.txt`.
- Demo covers are fictional and procedural. `caliza/native/procedural_covers.*`
  deterministically generates them from the demo game's stable key and title. The
  names in `caliza/host/host_demo.*` are invented for the host preview.
- `caliza/shaders/estratos2d.vert` and `estratos2d.frag` are original shaders for
  this interface. `caliza/native/estratos_shaders.inc` is generated SPIR-V data by
  `caliza/host/build-shaders.sh` and should be regenerated rather than edited.

## Fonts

The host preview reads the repository's existing font resources; it does not copy
or modify them:

- `bin/resources/fonts/Roboto-Regular.ttf` — Roboto, copyright Google Inc.,
  Apache License 2.0 (see `bin/resources/fonts/Roboto-Regular-copyright`).
- `bin/resources/fonts/promptfont.otf` — PromptFont, SIL Open Font License 1.1
  (see `bin/resources/fonts/promptfont-license`).

Console integration should use the same existing font-service resources or another
explicitly licensed equivalent. Text is rasterized through the existing SDF font
code in `ps5/frontend/fe_text.*`; no font binaries were added under `caliza/`.
