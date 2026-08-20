from PIL import Image, ImageDraw, ImageFont
import sys, os

W, H = 1200, 630
BONE   = (245, 244, 238)
TEXT   = (20, 20, 19)
TEXT2  = (61, 61, 58)
MUTED  = (107, 106, 101)
CLAY   = (204, 120, 92)
BORDER = (212, 209, 196)

LIB_B = "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
LIB_R = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
DJ_B  = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
DJ_R  = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
MONO  = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
bold    = LIB_B if os.path.exists(LIB_B) else DJ_B
regular = LIB_R if os.path.exists(LIB_R) else DJ_R
F = lambda p, s: ImageFont.truetype(p, s)

img = Image.new("RGB", (W, H), BONE)
d = ImageDraw.Draw(img)

# top accent rule
d.rectangle([0, 0, W, 8], fill=CLAY)

# logo
logo = Image.open("resources/notepatra-512.png").convert("RGBA")
LS = 184
logo = logo.resize((LS, LS), Image.LANCZOS)
LX, LY = 84, 176
img.paste(logo, (LX, LY), logo)

x = LX + LS + 56

# wordmark
f_name = F(bold, 96)
d.text((x, 174), "Notepatra", font=f_name, fill=TEXT)

# tagline
f_tag = F(regular, 37)
d.text((x, 292), "The code editor built for the AI era", font=f_tag, fill=TEXT2)

# spec chips
f_chip = F(bold, 23)
chips = ["C++ + Rust", "~12 MB native", "Zero Electron", "49-tool MCP server"]
cx, cy, ch = x, 362, 44
for c in chips:
    w = d.textlength(c, font=f_chip)
    d.rounded_rectangle([cx, cy, cx + w + 32, cy + ch], radius=ch // 2,
                        fill=(255, 255, 255), outline=BORDER, width=2)
    d.text((cx + 16, cy + 10), c, font=f_chip, fill=TEXT2)
    cx += w + 32 + 12

# footer rule + url + platforms
d.line([84, 500, W - 84, 500], fill=BORDER, width=2)
f_url = F(bold, 30)
d.text((84, 526), "notepatra.org", font=f_url, fill=CLAY)
f_meta = F(regular, 26)
meta = "Linux  ·  Windows  ·  macOS  ·  GPL-3.0  ·  Free forever"
mw = d.textlength(meta, font=f_meta)
d.text((W - 84 - mw, 529), meta, font=f_meta, fill=MUTED)

out = sys.argv[1]
img.save(out, "PNG", optimize=True)
print(f"{out}  {img.size[0]}x{img.size[1]}  ratio {img.size[0]/img.size[1]:.2f}  {os.path.getsize(out)} bytes")
