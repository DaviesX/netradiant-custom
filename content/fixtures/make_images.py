#!/usr/bin/env python3
"""Generates the fixture mod's test images (uncompressed TGA). Run from this directory."""
import math
from PIL import Image

S = 64
def save(name, img):
    img.save(f"textures/fixture/{name}.tga")

def checker(c0, c1, alpha=None):
    img = Image.new("RGBA", (S, S))
    for y in range(S):
        for x in range(S):
            c = c0 if ((x // 8) + (y // 8)) % 2 == 0 else c1
            a = 255 if alpha is None else alpha(x, y)
            img.putpixel((x, y), c + (a,))
    return img

save("fx_base", checker((200, 180, 150), (120, 100, 80)))
# vertical stripes of alpha 0 / 255: fence and less-than fixtures
save("fx_alpha", checker((90, 140, 90), (60, 100, 60), lambda x, y: 255 if (x // 8) % 2 == 0 else 0))
save("fx_glow", checker((255, 255, 255), (40, 40, 40)))
save("fx_white", Image.new("RGBA", (S, S), (255, 255, 255, 255)))
save("fx_env", checker((80, 120, 200), (200, 220, 255)))
save("fx_glass", Image.new("RGBA", (S, S), (150, 200, 220, 96)))
# bumpy tangent-space normal map
nrm = Image.new("RGBA", (S, S))
for y in range(S):
    for x in range(S):
        dx = 0.5 * math.cos(x / S * 4 * math.pi)
        dy = 0.5 * math.cos(y / S * 4 * math.pi)
        l = math.sqrt(dx * dx + dy * dy + 1)
        nrm.putpixel((x, y), (int((dx / l * 0.5 + 0.5) * 255), int((dy / l * 0.5 + 0.5) * 255), int((1 / l * 0.5 + 0.5) * 255), 255))
save("fx_nrm", nrm)
# metallic-roughness: left half metal (B = 255), roughness ramps along y (G)
mr = Image.new("RGBA", (S, S))
for y in range(S):
    for x in range(S):
        mr.putpixel((x, y), (255, int(y / (S - 1) * 255), 255 if x < S // 2 else 0, 255))
save("fx_mr", mr)
