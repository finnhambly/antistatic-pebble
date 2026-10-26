"""Tile one platform's tour screenshots: python3 scripts/sheet.py flint out.png"""
import glob, sys
from PIL import Image

plat, out = sys.argv[1], sys.argv[2]
files = sorted(glob.glob(f"shots/{plat}-*.png"))
ims = [Image.open(f).convert("RGB") for f in files]
w, h = ims[0].size
s = 2 if w < 200 else 1
cols = 4
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", ((w + 8) * cols * s, (h + 8) * rows * s), (60, 60, 60))
for i, im in enumerate(ims):
    sheet.paste(im.resize((w * s, h * s), Image.NEAREST), ((i % cols) * (w + 8) * s, (i // cols) * (h + 8) * s))
sheet.save(out)
