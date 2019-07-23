#version 430 compatability

#extension GL_ARB_compute_shader: enable
#extension GL_ARB_shader_storage_buffer_object: enable
#extension GL_ARB_compute_variable_group_size: enable

// TODO make configurable
const uint TEX_WIDTH = 1024u;
const uint TEX_HEIGHT = TEX_WIDTH;

// TODO make configurable
const uint MIN_WAVELENGTH = 380u;
const uint MAX_WAVELENGTH = 740u;
const uint NUM_WAVELENGTHS = MAX_WAVELENGTH - MIN_WAVELENGTH;

uniform image2D destTex;

// Size: 1440 bytes -> ~700,000 pixels per available gigabyte of ram
struct Pixel {
  uint intensityAtWavelengths[NUM_WAVELENGTHS];// [0..1000]
};

layout(std430, binding = 0) buffer Pixels {
  //Pixel pixels[TEX_WIDTH][TEX_HEIGHT];
  Pixel[] pixels;
};

Pixel getPixel(uvec2 position) {
  uint idx = position.y * TEX_WIDTH + position.x;
  return pixels[idx];
}


vec4 calcColor(uvec2 position) {
  Pixel p = getPixel(position);
  vec4 color = vec4(0.0);

  return color;
}

void main() {
  uint gid = gl_GlobalInvocationID.xy;
  imageStore(destTex, gid, calcColor(gid));
}