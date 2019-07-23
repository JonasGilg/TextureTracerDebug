////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR                               //
//      and may be used under the terms of the MIT license. See the LICENSE file for details.     //
//                        Copyright: (c) 2019 German Aerospace Center (DLR)                       //
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef CSP_SIMPLE_PLANETS_REFRACTIVE_INDEX_PRECALCULATOR_HPP
#define CSP_SIMPLE_PLANETS_REFRACTIVE_INDEX_PRECALCULATOR_HPP

#include <cstdint>
#include <utility>

struct AtmosphericProperties {
  float height;
  float gravity;
  float molarMass;
  float seaLevelMolecularNumberDensity;
};

struct AtmosphericLayer {
  float baseTemperature;      // K
  float temperatureLapseRate; // K / m
  float staticPressure;       // Pa
  float baseHeight;           // m
};

class LUTPrecalculator {
public:
  LUTPrecalculator();

  std::pair<uint32_t, uint32_t> createLUT(AtmosphericProperties props);

private:
  struct Uniforms {
    struct {
      uint32_t uAtmosphericHeight;
      uint32_t uGravity;
      uint32_t uMolarMass;
      uint32_t uSeaLevelMolecularNumberDensity;
    } planet;

    struct {
      uint32_t uA;
      uint32_t uNumTerms;
      uint32_t uTerms[8];
    } sellmeierCoefficients;

  } mUniforms;

  uint32_t mProgram;
};

#endif //CSP_SIMPLE_PLANETS_REFRACTIVE_INDEX_PRECALCULATOR_HPP
