"""Parses .mod3 files produced by MHW VRoid Hunter with AsteriskAmpersand's reference
Mod3-MHW-Importer code (https://github.com/AsteriskAmpersand/Mod3-MHW-Importer) to make sure an
independent reader agrees with our writer.

usage: python3 crosscheck_reference.py <path to Mod3-MHW-Importer checkout> <file.mod3> [...]
"""
import os
import sys
import types


def install_mathutils_stub():
    m = types.ModuleType("mathutils")

    class Vector(list):
        def __init__(self, it=()):
            super().__init__(it)

        def freeze(self):
            return tuple(self)

        def __sub__(self, o):
            return Vector(a - b for a, b in zip(self, o))

        def __add__(self, o):
            return Vector(a + b for a, b in zip(self, o))

        def __getitem__(self, i):
            r = list.__getitem__(self, i)
            return Vector(r) if isinstance(i, slice) else r

    class Matrix(list):
        def __init__(self, rows=()):
            super().__init__([list(r) for r in rows])

    m.Vector, m.Matrix = Vector, Matrix
    sys.modules["mathutils"] = m


def main():
    root, files = sys.argv[1], sys.argv[2:]
    install_mathutils_stub()
    # register the checkout as a package without running its Blender-only __init__.py
    pkg = "mod3_reference"
    package = types.ModuleType(pkg)
    package.__path__ = [os.path.abspath(root)]
    sys.modules[pkg] = package
    mod3 = __import__(pkg + ".mod3.Mod3", fromlist=["Mod3"])
    filelike = __import__(pkg + ".common.FileLike", fromlist=["FileLike"])
    ok = True
    for f in files:
        data = open(f, "rb").read()
        model = mod3.Mod3()
        model.marshall(filelike.FileLike(data))
        h = model.Header
        verts = sum(len(m.Vertices) for m in model.MeshParts.Meshes)
        faces = sum(len(m.Faces) for m in model.MeshParts.Meshes)
        print("%s: bones=%d meshes=%d materials=%d vertices=%d faces=%d header.vertexCount=%d header.faceCount=%d"
              % (os.path.basename(f), h.boneCount, h.meshCount, h.materialCount, verts, faces, h.vertexCount, h.faceCount))
        if verts != h.vertexCount or faces * 3 != h.faceCount:
            print("  MISMATCH in counts"); ok = False
        for mi, mesh in enumerate(model.MeshParts.Meshes):
            n = len(mesh.Vertices)
            for face in mesh.Faces:
                for v in (face.v1, face.v2, face.v3):
                    if not (mesh.Header.vertexSub <= v < mesh.Header.vertexSub + n):
                        print("  mesh %d: face index %d outside its vertex range" % (mi, v)); ok = False
                        break
            for vert in mesh.Vertices[:50]:
                w = vert.weights.weights if hasattr(vert, "weights") else [1]
                if abs(sum(w) - 1) > 0.01 or min(w) < -0.01:
                    print("  mesh %d: bad weights %s" % (mi, w)); ok = False
                    break
                for b in vert.boneIds.boneIds:
                    if b >= h.boneCount:
                        print("  mesh %d: bone id %d out of range" % (mi, b)); ok = False
            boxes = mesh.Header.boundingBoxCount
            if boxes == 0:
                print("  mesh %d has no bounding boxes" % mi); ok = False
        remainder = model.Trailing.Remnants
        print("  trailing bytes: %d, bounding boxes: %d" % (len(remainder), model.MeshParts.BoundingBoxes.count))
    print("REFERENCE PARSER OK" if ok else "REFERENCE PARSER FOUND PROBLEMS")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
