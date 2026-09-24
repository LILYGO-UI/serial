"""Render the application's original geometric serial-port icon (requires Pillow)."""

from pathlib import Path

from PIL import Image, ImageDraw


def main():
    scale = 4
    image = Image.new("RGBA", (128 * scale, 128 * scale))
    draw = ImageDraw.Draw(image)

    def coords(values):
        return tuple(round(value * scale) for value in values)

    draw.rounded_rectangle(coords((0, 0, 128, 128)), radius=28 * scale, fill="#007AFF")
    draw.rounded_rectangle(coords((19, 24, 109, 80)), radius=14 * scale, fill="white")
    draw.polygon([coords(p) for p in ((30, 35), (98, 35), (90, 68), (38, 68))], fill="#007AFF")
    for y, xs in ((45, (42, 53, 64, 75, 86)), (57, (47, 58, 69, 80))):
        for x in xs:
            draw.ellipse(coords((x - 2.5, y - 2.5, x + 2.5, y + 2.5)), fill="white")
    for points in (((39, 97), (70, 97)), ((64, 91), (70, 97), (64, 103)),
                   ((90, 111), (59, 111)), ((65, 105), (59, 111), (65, 117))):
        draw.line([coords(p) for p in points], fill="white", width=4 * scale, joint="curve")
    output = Path(__file__).resolve().parents[1] / "assets" / "app-icon.png"
    image.resize((128, 128), Image.Resampling.LANCZOS).save(output)


if __name__ == "__main__":
    main()
