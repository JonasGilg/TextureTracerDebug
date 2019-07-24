////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR //
//      and may be used under the terms of the MIT license. See the LICENSE file
//      for details.     //
//                        Copyright: (c) 2019 German Aerospace Center (DLR) //
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "AtmosphereEclipsePhotonMapper.hpp"
#include <GL/glew.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

void GLAPIENTRY MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                const GLchar *message, const void *userParam) {
    if (type == GL_DEBUG_TYPE_ERROR)
        fprintf(stderr, "GL ERROR: type = 0x%x, severity = 0x%x, message = %s\n", type, severity, message);
    else
        fprintf(stdout, "GL INFO: type = 0x%x, severity = 0x%x, message = %s\n", type, severity, message);
}

namespace gpu {
    const double TEX_HEIGHT_TO_RADIUS_FACTOR = 4;
    const double TEX_SHADOW_LENGTH_FACTOR = 8;

    const uint32_t TEX_WIDTH = 1024u;
    const uint32_t TEX_HEIGHT = TEX_WIDTH;

    const double RADIUS = 6'371'000.0;
    const double RADIUS_FACTORED = RADIUS * TEX_HEIGHT_TO_RADIUS_FACTOR;

    const double SUN_RADIUS = 695'510'000.0;
    const double DIST_TO_SUN = 149'600'000'000.0;
    const double ATMO_HEIGHT = 42'000.0;

    std::string loadShader(const std::string &fileName) {
        std::ifstream shaderFileStream(fileName, std::ios::in);
        if (!shaderFileStream.is_open()) {
            std::cerr << "Could not load the GLSL shader from '" << fileName << "'!" << std::endl;
            exit(-1);
        }

        std::string shaderCode;
        while (!shaderFileStream.eof()) {
            std::string line;
            std::getline(shaderFileStream, line);
            shaderCode.append(line + "\n");
        }

        return shaderCode;
    }

    void AtmosphereEclipsePhotonMapper::initTextureTracer() {
        mTextureTracerProgram = glCreateProgram();
        uint32_t rayTracingComputeShader = glCreateShader(GL_COMPUTE_SHADER);

        std::string code = loadShader("../resources/EclipseTextureTracer.glsl");
        const char *shader = code.c_str();
        glShaderSource(rayTracingComputeShader, 1, &shader, nullptr);
        glCompileShader(rayTracingComputeShader);

        glAttachShader(mTextureTracerProgram, rayTracingComputeShader);
        glLinkProgram(mTextureTracerProgram);

        mTextureTracerUniforms.uRectangleHeight = glGetUniformLocation(mTextureTracerProgram, "rectangleHeight");
        mTextureTracerUniforms.uShadowHeight = glGetUniformLocation(mTextureTracerProgram, "shadowHeight");
        mTextureTracerUniforms.uShadowLength = glGetUniformLocation(mTextureTracerProgram, "shadowLength");

        glDetachShader(mTextureTracerProgram, rayTracingComputeShader);
        glDeleteShader(rayTracingComputeShader);
    }

    AtmosphereEclipsePhotonMapper::AtmosphereEclipsePhotonMapper()
            : mRNG(1L), mDistributionSun(std::uniform_real_distribution<>(-SUN_RADIUS, SUN_RADIUS)),
              mDistributionWavelength(std::uniform_int_distribution<uint32_t>(380, 739)),
              mDistributionBoolean(std::bernoulli_distribution(0.5)) {
        // During init, enable debug output
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(MessageCallback, nullptr);

        initTextureTracer();
    }

    double raySphereDistance(glm::dvec2 origin, glm::dvec2 direction, glm::dvec2 center, double radius) {
        glm::dvec2 m = origin - center;
        double b = glm::dot(m, direction);
        double c = glm::dot(m, m) - (radius * radius);
        if (c > 0.0 && b > 0.0)
            return -1.0;

        double discr = b * b - c;

        // A negative discriminant corresponds to ray missing sphere
        if (discr < 0.0)
            return -1.0;

        // Ray now found to intersect sphere, compute smallest t value of intersection
        return glm::max(0.0, -b - glm::sqrt(discr));
    }

    Photon AtmosphereEclipsePhotonMapper::emitPhoton() {
        std::uniform_real_distribution<> distributionEarth(0.0, ATMO_HEIGHT);
        glm::dvec2 target = {0.0, RADIUS + distributionEarth(mRNG)};

        double d;
        do {
            d = glm::length(glm::dvec2(mDistributionSun(mRNG), mDistributionSun(mRNG)));
        } while (d > SUN_RADIUS);

        glm::dvec2 startPosition = glm::dvec2(-DIST_TO_SUN, mDistributionBoolean(mRNG) ? d : -d);
        glm::dvec2 direction = glm::normalize(target - startPosition);

        startPosition += direction * raySphereDistance(startPosition, direction, {0.0, 0.0},
                                                       RADIUS + ATMO_HEIGHT);

        return {glm::vec2(0.0, startPosition.y), glm::vec2(direction),
                mDistributionWavelength(mRNG), 1.0f};
    }

    std::vector<Photon> AtmosphereEclipsePhotonMapper::generatePhotons(uint32_t count) {
        std::vector<Photon> photons(count);
        std::generate(photons.begin(), photons.end(), [this]() {
            return emitPhoton();
        });
        return photons;
    }


    void AtmosphereEclipsePhotonMapper::traceThroughTexture(uint32_t ssboPhotons, size_t numPhotons) {
        glUseProgram(mTextureTracerProgram);

        glUniform1f(mTextureTracerUniforms.uRectangleHeight, RADIUS_FACTORED / TEX_HEIGHT);

        const double shadowLength = TEX_SHADOW_LENGTH_FACTOR * (DIST_TO_SUN * RADIUS) / (SUN_RADIUS - RADIUS);

        glUniform1f(mTextureTracerUniforms.uShadowLength, shadowLength);
        glUniform1f(mTextureTracerUniforms.uShadowHeight, RADIUS_FACTORED);

        const double xAxisScalingFactor = glm::log(shadowLength) / glm::log(static_cast<double>(TEX_WIDTH));

        // Each element of the vector contains information about two rectangles. x and
        // y are the x position and width for the first rectangle and z and w are the
        // x position and the width for the second rectangle. This is done to save
        // memory, since uniform buffers require a padding for to vec4. So we can fit
        // two rectangles in one element.
        std::vector<glm::vec4> horizontalRectangles = std::vector<glm::vec4>(TEX_WIDTH / 2);

        double xx0 = 0.0;
        for (int x = 0; x < TEX_WIDTH; ++x) {
            const double xx1 = glm::pow(static_cast<double>(x + 1), xAxisScalingFactor);

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
        glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::vec4) * horizontalRectangles.size(), horizontalRectangles.data(),
                     GL_STATIC_READ);

        const uint32_t MIN_WAVELENGTH = 380u;
        const uint32_t MAX_WAVELENGTH = 740u;
        const uint32_t NUM_WAVELENGTHS = MAX_WAVELENGTH - MIN_WAVELENGTH;

        size_t pixelBufferSize = TEX_WIDTH * TEX_HEIGHT * NUM_WAVELENGTHS * sizeof(uint32_t);
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
        const size_t debugSize         = DEBUG_BUFFER_SIZE * (sizeof(float) + 2 *
        sizeof(float) + sizeof(uint32_t) + 2 * sizeof(uint32_t)); uint32_t ssboDebug;
        glGenBuffers(1, &ssboDebug);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDebug);
        glBufferData(GL_SHADER_STORAGE_BUFFER, debugSize, nullptr, GL_DYNAMIC_COPY);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, ssboDebug);
      */

        // ############################################################################################ \\

        // TODO make configurable
        const uint32_t numThreads = 32u;
        const uint32_t numBlocks = numPhotons / numThreads;
        std::cout << "numBlocks: " << numBlocks << std::endl;

        glDispatchComputeGroupSizeARB(numBlocks, 1, 1, numThreads, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        struct Pixel {
            uint32_t intensityAtWavelengths[NUM_WAVELENGTHS];
        };

        std::vector<Pixel> pixels = std::vector<Pixel>(TEX_WIDTH * TEX_HEIGHT);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPixels);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, pixelBufferSize, pixels.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        for (int y = 0; y < TEX_HEIGHT; ++y) {
            printf("%4i | ", y);
            for (int x = 0; x < TEX_WIDTH; ++x) {
                Pixel p = pixels[y * TEX_WIDTH + x];
                int counter = 0;
                for (uint32_t i : p.intensityAtWavelengths) {
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

            std::cout << std::endl;
        }

        // ############################################################################################ \\

        /*
          glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDebug);

          std::array<float, DEBUG_BUFFER_SIZE> floats{};
          glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(floats),
          floats.data());

          std::array<glm::vec2, DEBUG_BUFFER_SIZE> vec2s{};
          glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(floats), sizeof(vec2s),
          vec2s.data());

          std::array<uint32_t, DEBUG_BUFFER_SIZE> uints{};
          glGetBufferSubData(
              GL_SHADER_STORAGE_BUFFER, sizeof(floats) + sizeof(vec2s), sizeof(uints),
          uints.data());

          std::array<glm::uvec2, DEBUG_BUFFER_SIZE> uvec2s{};
          glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(floats) + sizeof(vec2s)
          + sizeof(uints), sizeof(uvec2s), uvec2s.data());

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

    uint32_t AtmosphereEclipsePhotonMapper::createShadowMap() {
        std::vector<Photon> photons = generatePhotons(100'000);

        uint32_t ssboPhotons;
        glGenBuffers(1, &ssboPhotons);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPhotons);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Photon) * photons.size(), photons.data(), GL_DYNAMIC_COPY);

        traceThroughTexture(ssboPhotons, photons.size());

        glDeleteBuffers(1, &ssboPhotons);
        glDeleteProgram(mTextureTracerProgram);

        glDisable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(nullptr, nullptr);

        return 0;
    }
}