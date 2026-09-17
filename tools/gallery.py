"""Renders the README gallery: default, dark, no-photo and a long-text stress case."""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import preview as P
from PIL import Image

P.load_fonts()
photo = os.path.join("tools", "_testphoto.bin")
if not os.path.exists(photo):
    P.make_test_photo(photo)

def shot(cfg):
    c = P.render(cfg, photo)
    port = np.zeros((P.CANVAS_H, P.CANVAS_W), np.uint8)
    for x in range(P.CANVAS_W):
        for y in range(P.CANVAS_H):
            lx, ly = c.map(x, y)
            port[y, x] = c.fb[ly, lx]
    return (port * 17).astype(np.uint8), c.warnings

VARIANTS = [
    ("default", dict(P.DEFAULT_CFG)),
    ("dark", dict(P.DEFAULT_CFG, invert=True)),
    ("no photo", dict(P.DEFAULT_CFG, showPhoto=False,
        funFact="Ask me about LoRaWAN coverage mapping in rural Kenya, mesh "
                "gateways, and why the answer is always antenna height.")),
    ("long text", dict(P.DEFAULT_CFG,
        name="Bartholomew Featherstonehaugh",
        profession="Principal Distributed Systems Reliability Architect",
        company="Interplanetary Widget Manufacturing Consortium",
        pronouns="", footer="", funFact="Short one.")),
]

imgs = []
for label, cfg in VARIANTS:
    a, w = shot(cfg)
    print("%-9s %s" % (label, "; ".join(w)))
    imgs.append(a)

Image.fromarray(imgs[0], "L").save("docs/badge.png")

gap = 18
W = sum(i.shape[1] for i in imgs) + gap * (len(imgs) + 1)
sheet = np.full((P.CANVAS_H + 2 * gap, W), 200, np.uint8)
x = gap
for i in imgs:
    sheet[gap:gap + i.shape[0], x:x + i.shape[1]] = i
    x += i.shape[1] + gap
Image.fromarray(sheet, "L").resize((W // 2, (P.CANVAS_H + 2 * gap) // 2),
                                   Image.LANCZOS).save("docs/gallery.png")
print("wrote docs/badge.png and docs/gallery.png")
