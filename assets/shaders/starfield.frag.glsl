#version 450

layout(location = 0) in float vBrightness;
layout(location = 1) in vec3 vColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Radius across the sprite: 0 at the centre, 1 at the edge.
    float r = length(gl_PointCoord - vec2(0.5)) * 2.0;
    if (r > 1.0) discard;

    // A point source is not a disc with a soft rim. The old profile held full
    // brightness out to 40% of the sprite and faded across all the rest, which
    // is a defocused blob at every size it is drawn. This is the shape a real
    // point makes through a lens: a tight core carrying nearly all the flux,
    // and a faint wide halo around it.
    float core = exp(-r * r * 26.0);
    float halo = 0.035 * exp(-r * r * 3.0);
    float profile = core + halo;

    // Blending is SRC_ALPHA, ONE, so the destination receives rgb * a and
    // brightness must therefore appear in exactly one of the two. It used to be
    // in both, which squared it: a star at the faint end of the range landed at
    // a tenth of its intended value and disappeared, leaving only the bright
    // ones - each of which was also drawn at the largest point size. Few, fat
    // and soft is the look that reads as bad focus.
    outColor = vec4(vColor * vBrightness * profile, 1.0);
}
