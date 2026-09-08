#version 450

layout(set = 0, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    vec4 viewPos;
    vec4 fogColor;
    vec4 fogParams;
    vec4 shadowParams;
    vec4 playerPos;   // xyz = player world position, w = horizontal speed
    vec4 playerWake;  // xyz = trailing player position (springback reference)
};

layout(push_constant) uniform Push {
    mat4 model;
    float waveAmp;
    float waveFreq;
    float waveSpeed;
    float liquidBasicType; // 0=water, 1=ocean, 2=magma, 3=slime
    // The rest belongs to the fragment stage, and is spelled the same way here
    // so the two declarations of one range agree. This said `float brightness`
    // where the fragment stage has a vec2, which is a field that does not exist
    // sitting at another field's offset - harmless only for as long as nobody
    // reads it.
    vec2 screenSize;
    vec2 depthRange;
} push;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;
layout(location = 3) out float WaveOffset;
layout(location = 4) out vec2 ScreenUV;

// --- Gerstner wave ---
// Coordinate system: X,Y = horizontal plane, Z = up (height)
// displacement.xy = horizontal, displacement.z = vertical
struct GerstnerResult {
    vec3 displacement;
    vec3 tangent;   // along X
    vec3 binormal;  // along Y
    float waveHeight; // raw wave height for foam
};

// Keep the six waves as direct calls. Mutable arrays indexed by a loop leave
// function-memory store_deref instructions in the PS4 compiler, which aborts
// during world loading. The coefficients and wave count remain unchanged.
void accumulateGerstnerWave(inout GerstnerResult r, vec2 pos, float time,
                           vec2 direction, float A, float w, float speed,
                           float steepness) {
    float WA = w * A;
    // Zero-amplitude water must remain flat without a division by zero.
    float Q = clamp(steepness / max(abs(WA) * 6.0, 1e-7), 0.0, 1.0);
    float phase = w * dot(direction, pos) + speed * w * time;
    float s = sin(phase);
    float c = cos(phase);
    r.displacement.xy += Q * A * direction * c;
    r.displacement.z += A * s;
    r.tangent.x -= Q * direction.x * direction.x * WA * s;
    r.tangent.y -= Q * direction.x * direction.y * WA * s;
    r.tangent.z += direction.x * WA * c;
    r.binormal.x -= Q * direction.x * direction.y * WA * s;
    r.binormal.y -= Q * direction.y * direction.y * WA * s;
    r.binormal.z += direction.y * WA * c;
    r.waveHeight += A * s;
}

GerstnerResult evaluateGerstnerWaves(vec2 pos, float time, float amp, float freq, float spd, float basicType) {
    GerstnerResult r;
    r.displacement = vec3(0.0);
    r.tangent = vec3(1.0, 0.0, 0.0);
    r.binormal = vec3(0.0, 1.0, 0.0);
    r.waveHeight = 0.0;

    // Magma/slime: simple slow undulation
    if (basicType >= 1.5) {
        float wave = sin(pos.x * freq * 0.5 + time * spd * 0.3) * 0.4
                   + sin(pos.y * freq * 0.3 + time * spd * 0.5) * 0.3;
        r.displacement.z = wave * amp * 0.5;
        float dx = cos(pos.x * freq * 0.5 + time * spd * 0.3) * freq * 0.5 * amp * 0.5 * 0.4;
        float dy = cos(pos.y * freq * 0.3 + time * spd * 0.5) * freq * 0.3 * amp * 0.5 * 0.3;
        r.tangent = vec3(1.0, 0.0, dx);
        r.binormal = vec3(0.0, 1.0, dy);
        r.waveHeight = wave;
        return r;
    }

    bool ocean = basicType > 0.5;
    accumulateGerstnerWave(r, pos, time, normalize(vec2(0.86, 0.51)),
        amp * (ocean ? 1.0 : 0.5), freq * (ocean ? 0.7 : 1.0),
        spd * (ocean ? 0.8 : 0.6), ocean ? 0.35 : 0.20);
    accumulateGerstnerWave(r, pos, time, normalize(vec2(-0.47, 0.88)),
        amp * (ocean ? 0.55 : 0.25), freq * (ocean ? 1.3 : 1.8),
        spd * (ocean ? 1.0 : 0.9), ocean ? 0.30 : 0.18);
    accumulateGerstnerWave(r, pos, time, normalize(vec2(0.32, -0.95)),
        amp * (ocean ? 0.30 : 0.15), freq * (ocean ? 2.1 : 3.0),
        spd * (ocean ? 1.3 : 1.2), ocean ? 0.25 : 0.15);
    accumulateGerstnerWave(r, pos, time, normalize(vec2(-0.93, -0.37)),
        amp * (ocean ? 0.18 : 0.08), freq * (ocean ? 3.4 : 4.5),
        spd * (ocean ? 1.6 : 1.5), ocean ? 0.20 : 0.12);
    accumulateGerstnerWave(r, pos, time, normalize(vec2(0.67, -0.29)),
        amp * (ocean ? 0.10 : 0.05), freq * (ocean ? 5.0 : 7.0),
        spd * (ocean ? 2.0 : 1.9), ocean ? 0.15 : 0.10);
    accumulateGerstnerWave(r, pos, time, normalize(vec2(-0.15, 0.74)),
        amp * (ocean ? 0.06 : 0.03), freq * (ocean ? 7.5 : 10.0),
        spd * (ocean ? 2.5 : 2.3), ocean ? 0.10 : 0.08);

    return r;
}

void main() {
    float time = fogParams.z;
    vec4 worldPos = push.model * vec4(aPos, 1.0);

    // Evaluate Gerstner waves using X,Y horizontal plane
    GerstnerResult waves = evaluateGerstnerWaves(
        vec2(worldPos.x, worldPos.y), time,
        push.waveAmp, push.waveFreq, push.waveSpeed, push.liquidBasicType
    );

    // Apply displacement: xy = horizontal, z = vertical (up)
    worldPos.x += waves.displacement.x;
    worldPos.y += waves.displacement.y;
    worldPos.z += waves.displacement.z;
    WaveOffset = waves.waveHeight; // raw wave height for fragment shader foam

    // Player interaction ripples - concentric waves emanating from player position
    vec2 rippleOrigin = playerPos.xy;
    float rippleStrength = fogParams.w;
    float d = length(worldPos.xy - rippleOrigin);
    float ripple = rippleStrength * 0.12 * exp(-d * 0.12) * sin(d * 2.5 - time * 6.0);
    worldPos.z += ripple;

    // Analytical normal from Gerstner tangent/binormal (cross product gives Z-up normal)
    Normal = normalize(cross(waves.tangent, waves.binormal));

    FragPos = worldPos.xyz;
    TexCoord = aTexCoord;
    vec4 clipPos = projection * view * worldPos;
    gl_Position = clipPos;
    vec2 ndc = clipPos.xy / max(clipPos.w, 1e-5);
    ScreenUV = ndc * 0.5 + 0.5;
}
