# /// script
# requires-python = ">=3.11"
# dependencies = ["playwright==1.63.0", "html5lib==1.1", "PyYAML==6.0.3"]
# ///
"""Validate the static guide and its illustration in an isolated browser."""

import argparse
import json
from pathlib import Path

import html5lib
import yaml
from playwright.sync_api import sync_playwright


def check_sources(root):
    for path in (root / "docs").glob("*.html"):
        parser = html5lib.HTMLParser(strict=False)
        parser.parse(path.read_text(encoding="utf-8"))
        assert not parser.errors, f"{path.name}: {parser.errors}"

    for path in (root / ".github/ISSUE_TEMPLATE").glob("*.yml"):
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
        if "body" not in data:
            assert isinstance(data["contact_links"], list), path.name
            continue
        assert data["name"] and data["description"], path.name
        ids = [field["id"] for field in data["body"] if "id" in field]
        assert len(ids) == len(set(ids)), path.name
        assert all(field["type"] in {"markdown", "input", "textarea", "checkboxes", "dropdown"} for field in data["body"])
    print("PASS HTML parsing and issue form structure")


def check_page(browser, url, output, axe_script=None):
    for theme in ("light", "dark"):
        for width in (320, 390, 768, 1440):
            context = browser.new_context(
                viewport={"width": width, "height": 1000},
                color_scheme=theme,
                reduced_motion="reduce",
            )
            page = context.new_page()
            errors = []
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(url, wait_until="networkidle")
            assert not errors, errors
            assert page.locator("#demo-mute").is_enabled()
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), f"Horizontal overflow: {theme}, {width}"
            ids = page.locator("[id]").evaluate_all("nodes => nodes.map(node => node.id)")
            assert len(ids) == len(set(ids)), "Duplicate HTML IDs"
            fragments = page.locator('a[href^="#"]').evaluate_all("nodes => nodes.map(node => node.hash.slice(1))")
            assert all(fragment in ids for fragment in fragments), "Broken internal link"
            assert page.locator('meta[property="og:image"]').count() == 1

            recording = page.locator(".audio-branch").first
            original_recording = recording.inner_html()
            mute = page.locator("#demo-mute")
            mute.focus()
            page.keyboard.press("Space")
            assert mute.get_attribute("aria-pressed") == "true"
            assert page.locator("#demo-monitoring").inner_text() == "Silent to you"
            assert recording.inner_html() == original_recording

            vertical = page.locator('[data-scene="vertical"]')
            vertical.focus()
            page.keyboard.press("Enter")
            assert vertical.get_attribute("aria-pressed") == "true"
            assert page.locator("#demo-resolution").inner_text() == "1080 × 1920"
            assert "30 FPS" in page.locator("#demo-fps").inner_text()
            assert mute.get_attribute("aria-pressed") == "true", "Scene change must preserve global mute"
            assert recording.inner_html() == original_recording
            assert "none" == page.locator(".meter i").first.evaluate("node => getComputedStyle(node).animationName")

            mute.click()
            assert page.locator("#demo-monitoring").inner_text() == "Audible"
            page.locator('[data-scene="gameplay"]').click()
            assert page.locator("#demo-resolution").inner_text() == "1920 × 1080"
            assert "60 FPS" in page.locator("#demo-fps").inner_text()

            # The illustration must remain fully local, without media or trackers.
            assert page.locator("audio, video, iframe").count() == 0
            resources = page.evaluate("performance.getEntriesByType('resource').map(entry => entry.name)")
            assert all(resource.rsplit("/", 1)[-1] in {"landing.css", "landing.js", "favicon.ico"} for resource in resources), resources
            if axe_script:
                page.add_script_tag(path=str(axe_script))
                audit = page.evaluate("""async () => {
                    const result = await axe.run(document, {
                        runOnly: { type: 'tag', values: ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa'] }
                    });
                    return result.violations.map(rule => ({
                        id: rule.id, impact: rule.impact,
                        nodes: rule.nodes.map(node => ({ target: node.target, summary: node.failureSummary }))
                    }));
                }""")
                assert not audit, json.dumps(audit, indent=2)
            if width in (390, 1440):
                page.screenshot(path=str(output / f"{theme}-{width}.png"), full_page=True)
            print(f"PASS {theme} {width}px: layout, links, keyboard, mute, scenes, reduced motion")
            context.close()

    context = browser.new_context(java_script_enabled=False, viewport={"width": 390, "height": 900})
    page = context.new_page()
    page.goto(url, wait_until="load")
    assert page.locator("#demo-mute").is_disabled()
    assert page.locator("noscript").is_visible()
    assert page.locator('a[href$="releases/latest"]').count() >= 1
    assert page.locator("#quick-audio").is_visible()
    page.locator(".mobile-guide summary").click()
    assert page.locator(".mobile-guide-links").is_visible()
    assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
    context.close()
    print("PASS JavaScript disabled: downloads, guide and mobile navigation remain usable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", help="Optional deployed URL; defaults to the local guide file")
    parser.add_argument("--output", type=Path, help="Screenshot destination (default: artifacts/website-check)")
    parser.add_argument("--update-social-card", action="store_true", help="Render docs/social-card.html to docs/social-card.png")
    parser.add_argument("--axe-script", type=Path, help="Optional local axe-core script for a WCAG 2.1 AA automated audit")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output or root / "artifacts/website-check"
    output.mkdir(parents=True, exist_ok=True)
    check_sources(root)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(channel="chromium")
        if args.update_social_card:
            page = browser.new_page(viewport={"width": 1200, "height": 630}, device_scale_factor=1)
            page.goto((root / "docs/social-card.html").as_uri())
            page.screenshot(path=str(root / "docs/social-card.png"))
            page.close()
            print("Rendered share card: docs/social-card.png")
        check_page(browser, args.url or (root / "docs/index.html").as_uri(), output, args.axe_script)
        browser.close()
    print(json.dumps({"result": "passed", "screenshots": str(output)}))


if __name__ == "__main__":
    main()
