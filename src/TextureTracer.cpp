#include "TextureTracer.hpp"
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

    void TextureTracer::initTextureTracer() {
        mTextureTracerProgram = glCreateProgram();
        uint32_t rayTracingComputeShader = glCreateShader(GL_COMPUTE_SHADER);

        std::string code = loadShader("../resources/TextureTracer.glsl");
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

    TextureTracer::TextureTracer()
            : mRNG(1L), mDistributionSun(std::uniform_real_distribution<>(-SUN_RADIUS, SUN_RADIUS)),
              mDistributionWavelength(std::uniform_int_distribution<uint32_t>(380, 739)),
              mDistributionBoolean(std::bernoulli_distribution(0.5)) {
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

    Photon TextureTracer::emitPhoton() {
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

    std::vector<Photon> TextureTracer::generatePhotons(uint32_t count) {
        std::vector<Photon> photons(count);
        std::generate(photons.begin(), photons.end(), [this]() {
            return emitPhoton();
        });
        return photons;
    }


    void TextureTracer::traceThroughTexture(uint32_t ssboPhotons, size_t numPhotons) {
        glUseProgram(mTextureTracerProgram);

        glUniform1f(mTextureTracerUniforms.uRectangleHeight, RADIUS_FACTORED / TEX_HEIGHT);

        const double shadowLength = TEX_SHADOW_LENGTH_FACTOR * (DIST_TO_SUN * RADIUS) / (SUN_RADIUS - RADIUS);

        glUniform1f(mTextureTracerUniforms.uShadowLength, shadowLength);
        glUniform1f(mTextureTracerUniforms.uShadowHeight, RADIUS_FACTORED);

        const double xAxisScalingFactor = glm::log(shadowLength) / glm::log(static_cast<double>(TEX_WIDTH));

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

        const uint32_t numThreads = 32u;
        const uint32_t numBlocks = numPhotons / numThreads;
        std::cout << "numBlocks: " << numBlocks << std::endl;

        glDispatchComputeGroupSizeARB(numBlocks, 1, 1, numThreads, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        struct Pixel {
            uint32_t intensityAtWavelengths[NUM_WAVELENGTHS];
        };

        std::vector<Pixel> pixels(TEX_WIDTH * TEX_HEIGHT);

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

        glDeleteBuffers(1, &uboRectangles);
        glDeleteBuffers(1, &ssboPixels);

        glUseProgram(0);
    }

    uint32_t TextureTracer::createShadowMap(size_t numPhotons) {
        std::vector<Photon> photons = generatePhotons(numPhotons);

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