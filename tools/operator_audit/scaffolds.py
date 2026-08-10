"""Per-category minimal test-graph builders for the operator audit (ADR-0042).

The control API exposes each operator's ports as `{dir, name}` (no explicit type), so we categorise by
port-NAME convention. Given the running app's `Vivid` client and one operator descriptor (from
`list_operators`), `build_scaffold` (a) satisfies the operator's inputs with canonical source nodes and
(b) routes its output through the right sink chain to a terminal node whose `texture` output can be wired
into Output and captured.
"""


def in_ports(op):
    return [p["name"] for p in op.get("ports", []) if p.get("dir") == "in"]


def out_ports(op):
    return [p["name"] for p in op.get("ports", []) if p.get("dir") == "out"]


def input_kind(name: str) -> str:
    """What kind of source satisfies an input port, by name convention."""
    n = name.lower()
    if n.startswith("scene"):
        return "scene"
    if n == "instances":
        return "instances"
    if n == "mesh":
        return "mesh"
    if n == "signal":
        return "signal"
    if n == "clock":
        return "clock"
    if (n.startswith("pos_") or n.startswith("scale_") or n.startswith("color_")
            or n.startswith("rot") or n in ("amount", "attractors", "values", "spectrum")):
        return "lane"
    return "texture"   # input, source, displace, map, a, b, in_0..


def output_kind(op) -> str:
    """The transport an operator emits, from its output port names."""
    s = {n.lower() for n in out_ports(op)}
    if "texture" in s:
        return "texture"
    if "scene" in s:
        return "scene"
    if "instances" in s:
        return "instances"
    if "mesh" in s:
        return "mesh"
    if "signal" in s:
        return "signal"
    if (s & {"values", "spectrum", "step", "phase", "gate"}) or any(n.startswith("color_") for n in s):
        return "lane"
    if "output" in s:
        return "audio"
    return "unknown"


class Sources:
    """Lazily create + cache one canonical source node per kind, per scaffold."""

    _CANON = {
        "scene": "Shape3D",
        "instances": "InstanceGrid",
        "mesh": "MeshLoad",
        "signal": "Notes",
        "clock": "Clock",
        "lane": "LaneRamp",
        # `Gradient` is no longer in the catalog — this raised "unknown op" for EVERY texture-input
        # op (Blur, CRT, Composite, ...), the same way audit.py's baseline did. NoiseField is the
        # equivalent: an input-free texture source with plenty of contrast to sweep against.
        "texture": "NoiseField",
    }

    def __init__(self, v):
        self.v = v
        self.cache = {}

    def get(self, kind):
        if kind in self.cache:
            return self.cache[kind]
        op = self._CANON.get(kind)
        node = self.v.add_node(op) if op else None
        self.cache[kind] = node
        return node


def build_scaffold(v, op, sources: Sources):
    """Build a minimal renderable graph for `op`. Returns (op_node, terminal_node).

    terminal_node has a `texture` output to wire into Output. Returns terminal_node=None for audio/unknown
    ops (they can't be GPU-captured). Raises only on hard client errors; optional/incompatible inputs are
    tolerated so a partially-wireable op still gets audited.
    """
    op_node = v.add_node(op["name"])

    # Satisfy each input with a canonical source (dst port index = position among the op's inputs).
    for idx, pin in enumerate(in_ports(op)):
        src = sources.get(input_kind(pin))
        if src is None:
            continue
        try:
            v.connect(op_node, src, idx, 0)
        except Exception:
            pass  # optional / incompatible input — leave it unconnected

    ok = output_kind(op)
    shape = sources.get("scene")

    if ok == "texture":
        return op_node, op_node
    if ok == "mesh":
        mr = v.add_node("MeshRender"); v.connect(mr, op_node, 0, 0)
        return op_node, mr
    if ok == "scene":
        # A scene needs geometry AND a light to render. If the op IS a light, add a Shape3D to light;
        # otherwise the op is the geometry, so just add a light — adding a SECOND shape would dilute a
        # geometry op's own param effects (half the frame wouldn't change when you sweep its params).
        merge = v.add_node("SceneMerge")
        v.connect(merge, op_node, 0, 0)                 # scene_a <- op under test
        if op["name"] == "Light3D":
            v.connect(merge, shape, 1, 0)               # geometry for the light to reveal
        else:
            v.connect(merge, v.add_node("Light3D"), 1, 0)   # a light so the op's geometry is lit
        r = v.add_node("Render3D"); v.connect(r, merge, 0, 0)
        return op_node, r
    if ok == "instances":
        inst = v.add_node("Instancer3D"); v.connect(inst, shape, 0, 0); v.connect(inst, op_node, 1, 0)
        r = v.add_node("Render3D"); v.connect(r, inst, 0, 0)
        return op_node, r
    if ok == "signal":
        sig = v.add_node("InstancesFromSignal"); v.connect(sig, op_node, 0, 0)
        inst = v.add_node("Instancer3D"); v.connect(inst, shape, 0, 0); v.connect(inst, sig, 1, 0)
        r = v.add_node("Render3D"); v.connect(r, inst, 0, 0)
        return op_node, r
    if ok == "lane":
        lanes = v.add_node("InstancesFromLanes")
        v.connect(lanes, op_node, 4, 0)   # op's primary lane out -> scale_y (input index 4) => bar heights vary
        inst = v.add_node("Instancer3D"); v.connect(inst, shape, 0, 0); v.connect(inst, lanes, 1, 0)
        r = v.add_node("Render3D"); v.connect(r, inst, 0, 0)
        return op_node, r

    return op_node, None   # audio / unknown — not GPU-auditable
