#ifndef COMPUTESHADERDEBUG_CPUPHOTONMAPPER_HPP
#define COMPUTESHADERDEBUG_CPUPHOTONMAPPER_HPP

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cpu {
    typedef std::uint32_t uint;

    using glm::ivec2;
    using glm::uvec2;
    using glm::vec2;
    using glm::vec4;

    const uint TEX_WIDTH = 128u;
    const uint TEX_HEIGHT = TEX_WIDTH;

    const uint MIN_WAVELENGTH = 380u;
    const uint MAX_WAVELENGTH = 740u;
    const uint NUM_WAVELENGTHS = MAX_WAVELENGTH - MIN_WAVELENGTH;

    const float DL = 1000.0; // m
    const float DX = 10.0;   // m

// Size: 24 bytes -> ~40,000,000 photons per available gigabyte of ram
    struct Photon {
        vec2 position;   // m
        vec2 direction;  // normalized
        uint wavelength; // nm
        float intensity; // 0..1 should start at 1
    };

    struct Pixel {
        uint intensityAtWavelengths[NUM_WAVELENGTHS]; // [0..1000]
    };

    struct Planet {
        float radius;                         // m
        float atmosphericHeight;              // m
        float seaLevelMolecularNumberDensity; // cm^−3
    };

    struct Ray {
        vec2 origin;
        vec2 direction;
    };

    struct Rectangle {
        float x;
        float y;
        float w;
        float h;
    };

    class CPUPhotonMapper {
    public:
        CPUPhotonMapper() = default;

        void setAtmosphereHeight(float atmosphereHeight);

        void setSeaLevelMolecularNumberDensity(float seaLevelMolecularNumberDensity);

        void setPlanetRadius(float radius);

        void setRectangleHeight(float rectangleHeight);

        void setShadowLength(float shadowLength);

        void setShadowHeight(float shadowHeight);

        void setRectangleData(std::vector<vec4> rectangles);

        void setPhotonData(std::vector<Photon> photons);

        void setRefractiveIndices(
                std::array<std::array<float, NUM_WAVELENGTHS>, 42000> refractiveIndices);

        void setDensityData(std::vector<float> densities);

        void execute(uint gid);

        std::array<Pixel, TEX_WIDTH * TEX_HEIGHT> getPixelBuffer() {
            std::array<Pixel, TEX_WIDTH * TEX_HEIGHT> pixelsBuffer{};
            std::copy(std::begin(pixels), std::end(pixels), pixelsBuffer.begin());
            return pixelsBuffer;
        }

    private:
        vec2 getHorizontalRectangleAt(uint i);

        void addToPixel(uvec2 idx, uint wavelength, uint intensity);

        float densityAtAltitude(float altitude);

        float refractiveIndexAtSeaLevel(uint wavelength);

        float refractiveIndexAtAltitude(float altitude, uint wavelength);

        float partialRefractiveIndex(float altitude, float altitudeDelta,
                                     uint wavelength);

        void traceRay(Photon &photon);

        float molecularNumberDensityAtAltitude(float altitude);

        float rayleighScatteringCrossSection(uint wavelength);

        float rayleighVolumeScatteringCoefficient(float altitude, uint wavelength);

        void attenuateLight(Photon &photon, vec2 oldPosition);

        void tracePhoton(Photon &photon);

        Rectangle getRectangleAt(ivec2 indices);

        uint binarySearchForHorizontalRectangle(float x);

        ivec2 getRectangleIdxAt(vec2 location);

        float getRayIntersectAtX(Ray ray, float x);

        uint getRayRectangleExitEdge(Ray ray, Rectangle rect);

        void mirrorRayAroundUniversalXAxis(Ray &ray);

        std::vector<Photon> photons{};
        std::array<std::array<float, NUM_WAVELENGTHS>, 42000>
                refractiveIndicesAtAltitudes{}; // DX
        // steps
        std::array<float, 42000> densitiesAtAltitudes{};
        std::array<Pixel, TEX_WIDTH * TEX_HEIGHT> pixels{};
        std::array<vec4, TEX_WIDTH / 2u> horizontalRectangles{};

        float rectangleHeight{};
        Planet planet{};

        float shadowLength{};
        float shadowHeight{};
    };
}

#endif // COMPUTESHADERDEBUG_CPUPHOTONMAPPER_HPP
