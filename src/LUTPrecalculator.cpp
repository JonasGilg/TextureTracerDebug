////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR                               //
//      and may be used under the terms of the MIT license. See the LICENSE file for details.     //
//                        Copyright: (c) 2019 German Aerospace Center (DLR)                       //
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "LUTPrecalculator.hpp"
#include <GL/glew.h>
#include <array>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>

char const *const REFRACTIVE_INDICES_COMPUTE_SHADER = R"glsl(#version 430 compatibility
#extension GL_ARB_compute_shader: enable
#extension GL_ARB_shader_storage_buffer_object: enable
#extension GL_ARB_compute_variable_group_size: enable

struct Planet {
  float atmosphericHeight;              // m
  float gravitationAcceleration;        // m/s^2
  float molarMass;                      // kg / mol
  float seaLevelMolecularNumberDensity; // cm^−3
};

struct AtmosphericLayer {
  float baseTemperature;      // K
  float temperatureLapseRate; // K / m
  float staticPressure;       // Pa
  float baseHeight;           // m
};

struct SellmeierCoefficients {
  float a;
  uint numTerms;
  vec2 terms[8];
};

const uint MIN_WAVELENGTH = 380;
const uint MAX_WAVELENGTH = 740;
const uint NUM_WAVELENGTHS = MAX_WAVELENGTH - MIN_WAVELENGTH;

const float DX = 1.0;                              // m
const float IDEAL_UNIVERSAL_GAS_CONSTANT = 8.31447; // J / (mol * K)

uniform Planet planet;
uniform SellmeierCoefficients sellmeierCoefficients;

layout(std430, binding = 0) buffer RefractiveIndices {
  float[][NUM_WAVELENGTHS] refractiveIndicesAtAltitudes; // DX steps
};

layout(std430, binding = 1) buffer Densities {
  float[] densitiesAtAltitudes;
};

layout (local_size_variable) in;

// TODO replace with user defined SSBO lookup
AtmosphericLayer layerAtAltitude(float altitude) {
  if (altitude < 11000.0) {
    return AtmosphericLayer(288.15, -0.0065, 101325.0, 0.0);
  } else if (altitude < 20000.0) {
    return AtmosphericLayer(216.65, 0.0, 22632.10, 11000.0);
  } else if (altitude < 32000.0) {
    return AtmosphericLayer(216.65, 0.001, 5474.89, 20000.0);
  } else {
    return AtmosphericLayer(228.65, 0.0028, 868.02, 32000.0);
  }
}


float pressureAtAltitude(float altitude) {
  AtmosphericLayer layer = layerAtAltitude(altitude);

  if (layer.temperatureLapseRate != 0.0) {
    float divisor =
    layer.baseTemperature + layer.temperatureLapseRate * (altitude - layer.baseHeight);

    float exponent = (planet.gravitationAcceleration * planet.molarMass)
    / (IDEAL_UNIVERSAL_GAS_CONSTANT * layer.temperatureLapseRate);
    return layer.staticPressure * pow(layer.baseTemperature / divisor, exponent);
  } else {
    return layer.staticPressure * exp((-planet.gravitationAcceleration * planet.molarMass
    * (altitude -layer.baseHeight)) / (IDEAL_UNIVERSAL_GAS_CONSTANT * layer.baseTemperature));
  }
}

float temperatureAtAltitude(float altitude) {
  AtmosphericLayer layer = layerAtAltitude(altitude);
  return layer.baseTemperature + (layer.temperatureLapseRate * (altitude - layer.baseHeight));
}

float densityAtAltitude(float altitude) {
  float pressure = pressureAtAltitude(altitude);
  float temp = temperatureAtAltitude(altitude);
  return (pressure * planet.molarMass) / (IDEAL_UNIVERSAL_GAS_CONSTANT * temp);
}

// TODO Eliminate magic numbers! Maybe get it as a precomputed map?
//   Magic numbers may be replaceable by Sellmeier Equations!
float refractiveIndexAtSeaLevel(uint wavelength) {
  float wavelengthEN2 = pow(float(wavelength) * 1e-3, -2.0);

  float sum = 0.0;
  for(int i = 0; i < sellmeierCoefficients.numTerms; ++i) {
    sum += sellmeierCoefficients.terms[i].x / (sellmeierCoefficients.terms[i].y - wavelengthEN2);
  }

  return 1 + sellmeierCoefficients.a + sum;
}

float refractiveIndexAtAltitude(float altitude, uint wavelength) {
  float refractiveIndexAtSeaLevel = refractiveIndexAtSeaLevel(wavelength);
  float densityAtAlt = densityAtAltitude(altitude);
  float seaLevelDensity = densityAtAltitude(0.0);

  return 1.0 + (refractiveIndexAtSeaLevel - 1.0) * (densityAtAlt / seaLevelDensity);
}

void main() {
  uvec2 gid = gl_GlobalInvocationID.xy;

  float altitude = float(gid.x) * DX;
  if(altitude > planet.atmosphericHeight)
    return;

  uint wavelength = gid.y + MIN_WAVELENGTH;
  if(wavelength > MAX_WAVELENGTH)
    return;

  refractiveIndicesAtAltitudes[gid.x][gid.y] = refractiveIndexAtAltitude(altitude, wavelength);

  if(gid.y == 0);
    densitiesAtAltitudes[gid.x] = densityAtAltitude(altitude);
}
)glsl";

LUTPrecalculator::LUTPrecalculator() {
  mProgram = glCreateProgram();
  uint32_t precalculatorShader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(precalculatorShader, 1, &REFRACTIVE_INDICES_COMPUTE_SHADER, nullptr);
  glCompileShader(precalculatorShader);

  int rvalue;
  glGetShaderiv(precalculatorShader, GL_COMPILE_STATUS, &rvalue);
  if (!rvalue) {
    std::cerr << "Error in compiling the compute shader\n";
    GLchar log[10240];
    GLsizei length;
    glGetShaderInfoLog(precalculatorShader, 10239, &length, log);
    std::cerr << "Compiler log:\n" << log << std::endl;
    exit(40);
  }

  glAttachShader(mProgram, precalculatorShader);
  glLinkProgram(mProgram);

  glGetProgramiv(mProgram, GL_LINK_STATUS, &rvalue);
  if (!rvalue) {
    std::cerr << "Error in linking compute shader program\n";
    GLchar log[10240];
    GLsizei length;
    glGetProgramInfoLog(mProgram, 10239, &length, log);
    std::cerr << "Linker log:\n" << log << std::endl;
    exit(41);
  }

  mUniforms.planet.uAtmosphericHeight = glGetUniformLocation(mProgram, "planet.atmosphericHeight");
  mUniforms.planet.uGravity = glGetUniformLocation(mProgram, "planet.gravitationAcceleration");
  mUniforms.planet.uMolarMass = glGetUniformLocation(mProgram, "planet.molarMass");
  mUniforms.planet.uSeaLevelMolecularNumberDensity =
      glGetUniformLocation(mProgram, "planet.seaLevelMolecularNumber");

  mUniforms.sellmeierCoefficients.uA = glGetUniformLocation(mProgram, "sellmeierCoefficients.a");
  mUniforms.sellmeierCoefficients.uNumTerms =
      glGetUniformLocation(mProgram, "sellmeierCoefficients.numTerms");

  for (int i = 0; i < 8; ++i)
    mUniforms.sellmeierCoefficients.uTerms[i] = glGetUniformLocation(
        mProgram, ("sellmeierCoefficients.terms[" + std::to_string(i) + "]").c_str());
}

std::pair<uint32_t, uint32_t> LUTPrecalculator::createLUT(AtmosphericProperties props) {
  const float DX = 1.0;
  const uint32_t NUM_WAVELENGTHS = 360u;
  auto heightDim = static_cast<uint32_t>(props.height / DX);

  size_t bufferSize = heightDim * NUM_WAVELENGTHS;
  auto data = std::vector<float>(bufferSize);
  auto densities = std::vector<float>(heightDim);

  glUseProgram(mProgram);

  uint32_t ssboRefractiveIndices;
  glGenBuffers(1, &ssboRefractiveIndices);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboRefractiveIndices);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * bufferSize, nullptr, GL_STATIC_READ);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboRefractiveIndices);

  uint32_t ssboDensities;
  glGenBuffers(1, &ssboDensities);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDensities);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * heightDim, nullptr, GL_STATIC_READ);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboDensities);

  glUniform1f(mUniforms.planet.uAtmosphericHeight, props.height);
  glUniform1f(mUniforms.planet.uGravity, props.gravity);
  glUniform1f(mUniforms.planet.uMolarMass, props.molarMass);
  glUniform1f(
      mUniforms.planet.uSeaLevelMolecularNumberDensity, props.seaLevelMolecularNumberDensity);

  glUniform1f(mUniforms.sellmeierCoefficients.uA, 8.06051 * 1e-5);

  std::array<glm::vec2, 2> coefficients = {
      glm::vec2{2.480990e-2f, 132.274f}, glm::vec2{1.74557e-4f, 39.32957f}};
  glUniform1ui(mUniforms.sellmeierCoefficients.uNumTerms, coefficients.size());

  for (int i = 0; i < coefficients.size(); ++i) {
    glUniform2f(mUniforms.sellmeierCoefficients.uTerms[i], coefficients[i].x, coefficients[i].y);
  }

  const uint32_t numThreadsX = 32;
  const uint32_t numThreadsY = 32;
  glDispatchComputeGroupSizeARB(heightDim / numThreadsX + 1, NUM_WAVELENGTHS / numThreadsY + 1, 1,
                                numThreadsX, numThreadsY, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboRefractiveIndices);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(float) * bufferSize, data.data());

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDensities);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(float) * heightDim, densities.data());

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  /*for (int h = 41200; h <= heightDim; h += 1) {
    std::cout << "Height: " << h * DX << std::endl;
    std::cout << "Density: " << densities[h] << std::endl;

    for (int w = 0; w < NUM_WAVELENGTHS; w += 10) {
      int idx = h * NUM_WAVELENGTHS + w;
      printf("%.8f, ", data[idx]);
    }
    std::cout << std::endl;
  }*/

  return {ssboRefractiveIndices, ssboDensities};
}