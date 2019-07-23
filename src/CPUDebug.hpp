#ifndef COMPUTESHADERDEBUG_CPUDEBUG_H
#define COMPUTESHADERDEBUG_CPUDEBUG_H

#include <glm/glm.hpp>
#include <random>
#include "LUTPrecalculator.hpp"
#include "CpuPhotonMapper.hpp"

namespace cpu {

    struct PlanetWithAtmosphere {
        double radius;                         // m
        double atmosphericHeight;              // m
        double seaLevelMolecularNumberDensity; // cm^−3
    };
/*
// 6 * 4 = 24 Bytes
    struct Photon {
        glm::vec2 position;   // m
        glm::vec2 direction;  // normalized
        uint32_t waveLength; // nm
        float intensity;  // 0..1 should start at 1
    };*/

    class AtmosphereEclipsePhotonMapper {
    public:
        AtmosphereEclipsePhotonMapper();

        uint32_t createShadowMap(PlanetWithAtmosphere planet);

    private:
        Photon emitPhoton(double distToSun, double planetRadius, double atmosphereHeight);

        std::vector <Photon> generatePhotons(uint32_t count);

        struct Uniforms {
            uint32_t uPlanetRadius;
            uint32_t uPlanetAtmosphericHeight;
            uint32_t uPlanetSeaLevelMolecularNumberDensity;

            uint32_t uRectangleHeight;
        } mUniforms;

        uint32_t mProgram;
        uint32_t mPhotonMappingComputeShader;

        const double SUN_RADIUS = 695'510'000.0;

        std::mt19937_64 mRNG;
        std::uniform_real_distribution<> mDistributionSun;
        std::uniform_int_distribution <uint32_t> mDistributionWavelength;
        std::bernoulli_distribution mDistributionBoolean;

        LUTPrecalculator mLutPrecalculator;
    };
}

#endif //COMPUTESHADERDEBUG_CPUDEBUG_H
