#version 100
// Mesh vertex stage of the warp renderer (wobbly windows, magic lamp).
// `a_pos` is in the element's own physical pixels; `local` places the element
// in the frame and `matrix` is the frame's projection.

uniform mat3 matrix;
uniform mat3 local;

attribute vec2 a_pos;
attribute vec2 a_uv;

varying vec2 v_uv;
varying vec2 v_pos;

void main() {
    vec3 placed = local * vec3(a_pos, 1.0);
    v_uv = a_uv;
    v_pos = placed.xy;
    gl_Position = vec4(matrix * placed, 1.0);
}
