////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR //
//      and may be used under the terms of the MIT license. See the LICENSE file
//      for details.     //
//                        Copyright: (c) 2019 German Aerospace Center (DLR) //
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "AtmosphereEclipsePhotonMapper.hpp"
#include "CpuPhotonMapper.hpp"
#include <GL/glew.h>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

void GLAPIENTRY MessageCallback(GLenum source, GLenum type, GLuint id,
                                GLenum severity, GLsizei length,
                                const GLchar *message, const void *userParam) {
  if (type == GL_DEBUG_TYPE_ERROR)
    fprintf(stderr, "GL ERROR: type = 0x%x, severity = 0x%x, message = %s\n",
            type, severity, message);
}

namespace gpu {

std::string loadShader(const std::string& fileName) {
  std::ifstream shaderFileStream(fileName, std::ios::in);
  if (!shaderFileStream.is_open()) {
    std::cerr << "Could not load the GLSL shader from '" << fileName << "'!" << std::endl;
    exit(-1);
  }

  std::string shaderCode;
  std::string line;
  while (!shaderFileStream.eof()) {
    std::getline(shaderFileStream, line);
    shaderCode.append(line + "\n");
  }

  return shaderCode;
}

void AtmosphereEclipsePhotonMapper::initAtmosphereTracer() {
  mAtmosphereTracerProgram         = glCreateProgram();
  uint32_t rayTracingComputeShader = glCreateShader(GL_COMPUTE_SHADER);

  std::string code   = loadShader("resources/EclipsePhotonTracer.glsl");
  const char* shader = code.c_str();
  glShaderSource(rayTracingComputeShader, 1, &shader, nullptr);
  glCompileShader(rayTracingComputeShader);

  int rvalue;
  glGetShaderiv(rayTracingComputeShader, GL_COMPILE_STATUS, &rvalue);
  if (!rvalue) {
    std::cerr << "Error in compiling the compute shader\n";
    GLchar  log[10240];
    GLsizei length;
    glGetShaderInfoLog(rayTracingComputeShader, 10239, &length, log);
    std::cerr << "Compiler log:\n" << log << std::endl;
  }

  glAttachShader(mAtmosphereTracerProgram, rayTracingComputeShader);
  glLinkProgram(mAtmosphereTracerProgram);

  glValidateProgram(mAtmosphereTracerProgram);

  glGetProgramiv(mAtmosphereTracerProgram, GL_LINK_STATUS, &rvalue);
  if (!rvalue) {
    std::cerr << "Error in linking compute shader program\n";
    GLchar  log[10240];
    GLsizei length;
    glGetProgramInfoLog(mAtmosphereTracerProgram, 10239, &length, log);
    std::cerr << "Linker log:\n" << log << std::endl;
  }

  mAtmosphereTracerUniforms.uPlanetRadius =
      glGetUniformLocation(mAtmosphereTracerProgram, "planet.radius");
  mAtmosphereTracerUniforms.uPlanetAtmosphericHeight =
      glGetUniformLocation(mAtmosphereTracerProgram, "planet.atmosphericHeight");
  mAtmosphereTracerUniforms.uPlanetSeaLevelMolecularNumberDensity =
      glGetUniformLocation(mAtmosphereTracerProgram, "planet.seaLevelMolecularNumberDensity");

  glDetachShader(mAtmosphereTracerProgram, rayTracingComputeShader);
  glDeleteShader(rayTracingComputeShader);
}

void AtmosphereEclipsePhotonMapper::initTextureTracer() {
  mTextureTracerProgram            = glCreateProgram();
  uint32_t rayTracingComputeShader = glCreateShader(GL_COMPUTE_SHADER);

  std::string code   = loadShader("./resources/EclipseTextureTracer.glsl");
  const char* shader = code.c_str();
  glShaderSource(rayTracingComputeShader, 1, &shader, nullptr);
  glCompileShader(rayTracingComputeShader);

  int rvalue;
  glGetShaderiv(rayTracingComputeShader, GL_COMPILE_STATUS, &rvalue);
  if (!rvalue) {
    std::cerr << "Error in compiling the compute shader\n";
    GLchar  log[10240];
    GLsizei length;
    glGetShaderInfoLog(rayTracingComputeShader, 10239, &length, log);
    std::cerr << "Compiler log:\n" << log << std::endl;
  }

  glAttachShader(mTextureTracerProgram, rayTracingComputeShader);
  glLinkProgram(mTextureTracerProgram);

  glValidateProgram(mTextureTracerProgram);

  glGetProgramiv(mTextureTracerProgram, GL_LINK_STATUS, &rvalue);
  if (!rvalue) {
    std::cerr << "Error in linking compute shader program\n";
    GLchar  log[10240];
    GLsizei length;
    glGetProgramInfoLog(mTextureTracerProgram, 10239, &length, log);
    std::cerr << "Linker log:\n" << log << std::endl;
  }

  mTextureTracerUniforms.uRectangleHeight =
      glGetUniformLocation(mTextureTracerProgram, "rectangleHeight");
  mTextureTracerUniforms.uShadowHeight =
      glGetUniformLocation(mTextureTracerProgram, "shadowHeight");
  mTextureTracerUniforms.uShadowLength =
      glGetUniformLocation(mTextureTracerProgram, "shadowLength");

  mTextureTracerUniforms.uPass     = glGetUniformLocation(mTextureTracerProgram, "pass");
  mTextureTracerUniforms.uPassSize = glGetUniformLocation(mTextureTracerProgram, "passSize");

  glDetachShader(mTextureTracerProgram, rayTracingComputeShader);
  glDeleteShader(rayTracingComputeShader);
}

AtmosphereEclipsePhotonMapper::AtmosphereEclipsePhotonMapper()
    : mLutPrecalculator()
    , mRNG(/*std::random_device()()*/ 1L)
    , mDistributionSun(std::uniform_real_distribution<>(-SUN_RADIUS, SUN_RADIUS))
    , mDistributionWavelength(std::uniform_int_distribution<uint32_t>(380, 739))
    , mDistributionBoolean(std::bernoulli_distribution(0.5)) {
  // During init, enable debug output
  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(MessageCallback, nullptr);

  initAtmosphereTracer();
  initTextureTracer();
}

double raySphereDistance(
    glm::dvec2 origin, glm::dvec2 direction, glm::dvec2 center, double radius) {
  glm::dvec2 m = origin - center;
  double     b = glm::dot(m, direction);
  double     c = glm::dot(m, m) - (radius * radius);
  if (c > 0.0 && b > 0.0)
    return -1.0;

  double discr = b * b - c;

  // A negative discriminant corresponds to ray missing sphere
  if (discr < 0.0)
    return -1.0;

  // Ray now found to intersect sphere, compute smallest t value of intersection
  return glm::max(0.0, -b - glm::sqrt(discr));
}

Photon AtmosphereEclipsePhotonMapper::emitPhoton(
    double distToSun, double planetRadius, double atmosphereHeight) {
  std::uniform_real_distribution<> distributionEarth(0.0, atmosphereHeight);
  glm::dvec2                       target = {0.0, planetRadius + distributionEarth(mRNG)};

  double x;
  do {
    x = glm::length(glm::dvec2(mDistributionSun(mRNG), mDistributionSun(mRNG)));
  } while (x > SUN_RADIUS);

  glm::dvec2 startPosition = glm::dvec2(-distToSun, mDistributionBoolean(mRNG) ? x : -x);
  glm::dvec2 direction     = glm::normalize(target - startPosition);

  startPosition += direction * raySphereDistance(startPosition, direction, {0.0, 0.0},
                                                 planetRadius + atmosphereHeight);

  return {glm::vec2(startPosition), glm::vec2(direction), mDistributionWavelength(mRNG), 1.0f};
}

std::vector<Photon> AtmosphereEclipsePhotonMapper::generatePhotons(uint32_t count) {
  std::vector<Photon> photons(count);
  std::generate(photons.begin(), photons.end(),
                [this]() { return emitPhoton(149'600'000'000.0, 6'371'000.0, 42'000.0); });
  return photons;
}

const double TEX_HEIGHT_TO_RADIUS_FACTOR = 4;
const double TEX_SHADOW_LENGTH_FACTOR    = 8;

// TODO make configurable
const uint32_t TEX_WIDTH  = 1024u;
const uint32_t TEX_HEIGHT = TEX_WIDTH;

void AtmosphereEclipsePhotonMapper::traceThroughAtmosphere(
    uint32_t ssboPhotons, size_t numPhotons, PlanetWithAtmosphere const& planet) {
  auto [ssboRefractiveIndices, ssboDensities] =
  mLutPrecalculator.createLUT({(float)planet.atmosphericHeight, 9.81f, 0.0289644f,
                               (float)planet.seaLevelMolecularNumberDensity});

  glUseProgram(mAtmosphereTracerProgram);

  glUniform1f(mAtmosphereTracerUniforms.uPlanetAtmosphericHeight, planet.atmosphericHeight);
  glUniform1f(mAtmosphereTracerUniforms.uPlanetSeaLevelMolecularNumberDensity,
              planet.seaLevelMolecularNumberDensity);
  glUniform1f(mAtmosphereTracerUniforms.uPlanetRadius, planet.radius);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboPhotons);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboRefractiveIndices);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboDensities);

  std::cout << "Starting to trace photons..." << std::endl;
  std::clock_t begin = std::clock();

  // TODO make configurable
  const uint32_t numThreads = 512u;
  const uint32_t numBlocks  = (numPhotons / numThreads) + 1;
  std::cout << "numBlocks: " << numBlocks << std::endl;

  glDispatchComputeGroupSizeARB(numBlocks, 1, 1, numThreads, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  std::clock_t end         = std::clock();
  double       elapsedTime = double(end - begin) / CLOCKS_PER_SEC;
  std::cout << "Finished tracing photons." << std::endl;
  std::cout << "It took: " << elapsedTime << " seconds.\n" << std::endl;

  glDeleteBuffers(1, &ssboDensities);
  glDeleteBuffers(1, &ssboRefractiveIndices);

  glUseProgram(0);
}

void AtmosphereEclipsePhotonMapper::traceThroughTexture(
    uint32_t ssboPhotons, size_t numPhotons, PlanetWithAtmosphere const& planet) {
  glUseProgram(mTextureTracerProgram);

  glUniform1f(mTextureTracerUniforms.uRectangleHeight,
              (planet.radius * TEX_HEIGHT_TO_RADIUS_FACTOR) / TEX_HEIGHT);

  // TODO dist to sun
  const double shadowLength =
      TEX_SHADOW_LENGTH_FACTOR * (149'600'000'000.0 * planet.radius) / (SUN_RADIUS - planet.radius);

  glUniform1f(mTextureTracerUniforms.uShadowLength, shadowLength);
  glUniform1f(mTextureTracerUniforms.uShadowHeight, planet.radius * TEX_HEIGHT_TO_RADIUS_FACTOR);

  std::cout << "shadowLength: " << shadowLength / 1000.0 << std::endl;

  const double xAxisScalingFactor =
      glm::log(shadowLength) / glm::log(static_cast<double>(TEX_WIDTH));

  std::cout << "xAxisScalingFactor: " << xAxisScalingFactor << std::endl;

  // Each element of the vector contains information about two rectangles. x and y are the x
  // position and width for the first rectangle and z and w are the x position and the width for
  // the second rectangle. This is done to save memory, since uniform buffers require a padding
  // for to vec4. So we can fit two rectangles in one element.
  std::vector<glm::vec4> horizontalRectangles = std::vector<glm::vec4>(TEX_WIDTH / 2);

  double xx0 = 0.0;
  for (int x = 0; x < TEX_WIDTH; ++x) {
    const double xx1 = glm::pow(static_cast<double>(x), xAxisScalingFactor);

    if (x % 2 == 0) {
      horizontalRectangles[x / 2].x = xx0;
      horizontalRectangles[x / 2].y = xx1 - xx0;
    } else {
      horizontalRectangles[x / 2].z = xx0;
      horizontalRectangles[x / 2].w = xx1 - xx0;
    }

    xx0 = xx1;
  }

  uint32_t uboRectangles;
  glGenBuffers(1, &uboRectangles);
  glBindBuffer(GL_UNIFORM_BUFFER, uboRectangles);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::vec4) * horizontalRectangles.size(),
               horizontalRectangles.data(), GL_STATIC_READ);

  // TODO make configurable
  const uint32_t MIN_WAVELENGTH  = 380u;
  const uint32_t MAX_WAVELENGTH  = 740u;
  const uint32_t NUM_WAVELENGTHS = MAX_WAVELENGTH - MIN_WAVELENGTH;

  size_t   pixelBufferSize = TEX_WIDTH * TEX_HEIGHT * NUM_WAVELENGTHS * sizeof(uint32_t);
  uint32_t ssboPixels;
  glGenBuffers(1, &ssboPixels);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPixels);
  glBufferData(GL_SHADER_STORAGE_BUFFER, pixelBufferSize, nullptr, GL_DYNAMIC_COPY);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboPhotons);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboPixels);
  glBindBufferBase(GL_UNIFORM_BUFFER, 2, uboRectangles);

  // ############################################################################################ \\

  /*
  const size_t DEBUG_BUFFER_SIZE = 16;
  const size_t debugSize         = DEBUG_BUFFER_SIZE * (sizeof(float) + 2 * sizeof(float) +
                                                        sizeof(uint32_t) + 2 * sizeof(uint32_t));
  uint32_t     ssboDebug;
  glGenBuffers(1, &ssboDebug);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDebug);
  glBufferData(GL_SHADER_STORAGE_BUFFER, debugSize, nullptr, GL_DYNAMIC_COPY);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, ssboDebug);
*/

  // ############################################################################################ \\

  std::cout << "Starting to trace photons..." << std::endl;
  std::clock_t begin = std::clock();

  // TODO make configurable
  const uint32_t passSize   = 1024u;
  const uint32_t maxPasses  = (numPhotons / passSize) + 1;
  const uint32_t numThreads = 32u;
  const uint32_t numBlocks  = passSize / numThreads;
  std::cout << "numBlocks: " << numBlocks << std::endl;

  // Because OpenGL decides it doesn't like to compute all at once, we have to split them up
  // (╯°□°）╯︵ ┻━┻
  glUniform1ui(mTextureTracerUniforms.uPassSize, passSize);
  for (uint32_t pass = 0u; pass < maxPasses; ++pass) {
    glUniform1ui(mTextureTracerUniforms.uPass, pass);

    glDispatchComputeGroupSizeARB(numBlocks, 1, 1, numThreads, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  }

  std::clock_t end         = std::clock();
  double       elapsedTime = double(end - begin) / CLOCKS_PER_SEC;
  std::cout << "Finished tracing photons." << std::endl;
  std::cout << "It took: " << elapsedTime << " seconds.\n" << std::endl;

  std::clock_t beginDownload = std::clock();

  struct Pixel {
    uint32_t intensityAtWavelengths[NUM_WAVELENGTHS];
  };

  std::vector<Pixel> pixels = std::vector<Pixel>(TEX_WIDTH * TEX_HEIGHT);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPixels);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, pixelBufferSize, pixels.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  std::clock_t endDownload         = std::clock();
  double       elapsedTimeDownload = double(endDownload - beginDownload) / CLOCKS_PER_SEC;
  std::cout << "Downloaded photons." << std::endl;
  std::cout << "It took: " << elapsedTimeDownload << " seconds.\n" << std::endl;

  int skip = 1;
  for (int y = 0; y < TEX_HEIGHT; ++y) {
    printf("%4i | ", y);

    for (int x = 0; x < TEX_WIDTH; ++x) {
      if (x % skip == 0 && y % skip == 0) {
        Pixel p       = pixels[y * TEX_WIDTH + x];
        int   counter = 0;
        for (int i : p.intensityAtWavelengths) {
          counter += i;
        }

        if (counter == 0) {
          printf("  ");
        } else if (counter > 100'000'000) {
          printf("%4s", "\u25A0");
        } else if (counter > 10'000'000) {
          printf("%4s", "\u25A3");
        } else if (counter > 1'000'000) {
          printf("%4s", "\u25A6");
        } else if (counter > 100'000) {
          printf("%4s", "\u25A4");
        } else {
          printf("%4s", "\u25A1");
        }
      }
    }

    if (y % skip == 0)
      std::cout << std::endl;
  }

  // ############################################################################################ \\

  /*
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDebug);

    std::array<float, DEBUG_BUFFER_SIZE> floats{};
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(floats), floats.data());

    std::array<glm::vec2, DEBUG_BUFFER_SIZE> vec2s{};
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(floats), sizeof(vec2s), vec2s.data());

    std::array<uint32_t, DEBUG_BUFFER_SIZE> uints{};
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER, sizeof(floats) + sizeof(vec2s), sizeof(uints), uints.data());

    std::array<glm::uvec2, DEBUG_BUFFER_SIZE> uvec2s{};
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(floats) + sizeof(vec2s) + sizeof(uints),
                       sizeof(uvec2s), uvec2s.data());

    for (float f : floats) {
      printf("%.5f, ", f);
    }
    std::cout << std::endl;

    for (glm::vec2 v : vec2s) {
      printf("(%.5f, %.5f), ", v.x, v.y);
    }
    std::cout << std::endl;

    for (uint32_t u : uints) {
      std::cout << u << ", ";
    }
    std::cout << std::endl;

    for (glm::uvec2 v : uvec2s) {
      printf("(%u, %u), ", v.x, v.y);
    }
    std::cout << std::endl;
  */

  // ############################################################################################ \\

  glDeleteBuffers(1, &uboRectangles);
  glDeleteBuffers(1, &ssboPixels);

  glUseProgram(0);
}

uint32_t AtmosphereEclipsePhotonMapper::createShadowMap(PlanetWithAtmosphere const& planet) {
  // TODO make configurable
  std::cout << "Starting to generate photons..." << std::endl;
  std::clock_t        start   = std::clock();
  std::vector<Photon> photons = generatePhotons(10'000'000);
  std::clock_t        stop    = std::clock();
  double              time    = double(stop - start) / CLOCKS_PER_SEC;

  std::cout << "Finished generating photons." << std::endl;
  std::cout << "It took: " << time << " seconds.\n" << std::endl;

  auto beginUpload = std::clock();

  uint32_t ssboPhotons;
  glGenBuffers(1, &ssboPhotons);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPhotons);
  glBufferData(
      GL_SHADER_STORAGE_BUFFER, sizeof(Photon) * photons.size(), photons.data(), GL_DYNAMIC_COPY);

  auto   endUpload         = std::clock();
  double elapsedTimeUpload = double(endUpload - beginUpload) / CLOCKS_PER_SEC;
  std::cout << "Finished uploading photons." << std::endl;
  std::cout << "It took: " << elapsedTimeUpload << " seconds.\n" << std::endl;

  traceThroughAtmosphere(ssboPhotons, photons.size(), planet);
  traceThroughTexture(ssboPhotons, photons.size(), planet);

  /*const size_t DEBUG_BUFFER_SIZE = 16;
  const size_t debugSize         = DEBUG_BUFFER_SIZE * (sizeof(float) + 2 * sizeof(float) +
                                                   sizeof(uint32_t) + 2 * sizeof(uint32_t));
  uint32_t     ssboDebug;
  glGenBuffers(1, &ssboDebug);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDebug);
  glBufferData(GL_SHADER_STORAGE_BUFFER, debugSize, nullptr, GL_DYNAMIC_COPY);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, ssboDebug);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDebug);

  std::array<float, DEBUG_BUFFER_SIZE> floats{};
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(floats), floats.data());

  std::array<glm::vec2, DEBUG_BUFFER_SIZE> vec2s{};
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(floats), sizeof(vec2s), vec2s.data());

  std::array<uint32_t, DEBUG_BUFFER_SIZE> uints{};
  glGetBufferSubData(
      GL_SHADER_STORAGE_BUFFER, sizeof(floats) + sizeof(vec2s), sizeof(uints), uints.data());

  std::array<glm::uvec2, DEBUG_BUFFER_SIZE> uvec2s{};
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(floats) + sizeof(vec2s) + sizeof(uints),
      sizeof(uvec2s), uvec2s.data());

  for (float f : floats) {
    printf("%.5f, ", f);
  }
  std::cout << std::endl;

  for (glm::vec2 v : vec2s) {
    printf("(%.5f, %.5f), ", v.x, v.y);
  }
  std::cout << std::endl;

  for (uint32_t u : uints) {
    std::cout << u << ", ";
  }
  std::cout << std::endl;

  for (glm::uvec2 v : uvec2s) {
    printf("(%u, %u), ", v.x, v.y);
  }
  std::cout << std::endl;*/

  /*glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  glDeleteBuffers(1, &ssboDebug);

  GLint rectangleBufferSize = 0;
  glBindBuffer(GL_UNIFORM_BUFFER, uboRectangles);
  glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &rectangleBufferSize);
  std::cout << "Rectangle size: " << rectangleBufferSize << " bytes." << std::endl << std::endl;

  GLint densityBufferSize = 0;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDensities);
  glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &densityBufferSize);
  std::cout << "Densities size: " << densityBufferSize << " bytes." << std::endl;

  GLint reftIndexBufferSize = 0;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboRefractiveIndices);
  glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &reftIndexBufferSize);
  std::cout << "  Indexes size: " << reftIndexBufferSize << " bytes." << std::endl;

  GLint photonBufferSize = 0;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPhotons);
  glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &photonBufferSize);
  std::cout << "  Photons size: " << photonBufferSize << " bytes." << std::endl;

  GLint pixelsBufferSize = 0;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPixels);
  glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &pixelsBufferSize);
  std::cout << "    Pixel size: " << pixelsBufferSize << " bytes." << std::endl;
*/

  GLint64 size;
  glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &size);
  std::cout << " SSBO max size: " << size << " bytes." << std::endl;

  glDeleteBuffers(1, &ssboPhotons);

  glDeleteProgram(mAtmosphereTracerProgram);
  glDeleteProgram(mTextureTracerProgram);

  glDisable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(nullptr, nullptr);

  std::cout << "I'm out!" << std::endl;
  exit(0);

  return 0;
}
}