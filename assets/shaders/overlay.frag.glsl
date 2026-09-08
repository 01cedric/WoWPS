#version 450

// Full-screen colour overlay (underwater tint) with a waterline.
// Uses postprocess.vert.glsl as vertex shader (fullscreen triangle, no vertex input).

layout(push_constant) uniform Push {
    mat4 invViewProj;  // clip -> world, for the point each pixel sees at the near plane
    vec4 color;        // rgb = tint colour, a = opacity when fully submerged
    vec4 plane;        // w = water surface height in world units (xyz spare)
    vec4 params;       // x = softness in world units, y = time,
                       // z = ripple amplitude in world units, w = 0 disables the split
} push;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    // Where the split falls is worked out from the surface itself, not from a
    // line pushed in screen space.
    //
    // It used to be an NDC height anchored to the water plane's horizon. A
    // horizon is where an infinite plane goes at infinity, which is only where
    // the surface appears to be when the eye is level with it - so the seam
    // drifted away from the water as soon as the camera pitched, and looking
    // down it left the screen entirely. That is why the line showed up looking
    // up and nowhere else.
    //
    // Each pixel instead asks where its own ray enters the world - the point it
    // sees on the near plane, which is the plane the water is actually cutting
    // across - and compares that point's height against the surface. The seam
    // then lands on the water at any pitch, roll or height, because it is the
    // same test the water is passing or failing.
    vec4 clip = vec4(vUV.x * 2.0 - 1.0, vUV.y * 2.0 - 1.0, 0.0, 1.0);  // z=0: the near plane
    vec4 world = push.invViewProj * clip;
    world /= (abs(world.w) > 1e-6) ? world.w : 1e-6;

    // A touch of ripple on the surface height, so the seam is not a perfect
    // straight cut. In world units, so it does not change with the view.
    float waterZ = push.plane.w
                 + (sin(world.x * 1.7 + push.params.y * 1.9)
                  + sin(world.y * 2.3 - push.params.y * 1.4)) * push.params.z;

    float below = waterZ - world.z;      // positive: this pixel's ray starts under water
    float soft = max(push.params.x, 1e-4);

    // params.w == 0 means fully submerged with no seam left to draw: tint
    // everything rather than testing a plane that is behind the camera.
    float submerged = (push.params.w < 0.5) ? 1.0 : smoothstep(-soft, soft, below);

    // The meniscus: water climbing the glass at the seam. A slim band centred
    // on the surface, darker and denser than the water either side of it.
    // Without it the crossing is a cut between two flat colours, which is what
    // a split screen looks like and not what a lens half in the water looks
    // like.
    float meniscus = (push.params.w < 0.5)
                   ? 0.0
                   : (1.0 - smoothstep(0.0, soft * 1.8, abs(below)));

    float alpha = clamp(push.color.a * submerged + meniscus * 0.55, 0.0, 1.0);
    vec3 rgb = push.color.rgb * (1.0 - 0.45 * meniscus);
    outColor = vec4(rgb, alpha);
}
