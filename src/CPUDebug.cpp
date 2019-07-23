#include "CPUDebug.hpp"

#include "CpuPhotonMapper.hpp"
#include <GL/glew.h>
#include <algorithm>
#include <ctime>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace cpu {
    void GLAPIENTRY MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                    const GLchar *message, const void *userParam) {
        if (type == GL_DEBUG_TYPE_ERROR)
            fprintf(stderr,
                    "GL ERROR: type = 0x%x, severity = 0x%x, message = %s\n",
                    type, severity, message);
    }

    AtmosphereEclipsePhotonMapper::AtmosphereEclipsePhotonMapper()
            : mLutPrecalculator(), mRNG(/*std::random_device()()*/ 1L),
              mDistributionSun(
                      std::uniform_real_distribution<>(-SUN_RADIUS, SUN_RADIUS)),
              mDistributionWavelength(
                      std::uniform_int_distribution<uint32_t>(380, 739)),
              mDistributionBoolean(std::bernoulli_distribution(0.5)) {
        // During init, enable debug output
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(MessageCallback, nullptr);
    }

    double raySphereDistance(glm::dvec2 origin, glm::dvec2 direction,
                             glm::dvec2 center, double radius) {
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

    Photon AtmosphereEclipsePhotonMapper::emitPhoton(double distToSun,
                                                     double planetRadius,
                                                     double atmosphereHeight) {
        std::uniform_real_distribution<> distributionEarth(0.0, atmosphereHeight);
        glm::dvec2 target = {0.0, planetRadius + distributionEarth(mRNG)};

        double x;
        do {
            x = glm::length(glm::dvec2(mDistributionSun(mRNG), mDistributionSun(mRNG)));
        } while (x > SUN_RADIUS);

        glm::dvec2 startPosition =
                glm::dvec2(-distToSun, mDistributionBoolean(mRNG) ? x : -x);
        glm::dvec2 direction = glm::normalize(target - startPosition);

        startPosition +=
                direction * raySphereDistance(startPosition, direction, {0.0, 0.0},
                                              planetRadius + atmosphereHeight);

        return {glm::vec2(startPosition), glm::vec2(direction),
                mDistributionWavelength(mRNG), 1.0f};
    }

    std::vector<Photon>
    AtmosphereEclipsePhotonMapper::generatePhotons(uint32_t count) {
        std::vector<Photon> photons(count);
        std::generate(photons.begin(), photons.end(), [this]() {
            return emitPhoton(149'600'000'000.0, 6'371'000.0, 42'000.0);
        });
        return photons;
    }

    const double TEX_HEIGHT_TO_RADIUS_FACTOR = 4;
    const double TEX_SHADOW_LENGTH_FACTOR = 8;

    uint32_t AtmosphereEclipsePhotonMapper::createShadowMap(PlanetWithAtmosphere planet) {
        auto[ssboRefractiveIndices, ssboDensities] = mLutPrecalculator.createLUT(
                {(float) planet.atmosphericHeight, 9.81f, 0.0289644f,
                 (float) planet.seaLevelMolecularNumberDensity});

        CPUPhotonMapper cpuPhotonMapper{};

        cpuPhotonMapper.setAtmosphereHeight(planet.atmosphericHeight);
        cpuPhotonMapper.setSeaLevelMolecularNumberDensity(
                planet.seaLevelMolecularNumberDensity);
        cpuPhotonMapper.setPlanetRadius(planet.radius);

        cpuPhotonMapper.setRectangleHeight(
                (planet.radius * TEX_HEIGHT_TO_RADIUS_FACTOR) / TEX_HEIGHT);

        // TODO dist to sun
        const double shadowLength = TEX_SHADOW_LENGTH_FACTOR *
                                    (149'600'000'000.0 * planet.radius) /
                                    (SUN_RADIUS - planet.radius);

        cpuPhotonMapper.setShadowLength(shadowLength);
        cpuPhotonMapper.setShadowHeight(planet.radius * TEX_HEIGHT_TO_RADIUS_FACTOR);

        std::cout << "shadowLength: " << shadowLength / 1000.0 << std::endl;

        const double xAxisScalingFactor =
                glm::log(shadowLength) / glm::log(static_cast<double>(TEX_WIDTH));

        std::cout << "xAxisScalingFactor: " << xAxisScalingFactor << std::endl;

        {
            // Each element of the vector contains information about two rectangles. x
            // and y are the x position and width for the first rectangle and z and w
            // are the x position and the width for the second rectangle. This is done
            // to save memory, since uniform buffers require a padding for to vec4. So
            // we can fit two rectangles in one element.
            std::vector<glm::vec4> horizontalRectangles =
                    std::vector<glm::vec4>(TEX_WIDTH / 2);

            double xx0 = 0.0;
            for (int x = 0; x < TEX_WIDTH; ++x) {
                const double xx1 =
                        glm::pow(static_cast<double>(x), xAxisScalingFactor);

                if (x % 2 == 0) {
                    horizontalRectangles[x / 2].x = xx0;
                    horizontalRectangles[x / 2].y = xx1 - xx0;
                } else {
                    horizontalRectangles[x / 2].z = xx0;
                    horizontalRectangles[x / 2].w = xx1 - xx0;
                }

                xx0 = xx1;
            }

            cpuPhotonMapper.setRectangleData(horizontalRectangles);
        }

        size_t numPhotons = 5;

        {
            // TODO make configurable
            std::cout << "Starting to generate photons..." << std::endl;
            std::clock_t start = std::clock();
            std::vector<Photon> photons = generatePhotons(numPhotons);
            std::clock_t stop = std::clock();
            double time = double(stop - start) / CLOCKS_PER_SEC;
            std::cout << "Finished generating photons." << std::endl;
            std::cout << "It took: " << time << " seconds.\n" << std::endl;

            cpuPhotonMapper.setPhotonData(photons);
        }

        // TODO make configurable
        const uint32_t MIN_WAVELENGTH = 380u;
        const uint32_t MAX_WAVELENGTH = 740u;
        const uint32_t NUM_WAVELENGTHS = MAX_WAVELENGTH - MIN_WAVELENGTH;

        const float DX = 1.0;
        auto heightDim = static_cast<uint32_t>(planet.atmosphericHeight / DX);

        {
            size_t bufferSize = heightDim * NUM_WAVELENGTHS;
            auto data = std::vector<float>(bufferSize);

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboRefractiveIndices);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(float) * bufferSize,
                               data.data());

            std::array<std::array<float, NUM_WAVELENGTHS>, 42000> refractiveIndexData{};

            for (int h = 0; h < heightDim; ++h) {
                for (int i = 0; i < NUM_WAVELENGTHS; ++i) {
                    int idx = h * NUM_WAVELENGTHS + i;
                    refractiveIndexData[h][i] = data[idx];
                }
            }

            cpuPhotonMapper.setRefractiveIndices(refractiveIndexData);
        }

        {
            auto densities = std::vector<float>(heightDim);

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDensities);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(float) * heightDim,
                               densities.data());

            cpuPhotonMapper.setDensityData(densities);
        }

        std::cout << "Starting to trace photons..." << std::endl;
        std::clock_t begin = std::clock();

        //for (int j = 0; j < numPhotons; ++j) {
        cpuPhotonMapper.execute(0);
        //}

        std::clock_t end = std::clock();
        double elapsedTime = double(end - begin) / CLOCKS_PER_SEC;
        std::cout << "Finished tracing photons." << std::endl;
        std::cout << "It took: " << elapsedTime << " seconds.\n" << std::endl;

        {
            auto pixels = cpuPhotonMapper.getPixelBuffer();

            int skip = 1;
            for (int y = 0; y < TEX_HEIGHT; ++y) {
                for (int x = 0; x < TEX_WIDTH; ++x) {
                    if (x % skip == 0 && y % skip == 0) {
                        Pixel p = pixels[y * TEX_WIDTH + x];
                        int counter = 0;
                        for (int i : p.intensityAtWavelengths) {
                            counter += i;
                        }

                        if (counter == 0) {
                            printf("  ");
                        } else if (counter > 10000000) {
                            printf("%4s", "\u25A0");
                        } else if (counter > 1000000) {
                            printf("%4s", "\u25A3");
                        } else if (counter > 100000) {
                            printf("%4s", "\u25A6");
                        } else if (counter > 10000) {
                            printf("%4s", "\u25A4");
                        } else {
                            printf("%4s", "\u25A1");
                        }
                    }
                }

                if (y % skip == 0)
                    std::cout << std::endl;
            }
        }

        std::cout << "I'm out!" << std::endl;

        return 0;
    }
}