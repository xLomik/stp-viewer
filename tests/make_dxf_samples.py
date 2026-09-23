#!/usr/bin/env python3
"""Genera DXF de prueba con ezdxf (pip install ezdxf).

    python3 tests/make_dxf_samples.py tests/samples

plano_brida.dxf   plano 2D con todo lo que tiene un plano real: arcos en
                  polilineas, elipse, spline, bloques con punto base corrido,
                  cotas, TEXT, MTEXT con formato, simbolos y un marco en
                  espacio papel que no debe mezclarse con el modelo.
pieza_3d.dxf      caja de 3DFACE: tiene que seguir viendose en 3D.
"""
import math
import sys
from pathlib import Path

import ezdxf


# Estilo de cota en milimetros (el EZDXF de fabrica esta pensado para metros).
COTA_MM = {"dimtxt": 3.5, "dimasz": 3.0, "dimlfac": 1.0, "dimexe": 1.5, "dimexo": 1.0,
           "dimgap": 1.0, "dimdec": 0}


def plano_brida(path: Path) -> None:
    doc = ezdxf.new("R2018", setup=True)
    doc.units = ezdxf.units.MM  # ezdxf declara metros si no se dice nada
    doc.layers.add("CONTORNO", color=7)
    doc.layers.add("EJES", color=1, linetype="CENTER")
    doc.layers.add("COTAS", color=3)
    doc.layers.add("TEXTOS", color=2)
    doc.layers.add("OCULTA", color=4).off()
    msp = doc.modelspace()

    # Contorno exterior: rectangulo con esquinas redondeadas (bulge).
    r = 12.0
    w, h = 220.0, 140.0
    b = math.tan(math.radians(90) / 4)
    msp.add_lwpolyline(
        [
            (r, 0, 0, 0, 0), (w - r, 0, 0, 0, b), (w, r, 0, 0, 0), (w, h - r, 0, 0, b),
            (w - r, h, 0, 0, 0), (r, h, 0, 0, b), (0, h - r, 0, 0, 0), (0, r, 0, 0, b),
        ],
        format="xyseb", close=True, dxfattribs={"layer": "CONTORNO"},
    )
    # Brida central y agujeros de pernos (bloque con base en su centro corrido).
    cx, cy = 80.0, 70.0
    msp.add_circle((cx, cy), 45, dxfattribs={"layer": "CONTORNO"})
    msp.add_circle((cx, cy), 22, dxfattribs={"layer": "CONTORNO"})
    msp.add_circle((cx, cy), 35, dxfattribs={"layer": "EJES"})
    perno = doc.blocks.new("PERNO", base_point=(100, 100))
    perno.add_circle((100, 100), 5)
    perno.add_line((92, 100), (108, 100))
    perno.add_line((100, 92), (100, 108))
    for k in range(6):
        a = math.radians(60 * k + 30)
        msp.add_blockref("PERNO", (cx + 35 * math.cos(a), cy + 35 * math.sin(a)),
                         dxfattribs={"rotation": 60 * k + 30, "layer": "CONTORNO"})
    # Ranura de la derecha, elipse y spline de un perfil.
    msp.add_lwpolyline([(160, 95, 0, 0, 0), (160, 45, 0, 0, 1), (180, 45, 0, 0, 0),
                        (180, 95, 0, 0, 1)], format="xyseb", close=True,
                       dxfattribs={"layer": "CONTORNO"})
    msp.add_ellipse((200, 118), major_axis=(12, 0), ratio=0.45,
                    dxfattribs={"layer": "CONTORNO"})
    msp.add_spline([(130, 10), (145, 28), (160, 12), (178, 30), (200, 14)],
                   dxfattribs={"layer": "CONTORNO"})
    # Ejes.
    msp.add_line((cx - 55, cy), (cx + 55, cy), dxfattribs={"layer": "EJES"})
    msp.add_line((cx, cy - 55), (cx, cy + 55), dxfattribs={"layer": "EJES"})
    # Geometria en una capa apagada: no debe verse.
    msp.add_line((0, 0), (w, h), dxfattribs={"layer": "OCULTA"})

    # Cotas, dibujadas por ezdxf en su bloque *D como hace AutoCAD.
    dim = msp.add_linear_dim(base=(0, -18), p1=(0, 0), p2=(w, 0),
                             dimstyle="EZDXF", override=COTA_MM, dxfattribs={"layer": "COTAS"})
    dim.render()
    dim = msp.add_linear_dim(base=(w + 18, 0), p1=(w, 0), p2=(w, h), angle=90,
                             dimstyle="EZDXF", override=COTA_MM, dxfattribs={"layer": "COTAS"})
    dim.render()
    dim = msp.add_diameter_dim(center=(cx, cy), radius=45, angle=45,
                               dimstyle="EZDXF", override=COTA_MM, dxfattribs={"layer": "COTAS"})
    dim.render()

    # Textos.
    msp.add_text("BRIDA DE ACOPLE  %%c90", height=7,
                 dxfattribs={"layer": "TEXTOS"}).set_placement((0, h + 12))
    msp.add_text("Tolerancia %%p0,05  -  chaflan 45%%d", height=3.5,
                 dxfattribs={"layer": "TEXTOS"}).set_placement((0, h + 4))
    msp.add_text("Ranura", height=4, rotation=90,
                 dxfattribs={"layer": "TEXTOS"}).set_placement((152, 70), align=ezdxf.enums.TextEntityAlignment.MIDDLE_CENTER)
    msp.add_mtext("{\\fArial|b1;NOTAS:}\\P1. Material: acero A36\\P2. Pintura epoxica\\P"
                  "3. Revisi\\U+00F3n de planitud", dxfattribs={
                      "layer": "TEXTOS", "char_height": 3.5, "insert": (135, 132),
                      "attachment_point": 1})

    # Espacio papel: marco y cajetin a otra escala. Si se mezclara con el
    # modelo, el encuadre quedaria enorme.
    psp = doc.layouts.get("Layout1")
    psp.add_lwpolyline([(0, 0), (420, 0), (420, 297), (0, 297)], close=True)
    psp.add_text("CAJETIN", height=5).set_placement((330, 10))
    doc.saveas(path)


def pieza_3d(path: Path) -> None:
    doc = ezdxf.new("R2010")
    doc.units = ezdxf.units.MM
    msp = doc.modelspace()
    x, y, z = 60.0, 40.0, 25.0
    v = [(0, 0, 0), (x, 0, 0), (x, y, 0), (0, y, 0), (0, 0, z), (x, 0, z), (x, y, z), (0, y, z)]
    for f in [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]:
        msp.add_3dface([v[i] for i in f])
    doc.saveas(path)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "tests/samples")
    out.mkdir(parents=True, exist_ok=True)
    plano_brida(out / "plano_brida.dxf")
    pieza_3d(out / "pieza_3d.dxf")
    print("DXF de prueba en", out)


if __name__ == "__main__":
    main()
