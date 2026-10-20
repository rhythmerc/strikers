"""Writes build/share/index.html: a phone-friendly page of the desktop XR shots (newest first) and
anything else dropped in build/share (images, pages), for viewing over the tailnet.

build/share/shots links to build/linux-xr/shots. tools/xr-desktop.sh runs this after each shot;
tools/xr-desktop.sh serve serves the folder.

Usage: python3 tools/share-index.py [SHARE_DIR]
"""

import html
import sys
import time
from urllib.parse import quote
from pathlib import Path

PORT = Path(__file__).resolve().parent.parent
IMAGES = (".png", ".jpg", ".jpeg", ".gif", ".webp")
# Each shot's images, in the order shown, with a caption.
SHOT_IMAGES = (("xr_3d.png", "eyes (left above right)"), ("xr_hud.png", "HUD"),
               ("xr_screen.png", "screen"), ("xr_panel.png", "dev tools panel"))

STYLE = """
:root { color-scheme: light dark; --bg: #fafafa; --fg: #1a1a1a; --dim: #666; --card: #fff; --line: #ddd; }
@media (prefers-color-scheme: dark) { :root { --bg: #121212; --fg: #eee; --dim: #999; --card: #1e1e1e; --line: #333; } }
body { margin: 0; padding: 12px 16px 40px; background: var(--bg); color: var(--fg);
       font: 15px/1.4 system-ui, sans-serif; }
h1 { font-size: 20px; margin: 4px 0 12px; }
h2 { font-size: 17px; margin: 0 0 2px; }
section { background: var(--card); border: 1px solid var(--line); border-radius: 10px;
          padding: 12px; margin: 0 0 14px; }
.meta { color: var(--dim); font-size: 13px; margin-bottom: 8px; word-break: break-word; }
figure { margin: 8px 0 0; }
figcaption { color: var(--dim); font-size: 13px; }
img { display: block; max-width: 100%; height: auto; border-radius: 6px; }
a { color: inherit; }
ul { padding-left: 20px; }
"""


def stamp(path):
    return time.strftime("%b %d %H:%M", time.localtime(path.stat().st_mtime))


def figure(href, caption):
    href = html.escape(quote(href), quote=True)
    return (f'<figure><a href="{href}"><img loading="lazy" src="{href}" alt=""></a>'
            f'<figcaption>{html.escape(caption)}</figcaption></figure>')


def main():
    share = Path(sys.argv[1]) if len(sys.argv) > 1 else PORT / "build" / "share"
    share.mkdir(parents=True, exist_ok=True)
    shots = share / "shots"
    if not shots.exists():
        shots.symlink_to(Path("..") / "linux-xr" / "shots")

    body = []
    # Loose files at the top: crops, one-off images, other pages.
    loose = sorted((p for p in share.iterdir() if p.is_file() and p.name != "index.html"),
                   key=lambda p: p.stat().st_mtime, reverse=True)
    pages = [p for p in loose if p.suffix == ".html"]
    if pages:
        items = "".join(f'<li><a href="{html.escape(quote(p.name), quote=True)}">{html.escape(p.stem)}</a>'
                        f' <span class="meta">{stamp(p)}</span></li>' for p in pages)
        body.append(f"<section><h2>Pages</h2><ul>{items}</ul></section>")
    for p in loose:
        if p.suffix.lower() in IMAGES:
            body.append(f'<section><h2>{html.escape(p.stem)}</h2><div class="meta">{stamp(p)}</div>'
                        f'{figure(p.name, p.name)}</section>')

    dirs = [d for d in shots.iterdir() if (d / "dump").is_dir()] if shots.is_dir() else []
    for d in sorted(dirs, key=lambda d: (d / "dump").stat().st_mtime, reverse=True):
        figs = [figure(f"shots/{d.name}/dump/{name}", caption)
                for name, caption in SHOT_IMAGES if (d / "dump" / name).exists()]
        if not figs:
            continue
        args = (d / "args.txt").read_text().strip() if (d / "args.txt").exists() else ""
        meta = stamp(d / "dump") + (f" · {html.escape(args)}" if args else "")
        body.append(f'<section><h2>{html.escape(d.name)}</h2><div class="meta">{meta}</div>'
                    f'{"".join(figs)}</section>')

    page = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width, initial-scale=1">'
            f'<title>Strikers XR shots</title><style>{STYLE}</style></head><body>'
            f'<h1>Strikers XR shots</h1><div class="meta">updated {time.strftime("%b %d %H:%M")}</div>'
            + ("".join(body) or "<p>Nothing yet.</p>") + "</body></html>\n")
    (share / "index.html").write_text(page)


if __name__ == "__main__":
    main()
