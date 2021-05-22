#version 100

uniform sampler2D gm;
uniform sampler2D vt;
varying mediump vec2 tex_position;

void main() {
  lowp float r = texture2D(gm, tex_position).b;
  lowp float g = texture2D(gm, tex_position).g;
  lowp float b = texture2D(gm, tex_position).r;
  gl_FragColor = vec4(r, g, b, 1.0);
}