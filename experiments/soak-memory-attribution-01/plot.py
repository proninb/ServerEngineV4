"""Optional standalone SVG time-series plots using ReportLab's chart library."""
import argparse
import csv
import json
import math
from pathlib import Path
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics.shapes import Drawing, Rect, String, Line
from reportlab.graphics import renderSVG
from reportlab.lib import colors

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
root, out = args.root, args.output
series = {}
for arm, folder in (("OFF", "soak-off-01"), ("USE", "soak-01")):
    series[arm] = [r for r in map(json.loads, (root / folder / "samples.jsonl").read_text().splitlines()) if r["measured"]]
with (out / "aligned-cycles.csv").open() as stream:
    aligned = list(csv.DictReader(stream))
figure = Drawing(1000, 650)
figure.add(Rect(0, 0, 1000, 650, fillColor=colors.white, strokeColor=None))
figure.add(String(65, 622, "SOAK-MEMORY-ATTRIBUTION-01: quiescent Private Bytes", fontName="Helvetica-Bold", fontSize=17))
palette = [colors.HexColor("#1760a5"), colors.HexColor("#c95514")]
for i, arm in enumerate(("OFF", "USE")):
    x = 710 + i * 110
    figure.add(Line(x, 622, x + 24, 622, strokeColor=palette[i], strokeWidth=2))
    figure.add(String(x + 31, 618, arm, fontName="Helvetica", fontSize=11))
for y, title, data, axis in [
    (365, "Elapsed-time view (sequential runs)", [[(r["elapsed_seconds"], r["private_bytes"] / 1048576) for r in series[arm]] for arm in ("OFF", "USE")], "Measured elapsed seconds"),
    (90, "Common-cycle view (same fixture and reconnect sequence)", [[(int(r["cycle"]), float(r[key])) for r in aligned] for key in ("off_private_MiB", "use_private_MiB")], "Completed cycle number")]:
    plot = LinePlot()
    plot.x, plot.y, plot.width, plot.height = 75, y, 865, 190
    plot.data = data
    plot.xValueAxis.valueMin = 0
    plot.xValueAxis.valueMax = math.ceil(max(x for points in data for x, _ in points) / 100) * 100
    plot.xValueAxis.valueStep = 100 if y > 100 else 200
    plot.yValueAxis.valueMin = 0
    plot.yValueAxis.valueMax = math.ceil(max(v for points in data for _, v in points) / 50) * 50
    plot.yValueAxis.valueStep = 50
    plot.xValueAxis.labels.fontSize = 9
    plot.yValueAxis.labels.fontSize = 9
    for i in range(2):
        plot.lines[i].strokeColor = palette[i]
        plot.lines[i].strokeWidth = 0.65
    figure.add(plot)
    figure.add(String(65, y + 215, title, fontName="Helvetica-Bold", fontSize=12))
    figure.add(String(25, y + 175, "MiB", fontName="Helvetica", fontSize=9))
    figure.add(String(430, y - 32, axis, fontName="Helvetica", fontSize=10))
figure.add(String(65, 31, "Samples: UNLOADED after reconnect/GET_STATE. Lines join checkpoints; phase memory is unobserved.", fontName="Helvetica", fontSize=10))
figure.add(String(65, 15, "Release drift gate remains last-five minus first-five median per case <= 128 MiB; no threshold change.", fontName="Helvetica", fontSize=10))
renderSVG.drawToFile(figure, str(out / "memory-timeline.svg"))
print(out / "memory-timeline.svg")
