# Draws assets/flashshot.png and assets/flashshot.ico (needs Pillow).
from pathlib import Path
from PIL import Image, ImageDraw

N = 1024
WHITE = (255, 255, 255, 255)
TOP, BOTTOM = (168, 85, 247), (219, 39, 119)


def cap_line(d, a, b, w):
    d.line([a, b], fill=WHITE, width=w)
    for x, y in (a, b):
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=WHITE)


def logo():
    grad = Image.new('RGBA', (N, N))
    g = ImageDraw.Draw(grad)
    for y in range(N):
        t = y / (N - 1)
        g.line([(0, y), (N, y)], fill=tuple(round(TOP[i] * (1 - t) + BOTTOM[i] * t) for i in range(3)) + (255,))
    mask = Image.new('L', (N, N), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, N - 1, N - 1], radius=230, fill=255)
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    im.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(im)
    w, length = 60, 150
    lo, hi = 190, N - 190
    for x, y, sx, sy in ((lo, lo, 1, 1), (hi, lo, -1, 1), (lo, hi, 1, -1), (hi, hi, -1, -1)):
        cap_line(d, (x, y), (x + sx * length, y), w)
        cap_line(d, (x, y), (x, y + sy * length), w)
    c = N / 2
    for a, b in (((c, 330), (c, 450)), ((c, 574), (c, 694)), ((330, c), (450, c)), ((574, c), (694, c))):
        cap_line(d, a, b, 56)
    d.ellipse([c - 36, c - 36, c + 36, c + 36], fill=WHITE)
    return im


assets = Path(__file__).resolve().parent.parent / 'assets'
image = logo()
image.resize((256, 256), Image.LANCZOS).save(assets / 'flashshot.png')
image.resize((256, 256), Image.LANCZOS).save(assets / 'flashshot.ico',
                                             sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (256, 256)])
