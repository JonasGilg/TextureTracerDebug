#version 430 compatibility
#extension GL_ARB_compute_shader: enable
#extension GL_ARB_shader_storage_buffer_object: enable
#extension GL_ARB_compute_variable_group_size: enable

/*uint counter = 0;
const uint DEBUG_BUFFER_SIZE = 16;
layout(std430, binding = 9) buffer Debug {
  float floats[DEBUG_BUFFER_SIZE];
  vec2 vec2s[DEBUG_BUFFER_SIZE];

  uint uints[DEBUG_BUFFER_SIZE];
  uvec2 uvec2s[DEBUG_BUFFER_SIZE];
};*/

// TODO make configurable
const uint MIN_WAVELENGTH = 380u;
const uint MAX_WAVELENGTH = 740u;
const uint NUM_WAVELENGTHS = MAX_WAVELENGTH - MIN_WAVELENGTH;

// Size: 24 bytes -> ~40,000,000 photons per available gigabyte of ram
struct Photon {
  vec2 position;// m
  vec2 direction;// normalized
  uint wavelength;// nm
  float intensity;// 0..1 should start at 1
};

// maybe generate in GPU with rng?
layout(std430, binding = 0) buffer Photons {
  Photon photons[];
};

layout(std430, binding = 1) buffer RefractiveIndices {
  float[][NUM_WAVELENGTHS] refractiveIndicesAtAltitudes;// DX steps
};

layout(std430, binding = 2) buffer Densities {
  float[] densitiesAtAltitudes;
};

struct Planet {
  float radius;// m
  float atmosphericHeight;// m
  float seaLevelMolecularNumberDensity;// cm^−3
};

uniform Planet planet;

// TODO make configurable
const float DL = 1000.0;// m
const float DX = 10.0;// m

layout (local_size_variable) in;

float densityAtAltitude(float altitude) {
  return densitiesAtAltitudes[uint(altitude)];
}

float refractiveIndexAtSeaLevel(uint wavelength) {
  return refractiveIndicesAtAltitudes[0][wavelength - MIN_WAVELENGTH];
}

float refractiveIndexAtAltitude(float altitude, uint wavelength) {
  return refractiveIndicesAtAltitudes[uint(altitude)][wavelength - MIN_WAVELENGTH];
}

float partialRefractiveIndex(float altitude, float altitudeDelta, uint wavelength) {
  float refrIndexPlusDelta = refractiveIndexAtAltitude(altitudeDelta, wavelength);
  float refrIndex = refractiveIndexAtAltitude(altitude, wavelength);

  return (refrIndexPlusDelta - refrIndex) / DX;
}

/// Moves the photon to its next location.
void traceRay(inout Photon photon) {
  float altitude = length(photon.position) - planet.radius;
  float altitudeDelta = length(photon.position + vec2(DX, DX)) - planet.radius;

  if (altitude < planet.atmosphericHeight && altitudeDelta < planet.atmosphericHeight) {
    float ni = refractiveIndexAtAltitude(altitude, photon.wavelength);
    float dn = partialRefractiveIndex(altitude, altitudeDelta, photon.wavelength);

    float ni1 = ni + dn;
    photon.direction = ((ni * photon.direction) + (dn * DL)) / ni1;
    photon.direction = normalize(photon.direction);
  }

  photon.position += (DL * photon.direction);
}

float molecularNumberDensityAtAltitude(float altitude) {
  float seaLevelDensity = densityAtAltitude(0.0);
  return planet.seaLevelMolecularNumberDensity * (densityAtAltitude(altitude) / seaLevelDensity);
}

float rayleighScatteringCrossSection(uint wavelength) {
  float wavelengthInCM = float(wavelength) * 1.0e-7;
  float wavelengthInCM2 = wavelengthInCM * wavelengthInCM;
  float wavelengthInCM4 = wavelengthInCM2 * wavelengthInCM2;

  float refractiveIndex = refractiveIndexAtSeaLevel(wavelength);
  float refractiveIndex2 = refractiveIndex * refractiveIndex;

  float molecularNumberDensity = molecularNumberDensityAtAltitude(0.0);
  float molecularNumberDensity2 = molecularNumberDensity * molecularNumberDensity;

  const float kingCorrelationFactor = 1.05;
  const float PI_F = 3.14159;
  const float PI_F_3 = PI_F * PI_F * PI_F;

  float dividend = 24.0 * PI_F_3 * pow(refractiveIndex2 - 1.0, 2);
  float divisor = wavelengthInCM4 * molecularNumberDensity2 * pow(refractiveIndex2 + 2, 2);
  return (dividend / divisor) * kingCorrelationFactor;
}

// TODO maybe precompute in a 2D map?
float rayleighVolumeScatteringCoefficient(float altitude, uint wavelength) {
  float sigma = rayleighScatteringCrossSection(wavelength);
  float mnd = molecularNumberDensityAtAltitude(altitude);
  return mnd * sigma;
}

/// Applies rayleigh scattering to the photon for this step.
void attenuateLight(inout Photon photon, vec2 oldPosition) {
  float altitude = length(oldPosition) - planet.radius;

  float beta = rayleighVolumeScatteringCoefficient(altitude, photon.wavelength);

  // TODO don't know what to do with this for now... maybe make it configurable per planet?
  float alpha =  0.0;

  photon.intensity = photon.intensity * exp(-(alpha + beta) * DL);
}

/// Does a single step of the ray tracing. It moves the photon to the next location and applies
/// rayleigh scattering to it.
void tracePhoton(inout Photon photon) {
  vec2 oldPosition = photon.position;

  traceRay(photon);
  attenuateLight(photon, oldPosition);
}

void main() {
  uint gid = gl_GlobalInvocationID.x;
  if (gid >= photons.length()) return;

  Photon photon = photons[gid];

  bool enteredAtmosphere = false;
  bool exitedAtmosphere = false;

  float atmosphereRadius = planet.radius + planet.atmosphericHeight;

  while (!exitedAtmosphere && length(photon.position) > planet.radius) {
    tracePhoton(photon);

    if (!enteredAtmosphere && length(photon.position) < atmosphereRadius) {
      enteredAtmosphere = true;
    }

    if (enteredAtmosphere && length(photon.position) > atmosphereRadius) {
      exitedAtmosphere = true;
    }
  }

  if (length(photon.position) <= planet.radius || !enteredAtmosphere) {
    photon.intensity = -1.0;
  }

  photons[gid] = photon;
}