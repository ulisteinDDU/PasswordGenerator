# -*- coding: utf-8 -*-
"""
生成 app.ico：深蓝->青紫渐变圆角方块 + 白色粗钥匙。
用法：python tools/make_icon.py   （在项目根目录运行，输出 app.ico）
依赖：Pillow
"""
from PIL import Image, ImageDraw

C1 = (28, 40, 160)     # 深蓝 #1C28A0
C2 = (110, 60, 220)    # 青紫 #6E3CDC
MASTER = 256           # 母版尺寸，小尺寸由它缩放
SIZES = [16, 24, 32, 48]   # 内嵌尺寸：16/24/32/48（控制 exe 体积）


def make_master() -> Image.Image:
    s = MASTER
    # 垂直渐变
    grad = Image.new("RGBA", (s, s))
    d = ImageDraw.Draw(grad)
    for y in range(s):
        t = y / (s - 1)
        col = tuple(int(C1[i] + (C2[i] - C1[i]) * t) for i in range(3)) + (255,)
        d.line([(0, y), (s, y)], fill=col)
    # 圆角遮罩
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1],
                                           radius=int(s * 0.22), fill=255)
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    img.paste(grad, (0, 0), mask)

    # 白色钥匙：圆环头 + 竖杆 + 两齿
    d = ImageDraw.Draw(img)
    w = max(3, int(s * 0.075))            # 笔画粗细
    cx, r = int(s * 0.46), int(s * 0.17)  # 圆心与半径（偏上偏左）
    cy = int(s * 0.34)
    d.ellipse([cx - r, cy - r, cx + r, cy + r],
              outline=(255, 255, 255, 255), width=w)
    top = cy + r - w // 2
    bottom = int(s * 0.80)
    d.rounded_rectangle([cx - w // 2, top, cx + w // 2, bottom],
                        radius=w // 2, fill=(255, 255, 255, 255))
    d.rectangle([cx + w // 2, top + int(s * 0.12),
                 cx + int(s * 0.30), top + int(s * 0.12) + w],
                fill=(255, 255, 255, 255))
    d.rectangle([cx + w // 2, top + int(s * 0.26),
                 cx + int(s * 0.26), top + int(s * 0.26) + w],
                fill=(255, 255, 255, 255))
    return img


def main():
    master = make_master()
    # 每个内嵌尺寸都要提供精确大小的帧（Pillow 按精确尺寸匹配，
    # 缺失的档位会用最后一帧缩放补位，不会放大）
    frames = [master.resize((sz, sz), Image.LANCZOS) for sz in SIZES]
    master.save("app.ico", format="ICO",
                append_images=frames,
                sizes=[(sz, sz) for sz in SIZES])
    print("app.ico written:", SIZES)


if __name__ == "__main__":
    main()
