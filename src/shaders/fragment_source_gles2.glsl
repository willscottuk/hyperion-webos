#version 100

uniform sampler2D tex_gm;
uniform sampler2D tex_vt;
varying mediump vec2 tex_position;

void main() {
  mediump vec4 gm_color = texture2D(tex_gm, tex_position);
  mediump vec4 vt_color = texture2D(tex_vt, vec2(tex_position.s, 1.0 - tex_position.t));
  gl_FragColor = vec4(gm_color.b, gm_color.g, gm_color.r, gm_color.a) + vt_color;
}