////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR //
//      and may be used under the terms of the MIT license. See the LICENSE file
//      for details.     //
//                        Copyright: (c) 2019 German Aerospace Center (DLR) //
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef CSP_SIMPLE_PLANETS_ATMOSPHERE_ECLIPSE_PHOTON_MAPPER_HPP
#define CSP_SIMPLE_PLANETS_ATMOSPHERE_ECLIPSE_PHOTON_MAPPER_HPP

#include <glm/glm.hpp>
#include <random>

namespace gpu {

// 6 * 4 = 24 Bytes
    struct Photon {
        glm::vec2 position;  // m
        glm::vec2 direction; // normalized
        uint32_t waveLength; // nm
        float intensity;     // 0..1 should start at 1
    };

    class AtmosphereEclipsePhotonMapper {
    public:
        AtmosphereEclipsePhotonMapper();

        uint32_t createShadowMap();

    private:
        void initTextureTracer();

        void traceThroughTexture(uint32_t ssboPhotons, size_t numPhotons);

        Photon emitPhoton();

        std::vector<Photon> generatePhotons(uint32_t count);

        struct {
            uint32_t uRectangleHeight;
            uint32_t uShadowLength;
            uint32_t uShadowHeight;
        } mTextureTracerUniforms;

        uint32_t mTextureTracerProgram;

        std::mt19937_64 mRNG;
        std::uniform_real_distribution<> mDistributionSun;
        std::uniform_int_distribution<uint32_t> mDistributionWavelength;
        std::bernoulli_distribution mDistributionBoolean;
    };

} // namespace gpu

#endif // CSP_SIMPLE_PLANETS_ATMOSPHERE_ECLIPSE_PHOTON_MAPPER_HPP
