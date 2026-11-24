#version 450
// Samples an imported AHardwareBuffer through its YCbCr conversion (an
// immutable combined image sampler) at an affine map of the target's uv.
layout(set = 0, binding = 0) uniform sampler2D source;
layout(push_constant) uniform Transform {
    vec4 row0;  // source.u = dot(row0.xyz, (uv, 1))
    vec4 row1;  // source.v = dot(row1.xyz, (uv, 1))
} transform;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
    const vec3 p = vec3(uv, 1.0);
    const vec2 s = vec2(dot(transform.row0.xyz, p), dot(transform.row1.xyz, p));
    color = vec4(texture(source, s).rgb, 1.0);
}
