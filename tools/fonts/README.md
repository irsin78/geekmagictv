# Fonts

Only needed to regenerate `firmware/usagebar/src/glyphs.h` with `tools/gen_glyphs.py`. The generated
header is committed, so building the firmware does not need these files. The font files are not
committed; download them here:

| File | Source | License |
|---|---|---|
| `Galmuri14.ttf` (digits, Latin, Korean) | [Galmuri v2.40.4](https://github.com/quiple/galmuri/releases/tag/v2.40.4), from `Galmuri-v2.40.4.zip` | SIL OFL 1.1 (`Galmuri-OFL.txt`) |
| `Galmuri11-Bold.ttf` (optional alternative look) | same archive | SIL OFL 1.1 |
| `unifont.otf` (Japanese, Chinese, Cyrillic, Thai) | [GNU Unifont 18.0.01](https://unifoundry.com/pub/unifont/unifont-18.0.01/font-builds/), `unifont-18.0.01.otf` renamed | SIL OFL 1.1 (`Unifont-OFL.txt`) |

```sh
curl -L -o /tmp/galmuri.zip https://github.com/quiple/galmuri/releases/download/v2.40.4/Galmuri-v2.40.4.zip
unzip -j /tmp/galmuri.zip Galmuri14.ttf Galmuri11-Bold.ttf -d tools/fonts
curl -L -o tools/fonts/unifont.otf https://unifoundry.com/pub/unifont/unifont-18.0.01/font-builds/unifont-18.0.01.otf
```
