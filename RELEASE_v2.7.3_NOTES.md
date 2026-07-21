## peterhack v2.7.3

Download **`peterhack-v2.7.3-win64.zip`**, extract anywhere (e.g. Desktop), then run:

`peterhack-loader.exe --local --wait`

**Important:** fully exit the game before updating — the bridge DLL stays loaded until the process exits.

---

## New since v2.7.2

### Camouflage — emissive & paint sync
- Fixed neon/emissive camo paint via **packed PBR texture import** (metallic/roughness/emissive composed as one channel)
- Production paint uses **AMRE channel 7** with 31-byte wire format and proper emissive encoding
- **Auto material** now detects emissive from the target's existing paint (no longer forced to zero)
- Incremental local texture import runs alongside server packed batches for visible, stable results
- Material auto-detect prefers non-emissive dielectric patterns with packed-PBR emissive fallback

### Camouflage — defaults
- **Brush 1 + Brush 2** enabled (25 / 5 texels)
- **Auto material** on, **all regions Paint**, **camo hotkeys** enabled (F1–F4)
- Manual metallic/roughness/emissive sliders shown as fallbacks when auto material is on

### Quality of life
- Removed periodic `[ESP]` console status spam during matches

### Game update (2.9.0 build)
- Bridge and SDK refreshed for current **Meccha Chameleon 2.9.0** shipping binary
- Updated packed-paint route RVAs and paint material pattern layout (emissive-aware)

---

## Build info
- Tag: `v2.7.3`
- Branch: `github-main`
- Built: Release | x64
- Output: `peterhack.dll`, `peterhack-loader.exe`, `bridge/peterhack-bridge.dll`
