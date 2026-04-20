/*{
  "name": "HSV",
  "description": "Shifts hue, saturation, and value of the input image. hue_shift rotates *existing* hues — it has no effect on grayscale sources (which have no hue to rotate). To colorize a grayscale source, use LutApply with a color LUT, a Composite blend with a colored layer, or generate the source in color to begin with.",
  "params": [
    {"name": "hue_shift",  "default": 0.0, "min": 0.0, "max": 360.0},
    {"name": "saturation", "default": 0.0, "min": -1.0, "max": 1.0},
    {"name": "value",      "default": 0.0, "min": -1.0, "max": 1.0}
  ]
}*/
fn rgb2hsv(c: vec3f) -> vec3f {
    let cmax = max(c.r, max(c.g, c.b));
    let cmin = min(c.r, min(c.g, c.b));
    let delta = cmax - cmin;

    var h: f32 = 0.0;
    if (delta > 0.00001) {
        if (cmax == c.r) {
            h = (c.g - c.b) / delta;
            if (h < 0.0) { h += 6.0; }
        } else if (cmax == c.g) {
            h = (c.b - c.r) / delta + 2.0;
        } else {
            h = (c.r - c.g) / delta + 4.0;
        }
        h /= 6.0;
    }

    var s: f32 = 0.0;
    if (cmax > 0.00001) {
        s = delta / cmax;
    }

    return vec3f(h, s, cmax);
}

fn hsv2rgb(c: vec3f) -> vec3f {
    let h = c.x * 6.0;
    let s = c.y;
    let v = c.z;

    let i = floor(h);
    let f = h - i;
    let p = v * (1.0 - s);
    let q = v * (1.0 - s * f);
    let t = v * (1.0 - s * (1.0 - f));

    let sector = i32(i) % 6;
    if (sector == 0) { return vec3f(v, t, p); }
    if (sector == 1) { return vec3f(q, v, p); }
    if (sector == 2) { return vec3f(p, v, t); }
    if (sector == 3) { return vec3f(p, q, v); }
    if (sector == 4) { return vec3f(t, p, v); }
    return vec3f(v, p, q);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let col = textureSample(inputTex, texSampler, input.uv);
    var hsv = rgb2hsv(col.rgb);

    hsv.x = fract(hsv.x + u.hue_shift / 360.0);
    hsv.y = clamp(hsv.y + u.saturation, 0.0, 1.0);
    hsv.z = clamp(hsv.z + u.value, 0.0, 1.0);

    let rgb = hsv2rgb(hsv);
    return vec4f(rgb, col.a);
}
