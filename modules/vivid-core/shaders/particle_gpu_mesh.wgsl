// Vivid Effects - GPU Mesh Particle Render Shader
// Reads directly from particle storage buffer for 3D mesh rendering

struct Particle {
    posX: f32, posY: f32, posZ: f32,
    velX: f32, velY: f32, velZ: f32,
    life: f32, maxLife: f32,
    size: f32, rotation: f32,
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,
    seed: f32, _pad: f32,
}

struct MeshUniforms {
    viewProj: mat4x4f,
    sizeStart: f32,
    sizeEnd: f32,
    fadeOut: f32,
    alignToVelocity: f32,
    colorStartR: f32, colorStartG: f32, colorStartB: f32, colorStartA: f32,
    colorEndR: f32, colorEndG: f32, colorEndB: f32, colorEndA: f32,
    meshScaleX: f32,
    meshScaleY: f32,
    meshScaleZ: f32,
    _pad: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) worldNormal: vec3f,
}

@group(0) @binding(0) var<uniform> u: MeshUniforms;
@group(0) @binding(1) var<storage, read> particles: array<Particle>;

@vertex
fn vs_main(
    @location(0) localPos: vec3f,
    @builtin(instance_index) instanceIdx: u32
) -> VertexOutput {
    let p = particles[instanceIdx];

    // Skip dead particles
    if (p.life <= 0.0) {
        var output: VertexOutput;
        output.position = vec4f(-100.0, -100.0, -100.0, 1.0);
        output.color = vec4f(0.0);
        output.worldNormal = vec3f(0.0, 0.0, 1.0);
        return output;
    }

    // Age ratio
    let age = 1.0 - (p.life / p.maxLife);

    // Interpolate size
    let sizeScale = mix(u.sizeStart, u.sizeEnd, age);

    // Interpolate color
    let colorStart = vec4f(u.colorStartR, u.colorStartG, u.colorStartB, u.colorStartA);
    let colorEnd = vec4f(u.colorEndR, u.colorEndG, u.colorEndB, u.colorEndA);
    var color = mix(colorStart, colorEnd, age);
    color *= vec4f(p.colorR, p.colorG, p.colorB, p.colorA);

    // Fade out
    if (u.fadeOut > 0.5) {
        let fadeStart = 0.7;
        if (age > fadeStart) {
            color.a *= 1.0 - (age - fadeStart) / (1.0 - fadeStart);
        }
    }

    // Build transform matrix
    var scaledPos = localPos * vec3f(u.meshScaleX, u.meshScaleY, u.meshScaleZ) * sizeScale;
    var worldPos: vec3f;
    var normal = vec3f(0.0, 0.0, 1.0);

    if (u.alignToVelocity > 0.5) {
        // Velocity alignment - orient mesh along velocity vector
        let vel = vec3f(p.velX, p.velY, p.velZ);
        let speed = length(vel);

        if (speed > 0.001) {
            let forward = normalize(vel);

            // Find a suitable up vector (avoid parallel)
            var up = vec3f(0.0, 1.0, 0.0);
            if (abs(dot(forward, up)) > 0.99) {
                up = vec3f(0.0, 0.0, 1.0);
            }

            let right = normalize(cross(up, forward));
            up = cross(forward, right);

            // Transform local position (local Z = forward direction)
            worldPos = vec3f(p.posX, p.posY, p.posZ)
                     + right * scaledPos.x
                     + up * scaledPos.y
                     + forward * scaledPos.z;

            normal = forward;
        } else {
            worldPos = vec3f(p.posX, p.posY, p.posZ) + scaledPos;
        }
    } else {
        // Simple translation, no rotation
        worldPos = vec3f(p.posX, p.posY, p.posZ) + scaledPos;
    }

    var output: VertexOutput;
    output.position = u.viewProj * vec4f(worldPos, 1.0);
    output.color = color;
    output.worldNormal = normal;
    return output;
}

@fragment
fn fs_lit(input: VertexOutput) -> @location(0) vec4f {
    let lightDir = normalize(vec3f(1.0, 2.0, 1.5));
    let ambient = 0.3;
    let diffuse = max(dot(input.worldNormal, lightDir), 0.0);
    let lighting = ambient + diffuse * 0.7;
    return vec4f(input.color.rgb * lighting, input.color.a);
}

@fragment
fn fs_unlit(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
