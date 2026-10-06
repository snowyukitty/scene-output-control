# Website maintenance

The project landing page and user guide are a single static website. GitHub
Pages serves **`main:/docs`** at
<https://snowyukitty.github.io/scene-output-control/>. No build service, framework,
analytics, third-party font, or application backend is required.

## Files

- `index.html`: landing page, downloads, and guide. Preserve existing section IDs;
  the plugin and shared links use them as deep links.
- `landing.css`: responsive landing page and illustration styles. Guide styles
  remain inline in `index.html`.
- `landing.js`: local illustration only. It must not connect to OBS, change audio
  settings, play sound, or imply that it is a real OBS screenshot.
- `social-card.html`: editable source for the 1200 × 630 share image.
- `social-card.png`: rendered output used by Open Graph previews.

Keep download names and release numbers consistent with actual GitHub Release
assets. The main Download button points to `releases/latest`; platform links
identify the version they download. Label development-only features explicitly.
Do not claim support for an unverified OBS version or package distribution.

## Verification

Install [uv](https://docs.astral.sh/uv/) and the pinned isolated test browser once:

```powershell
uv run --with playwright==1.63.0 python -m playwright install chromium
```

From the repository root:

```powershell
uv run scripts/check_website.py
uv run scripts/check_website.py --update-social-card
```

The script declares its own pinned Python dependencies. It checks HTML parsing,
issue form structure, internal anchors, duplicate IDs, layout at 320/390/768/1440px
in light/dark mode, keyboard controls, global mute behavior across demo scene
changes, reduced motion, and the no-JavaScript fallback. The recording branch in
the illustration must stay unchanged during mute. It also rejects unexpected
page resources and media elements. Screenshots are saved to the ignored
`artifacts/website-check` directory; inspect desktop, mobile, and the share card.
These are website checks, not real OBS audio or plugin compatibility tests.

For an additional automated WCAG 2.1 AA audit, provide a trusted local copy of
axe-core (this milestone used 4.11.0):

```powershell
uv run scripts/check_website.py --axe-script .cache/axe.min.js
```

The audit script is injected only into the isolated test browser. It is not
included in the deployed website. Automated checks do not replace manual
keyboard and screen-reader review.

Use `--url https://snowyukitty.github.io/scene-output-control/` after deployment
to run the same browser checks against the public site. Review actual release
asset links through the GitHub API when changing downloads.

## Deployment

Work on a branch and validate locally. A finished docs-only change can land on
`main` without merging an unfinished plugin feature branch. GitHub Pages then
builds the `docs` folder automatically. The existing main push workflow also
builds the plugin; do not disable it to ship a website update. Check deployment
status and the live site after pushing. Updating these files does not create a
plugin release or alter repository visibility.
