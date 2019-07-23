#include "CpuPhotonMapper.hpp"
#include <glm/glm.hpp>

namespace cpu {

    vec2 CPUPhotonMapper::getHorizontalRectangleAt(uint i) {
        vec4 entry = horizontalRectangles[i / 2];
        if (i % 2 == 0) {
            return vec2(entry.x, entry.y); // entry.xy();
        } else {
            return vec2(entry.z, entry.w); // entry.zw();
        }
    }

    void CPUPhotonMapper::addToPixel(uvec2 idx, uint wavelength, uint intensity) {
        if (idx.x >= 0u && idx.x < TEX_WIDTH && idx.y >= 0u && idx.y < TEX_HEIGHT) {
            uint index = (idx.y * TEX_WIDTH) + idx.x;

            // atomic
            pixels[index].intensityAtWavelengths[wavelength - MIN_WAVELENGTH] += intensity;
        }
    }

    float CPUPhotonMapper::densityAtAltitude(float altitude) {
        return densitiesAtAltitudes[static_cast<uint>(altitude)];
    }

    float CPUPhotonMapper::refractiveIndexAtSeaLevel(uint wavelength) {
        return refractiveIndicesAtAltitudes[0][wavelength - MIN_WAVELENGTH];
    }

    float CPUPhotonMapper::refractiveIndexAtAltitude(float altitude,
                                                     uint wavelength) {
        return refractiveIndicesAtAltitudes[static_cast<uint>(altitude)]
        [wavelength - MIN_WAVELENGTH];
    }

    float CPUPhotonMapper::partialRefractiveIndex(float altitude, float altitudeDelta, uint
    wavelength) {
        float refrIndexPlusDelta = refractiveIndexAtAltitude(altitudeDelta, wavelength);
        float refrIndex = refractiveIndexAtAltitude(altitude, wavelength);

        return (refrIndexPlusDelta - refrIndex) / DX;
    }

/// Moves the photon to its next location.
    void CPUPhotonMapper::traceRay(Photon &photon) {
        float altitude = glm::length(photon.position) - planet.radius;
        float altitudeDelta = glm::length(photon.position + vec2(DX, DX)) - planet.radius;

        if (altitude < planet.atmosphericHeight && altitudeDelta < planet.atmosphericHeight) {
            float ni = refractiveIndexAtAltitude(altitude, photon.wavelength);
            float dn = partialRefractiveIndex(
                    altitude, altitudeDelta,
                    photon.wavelength);

            float ni1 = ni + dn;
            photon.direction = ((ni * photon.direction) + (dn * DL)) / ni1;
            photon.direction = glm::normalize(photon.direction);
        }

        photon.position += (DL * photon.direction);
    }

    float CPUPhotonMapper::molecularNumberDensityAtAltitude(float altitude) {
        float seaLevelDensity = densityAtAltitude(0.0);
        return planet.seaLevelMolecularNumberDensity *
               (densityAtAltitude(altitude) / seaLevelDensity);
    }

    float CPUPhotonMapper::rayleighScatteringCrossSection(uint wavelength) {
        float wavelengthInCM = float(wavelength) * 1.0e-7;
        float wavelengthInCM2 = wavelengthInCM * wavelengthInCM;
        float wavelengthInCM4 = wavelengthInCM2 * wavelengthInCM2;

        float refractiveIndex = refractiveIndexAtSeaLevel(wavelength);
        float refractiveIndex2 = refractiveIndex * refractiveIndex;

        float molecularNumberDensity = molecularNumberDensityAtAltitude(0.0);
        float molecularNumberDensity2 =
                molecularNumberDensity * molecularNumberDensity;

        const float kingCorrelationFactor = 1.05;
        const float PI_F = 3.14159;
        const float PI_F_3 = PI_F * PI_F * PI_F;

        float dividend = 24.0 * PI_F_3 * pow(refractiveIndex2 - 1.0, 2);
        float divisor =
                wavelengthInCM4 * molecularNumberDensity2 * pow(refractiveIndex2 + 2, 2);
        return (dividend / divisor) * kingCorrelationFactor;
    }

// TODO maybe precompute in a 2D map?
    float CPUPhotonMapper::rayleighVolumeScatteringCoefficient(float altitude,
                                                               uint wavelength) {
        float sigma = rayleighScatteringCrossSection(wavelength);
        float mnd = molecularNumberDensityAtAltitude(altitude);
        return mnd * sigma;
    }

/// Applies rayleigh scattering to the photon for this step.
    void CPUPhotonMapper::attenuateLight(Photon &photon, vec2 oldPosition) {
        float altitude = glm::length(oldPosition) - planet.radius;

        float beta = rayleighVolumeScatteringCoefficient(altitude, photon.wavelength);

        // TODO don't know what to do with this for now... maybe make it configurable
        // per planet?
        float alpha = 0.0;

        photon.intensity = photon.intensity * glm::exp(-(alpha + beta) * DL);
    }

/// Does a single step of the ray tracing. It moves the photon to the next
/// location and applies rayleigh scattering to it.
    void CPUPhotonMapper::tracePhoton(Photon &photon) {
        vec2 oldPosition = photon.position;

        traceRay(photon);
        attenuateLight(photon, oldPosition);
    }

/// Returns the rectangle at the given indices.
    Rectangle CPUPhotonMapper::getRectangleAt(ivec2 indices) {
        vec2 horRect = getHorizontalRectangleAt(indices.x);
        return Rectangle{horRect.x, rectangleHeight * float(indices.y), horRect.y,
                         rectangleHeight};
    }

/// Searches for the horizontal index in the rectangle array which contains x.
    uint CPUPhotonMapper::binarySearchForHorizontalRectangle(float x) {
        uint l = 0u;
        uint r = TEX_WIDTH - 1u;

        while (l <= r) {
            uint m = (l + r) / 2u;
            vec2 rectM = getHorizontalRectangleAt(m);
            if (rectM.x + rectM.y < x) {
                l = m + 1u;
            } else if (rectM.x > x) {
                r = m - 1u;
            } else
                return m;
        }

        return TEX_WIDTH; // outside of grid (should never happen in any reasonable
        // scenario...)
    }

/// Returns the indices of the rectangle at the given location
    ivec2 CPUPhotonMapper::getRectangleIdxAt(vec2 location) {
        if (location.x >= 0 && location.x < shadowLength && location.y >= 0 &&
            location.y < shadowHeight) {
            uint x = binarySearchForHorizontalRectangle(location.x);
            uint y = uint(location.y / rectangleHeight);
            return ivec2(x, y);
        } else
            return ivec2(TEX_WIDTH, TEX_HEIGHT);
    }

    float CPUPhotonMapper::getRayIntersectAtX(Ray ray, float x) {
        float slope = ray.direction.y / ray.direction.x;
        return slope * (x - ray.origin.x) + ray.origin.y;
    }

    const uint TOP = 0u;
    const uint RIGHT = 1u;
    const uint BOTTOM = 2u;

    uint CPUPhotonMapper::getRayRectangleExitEdge(Ray ray, Rectangle rect) {
        float intersectHeight = getRayIntersectAtX(ray, rect.x + rect.w);
        if (intersectHeight < rect.y)
            return BOTTOM;
        else if (intersectHeight > rect.y + rect.h)
            return TOP;
        else
            return RIGHT;
    }

    void CPUPhotonMapper::mirrorRayAroundUniversalXAxis(Ray &ray) {
        ray.origin.y = -ray.origin.y;
        ray.direction.y = -ray.direction.y;
    }

    void CPUPhotonMapper::execute(uint gid) {
        // uint gid = gl_GlobalInvocationID.x;
        if (gid >= photons.size())
            return;

        Photon photon = photons[gid];

        bool enteredAtmosphere = false;
        bool exitedAtmosphere = false;

        float atmosphereRadius = planet.radius + planet.atmosphericHeight;

        int loopCounter = 0;
        while (!exitedAtmosphere && glm::length(photon.position) > planet.radius) {
            if (glm::length(photon.position) > planet.radius) {
                tracePhoton(photon);
                loopCounter++;
            }

            if (!enteredAtmosphere && glm::length(photon.position) < atmosphereRadius) {
                enteredAtmosphere = true;
            }

            if (enteredAtmosphere && glm::length(photon.position) > atmosphereRadius) {
                exitedAtmosphere = true;
            }
        }

        if (glm::length(photon.position) < planet.radius || !enteredAtmosphere) {
            return;
        }

        Ray photonRay = Ray{photon.position, photon.direction};

        glm::ivec2 photonTexIndices = getRectangleIdxAt(photon.position);
        while (photonTexIndices.x < TEX_WIDTH && photonTexIndices.y < TEX_HEIGHT &&
               photonTexIndices.x >= 0 && photonTexIndices.y >= 0) {
            // need to convert to uint for atomic add operations...
            addToPixel(photonTexIndices, photon.wavelength, uint(photon.intensity * 100.0));

            const uint dir = getRayRectangleExitEdge(photonRay, getRectangleAt(photonTexIndices));
            switch (dir) {
                case TOP:
                    photonTexIndices.y++;
                    break;
                case BOTTOM:
                    photonTexIndices.y--;
                    break;
                default:
                    photonTexIndices.x++;
            }

            // When the ray goes out of bounds on the bottom then mirror it to simulate
            // rays coming from the other side of the planet. This works because of the
            // rotational symmetry of the system.
            if (photonTexIndices.y < 0) {
                photonTexIndices.y = 0;
                mirrorRayAroundUniversalXAxis(photonRay);
            }
        }
    }

    void CPUPhotonMapper::setAtmosphereHeight(float atmosphereHeight) {
        this->planet.atmosphericHeight = atmosphereHeight;
    }

    void CPUPhotonMapper::setSeaLevelMolecularNumberDensity(
            float seaLevelMolecularNumberDensity) {
        this->planet.seaLevelMolecularNumberDensity = seaLevelMolecularNumberDensity;
    }

    void CPUPhotonMapper::setPlanetRadius(float radius) {
        this->planet.radius = radius;
    }

    void CPUPhotonMapper::setRectangleHeight(float rectangleHeight) {
        this->rectangleHeight = rectangleHeight;
    }

    void CPUPhotonMapper::setShadowLength(float shadowLength) {
        this->shadowLength = shadowLength;
    }

    void CPUPhotonMapper::setShadowHeight(float shadowHeight) {
        this->shadowHeight = shadowHeight;
    }

    void CPUPhotonMapper::setRectangleData(std::vector<vec4> rectangles) {
        std::copy(rectangles.begin(), rectangles.end(),
                  std::begin(this->horizontalRectangles));
    }

    void CPUPhotonMapper::setPhotonData(std::vector<Photon> photons) {
        this->photons = photons;
    }

    void CPUPhotonMapper::setDensityData(std::vector<float> densities) {
        std::copy(densities.begin(), densities.end(),
                  std::begin(densitiesAtAltitudes));
    }

    void CPUPhotonMapper::setRefractiveIndices(
            std::array<std::array<float, NUM_WAVELENGTHS>, 42000> refractiveIndices) {
        for (int i = 0; i < refractiveIndices.size(); ++i) {
            for (int w = 0; w < NUM_WAVELENGTHS; ++w) {
                this->refractiveIndicesAtAltitudes[i][w] = refractiveIndices[i][w];
            }
        }
    }
}