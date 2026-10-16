# Wallpapers

Original wallpapers for the Galleon 100 SD status screen and key grid, one folder per style. They follow the rules in [`ASSETS.md`](../../ASSETS.md) and are licensed **CC-BY-SA-4.0**.

| Folder | Style |
|---|---|
| `manga-classic/` | Classic hand-inked manga and anime |
| `manga-cyberpunk/` | Neon cyberpunk in manga style |
| `western-cyberpunk/` | Gritty cyberpunk in Western concept-art style |
| `video-game-hero/` | Original video game key art |
| `portrait/` | Portraits of fictional adults |
| `cinematic/` | Widescreen film stills |
| `tv-series/` | Prestige television look |
| `space-western/` | Space western with an original bounty hunter |

## Format

The screen size is not confirmed yet (1280×720 or 720×180), so wallpapers are kept as large originals and cropped by the settings application for each target.

- Landscape **16:9**, the largest size the tool offers.
- Main subject in the **central horizontal band**: the image must survive a crop to a thin 4:1 banner.
- **Left third calmer and darker**: widgets (clock, sensors) sit there.
- No text, logos, watermarks or signatures.
- Bold shapes and strong contrast; avoid subtle gradients and fine noise, which JPEG compression on the keyboard turns into blocks.

## Files

- Name: `<style>-<nn>.jpg`, for example `manga-classic-01.jpg`.
- Commit **JPEG at quality 92** rather than PNG, to keep the repository small: originals from image tools are often several megabytes each.
  `magick input.png -quality 92 manga-classic-01.jpg`
- Record every image in its folder's `PROVENANCE.md`: tool, date and exact prompt.

## Adding images

Open a pull request. Check each image against `ASSETS.md` first: a generated image can still resemble an existing character or brand.
