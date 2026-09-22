#!/usr/bin/env python3
"""Genera archivos STEP (AP214) de prueba sin depender de un kernel CAD.

Cubre los casos que mas importan para el mallador: caras planas con agujeros,
caras cilindricas completas con costura, y arcos de circulo.

    python3 tests/make_samples.py tests/samples
"""

import math
import os
import sys


class StepWriter:
    def __init__(self, name):
        self.name = name
        self.lines = []
        self.n = 0

    def add(self, text):
        self.n += 1
        self.lines.append("#%d=%s;" % (self.n, text))
        return "#%d" % self.n

    # --- geometria basica ---------------------------------------------------
    def point(self, x, y, z):
        return self.add("CARTESIAN_POINT('',(%.9f,%.9f,%.9f))" % (x, y, z))

    def direction(self, x, y, z):
        return self.add("DIRECTION('',(%.9f,%.9f,%.9f))" % (x, y, z))

    def axis(self, origin, z=(0, 0, 1), x=(1, 0, 0)):
        return self.add("AXIS2_PLACEMENT_3D('',%s,%s,%s)" % (
            self.point(*origin), self.direction(*z), self.direction(*x)))

    def vertex(self, xyz):
        return self.add("VERTEX_POINT('',%s)" % self.point(*xyz))

    def line_edge(self, va, vb, pa, pb):
        d = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        length = math.sqrt(sum(c * c for c in d)) or 1.0
        unit = tuple(c / length for c in d)
        vec = self.add("VECTOR('',%s,1.0)" % self.direction(*unit))
        line = self.add("LINE('',%s,%s)" % (self.point(*pa), vec))
        return self.add("EDGE_CURVE('',%s,%s,%s,.T.)" % (va, vb, line))

    def arc_edge(self, va, vb, center, radius, z=(0, 0, 1), x=(1, 0, 0)):
        circle = self.add("CIRCLE('',%s,%.9f)" % (self.axis(center, z, x), radius))
        return self.add("EDGE_CURVE('',%s,%s,%s,.T.)" % (va, vb, circle))

    def loop(self, oriented):
        refs = []
        for edge, forward in oriented:
            refs.append(self.add("ORIENTED_EDGE('',*,*,%s,%s)" % (edge, ".T." if forward else ".F.")))
        return self.add("EDGE_LOOP('',(%s))" % ",".join(refs))

    def face(self, surface, bounds, same_sense=True):
        refs = []
        for loop_ref, outer in bounds:
            kind = "FACE_OUTER_BOUND" if outer else "FACE_BOUND"
            refs.append(self.add("%s('',%s,.T.)" % (kind, loop_ref)))
        return self.add("ADVANCED_FACE('',(%s),%s,%s)" % (
            ",".join(refs), surface, ".T." if same_sense else ".F."))

    def plane(self, origin, z=(0, 0, 1), x=(1, 0, 0)):
        return self.add("PLANE('',%s)" % self.axis(origin, z, x))

    def cylinder(self, origin, radius, z=(0, 0, 1), x=(1, 0, 0)):
        return self.add("CYLINDRICAL_SURFACE('',%s,%.9f)" % (self.axis(origin, z, x), radius))

    # --- envoltura del archivo ---------------------------------------------
    def finish(self, faces, path):
        shell = self.add("CLOSED_SHELL('',(%s))" % ",".join(faces))
        brep = self.add("MANIFOLD_SOLID_BREP('',%s)" % shell)

        length_unit = self.add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
        angle_unit = self.add("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
        solid_unit = self.add("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
        uncertainty = self.add(
            "UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),%s,'distance_accuracy_value','')"
            % length_unit)
        context = self.add(
            "( GEOMETRIC_REPRESENTATION_CONTEXT(3) "
            "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((%s)) "
            "GLOBAL_UNIT_ASSIGNED_CONTEXT((%s,%s,%s)) REPRESENTATION_CONTEXT('','3D') )"
            % (uncertainty, length_unit, angle_unit, solid_unit))
        origin_axis = self.axis((0, 0, 0))
        rep = self.add("ADVANCED_BREP_SHAPE_REPRESENTATION('%s',(%s,%s),%s)"
                       % (self.name, origin_axis, brep, context))

        app = self.add("APPLICATION_CONTEXT('automotive design')")
        self.add("APPLICATION_PROTOCOL_DEFINITION('international standard',"
                 "'automotive_design',2000,%s)" % app)
        product = self.add("PRODUCT('%s','%s','',(%s))"
                           % (self.name, self.name,
                              self.add("PRODUCT_CONTEXT('',%s,'mechanical')" % app)))
        formation = self.add(
            "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE('','',%s,.NOT_KNOWN.)" % product)
        definition = self.add("PRODUCT_DEFINITION('design','',%s,%s)"
                              % (formation,
                                 self.add("PRODUCT_DEFINITION_CONTEXT('part definition',%s,'design')"
                                          % app)))
        shape = self.add("PRODUCT_DEFINITION_SHAPE('','',%s)" % definition)
        self.add("SHAPE_DEFINITION_REPRESENTATION(%s,%s)" % (shape, rep))

        header = [
            "ISO-10303-21;",
            "HEADER;",
            "FILE_DESCRIPTION((''),'2;1');",
            "FILE_NAME('%s.stp','2026-01-01T00:00:00',(''),(''),"
            "'stp-viewer sample generator','','');" % self.name,
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));",
            "ENDSEC;",
            "DATA;",
        ]
        footer = ["ENDSEC;", "END-ISO-10303-21;"]
        with open(path, "w") as fh:
            fh.write("\n".join(header + self.lines + footer) + "\n")
        return path


def box_faces(w, sx, sy, sz, origin=(0.0, 0.0, 0.0)):
    """Devuelve (caras, vertices, aristas) de una caja alineada a los ejes."""
    ox, oy, oz = origin
    corners = [
        (ox, oy, oz), (ox + sx, oy, oz), (ox + sx, oy + sy, oz), (ox, oy + sy, oz),
        (ox, oy, oz + sz), (ox + sx, oy, oz + sz), (ox + sx, oy + sy, oz + sz),
        (ox, oy + sy, oz + sz),
    ]
    verts = [w.vertex(c) for c in corners]

    def edge(a, b):
        return w.line_edge(verts[a], verts[b], corners[a], corners[b])

    bottom = [edge(0, 1), edge(1, 2), edge(2, 3), edge(3, 0)]
    top = [edge(4, 5), edge(5, 6), edge(6, 7), edge(7, 4)]
    side = [edge(0, 4), edge(1, 5), edge(2, 6), edge(3, 7)]
    return corners, verts, bottom, top, side


def make_box(path):
    w = StepWriter("caja")
    sx, sy, sz = 40.0, 25.0, 15.0
    corners, verts, bottom, top, side = box_faces(w, sx, sy, sz)

    faces = []
    faces.append(w.face(w.plane((0, 0, 0), (0, 0, -1), (1, 0, 0)),
                        [(w.loop([(bottom[0], True), (bottom[1], True),
                                  (bottom[2], True), (bottom[3], True)]), True)]))
    faces.append(w.face(w.plane((0, 0, sz), (0, 0, 1), (1, 0, 0)),
                        [(w.loop([(top[0], True), (top[1], True),
                                  (top[2], True), (top[3], True)]), True)]))
    wall = [
        (bottom[0], side[1], top[0], side[0], (0, -1, 0), (0, 0, 0)),
        (bottom[1], side[2], top[1], side[1], (1, 0, 0), (sx, 0, 0)),
        (bottom[2], side[3], top[2], side[2], (0, 1, 0), (sx, sy, 0)),
        (bottom[3], side[0], top[3], side[3], (-1, 0, 0), (0, sy, 0)),
    ]
    for b, up, t, down, normal, origin in wall:
        loop = w.loop([(b, True), (up, True), (t, False), (down, False)])
        faces.append(w.face(w.plane(origin, normal, (0, 0, 1)), [(loop, True)]))
    return w.finish(faces, path)


def hole_edges(w, cx, cy, z0, z1, r):
    """Arcos y costuras de un cilindro vertical completo (dos mitades)."""
    pb0, pb1 = (cx + r, cy, z0), (cx - r, cy, z0)
    pt0, pt1 = (cx + r, cy, z1), (cx - r, cy, z1)
    vb0, vb1 = w.vertex(pb0), w.vertex(pb1)
    vt0, vt1 = w.vertex(pt0), w.vertex(pt1)

    ba1 = w.arc_edge(vb0, vb1, (cx, cy, z0), r)
    ba2 = w.arc_edge(vb1, vb0, (cx, cy, z0), r)
    ta1 = w.arc_edge(vt0, vt1, (cx, cy, z1), r)
    ta2 = w.arc_edge(vt1, vt0, (cx, cy, z1), r)
    seam0 = w.line_edge(vb0, vt0, pb0, pt0)
    seam1 = w.line_edge(vb1, vt1, pb1, pt1)
    return ba1, ba2, ta1, ta2, seam0, seam1


def make_plate_with_hole(path):
    w = StepWriter("placa_con_agujero")
    sx, sy, sz = 80.0, 50.0, 8.0
    r = 10.0
    cx, cy = sx / 2, sy / 2
    corners, verts, bottom, top, side = box_faces(w, sx, sy, sz)
    ba1, ba2, ta1, ta2, seam0, seam1 = hole_edges(w, cx, cy, 0.0, sz, r)

    faces = []
    # Cara inferior con el agujero como contorno interno.
    faces.append(w.face(
        w.plane((0, 0, 0), (0, 0, -1), (1, 0, 0)),
        [(w.loop([(bottom[0], True), (bottom[1], True), (bottom[2], True), (bottom[3], True)]), True),
         (w.loop([(ba1, True), (ba2, True)]), False)]))
    faces.append(w.face(
        w.plane((0, 0, sz), (0, 0, 1), (1, 0, 0)),
        [(w.loop([(top[0], True), (top[1], True), (top[2], True), (top[3], True)]), True),
         (w.loop([(ta1, True), (ta2, True)]), False)]))

    wall = [
        (bottom[0], side[1], top[0], side[0], (0, -1, 0), (0, 0, 0)),
        (bottom[1], side[2], top[1], side[1], (1, 0, 0), (sx, 0, 0)),
        (bottom[2], side[3], top[2], side[2], (0, 1, 0), (sx, sy, 0)),
        (bottom[3], side[0], top[3], side[3], (-1, 0, 0), (0, sy, 0)),
    ]
    for b, up, t, down, normal, origin in wall:
        loop = w.loop([(b, True), (up, True), (t, False), (down, False)])
        faces.append(w.face(w.plane(origin, normal, (0, 0, 1)), [(loop, True)]))

    # Pared del agujero: el material queda fuera del cilindro, por eso .F.
    surf = w.cylinder((cx, cy, 0), r)
    faces.append(w.face(surf, [(w.loop([(ba1, True), (seam1, True), (ta1, False), (seam0, False)]), True)], False))
    faces.append(w.face(surf, [(w.loop([(ba2, True), (seam0, True), (ta2, False), (seam1, False)]), True)], False))
    return w.finish(faces, path)


def make_shaft(path):
    w = StepWriter("eje")
    r, h = 15.0, 60.0
    ba1, ba2, ta1, ta2, seam0, seam1 = hole_edges(w, 0.0, 0.0, 0.0, h, r)

    faces = []
    surf = w.cylinder((0, 0, 0), r)
    faces.append(w.face(surf, [(w.loop([(ba1, True), (seam1, True), (ta1, False), (seam0, False)]), True)]))
    faces.append(w.face(surf, [(w.loop([(ba2, True), (seam0, True), (ta2, False), (seam1, False)]), True)]))
    faces.append(w.face(w.plane((0, 0, 0), (0, 0, -1), (1, 0, 0)),
                        [(w.loop([(ba1, True), (ba2, True)]), True)]))
    faces.append(w.face(w.plane((0, 0, h), (0, 0, 1), (1, 0, 0)),
                        [(w.loop([(ta1, True), (ta2, True)]), True)]))
    return w.finish(faces, path)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/samples"
    os.makedirs(out, exist_ok=True)
    for name, fn in (("caja.stp", make_box),
                     ("placa_agujero.stp", make_plate_with_hole),
                     ("eje.stp", make_shaft)):
        print("generado:", fn(os.path.join(out, name)))


if __name__ == "__main__":
    main()
