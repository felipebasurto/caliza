# Caliza Estratos asset provenance

This interface does **not** use PS2UI assets, PlayStation/PS2 branding, BIOS data,
commercial game artwork, or the generated reference bitmaps as runtime resources.

## Vector and procedural assets

- The Caliza mark is drawn at runtime as three staggered mineral-sheet polygons in
  `caliza/native/estratos_scene.cpp`. No bitmap logo is embedded.
- The basalt/graphite/indigo diagonal strata, focus frames, controller diagram,
  first-run mineral crystal, panels and icons are runtime vector geometry emitted
  through `caliza/native/draw_list.*`.
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
