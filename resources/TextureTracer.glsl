#version 430

#extension GL_EXT_compute_shader: enable
#extension GL_EXT_shader_storage_buffer_object: enable
#extension GL_ARB_compute_variable_group_size: enable

const uint TEX_WIDTH = 1024u;
const uint TEX_HEIGHT = TEX_WIDTH;

const uint MIN_WAVELENGTH = 380u;
const uint MAX_WAVELENGTH = 740u;
const uint NUM_WAVELENGTHS = MAX_WAVELENGTH - MIN_WAVELENGTH;

// Size: 24 bytes -> ~40,000,000 photons per available gigabyte of ram
struct Photon {
    vec2 position;// m
    vec2 direction;// normalized
    uint wavelength;// nm
    float intensity;// 0..1 should start at 1
};

layout(std430, binding = 0) buffer Photons {
    Photon photons[];
};

// Size: 1440 bytes -> ~700,000 pixels per available gigabyte of ram
struct Pixel {
    uint intensityAtWavelengths[NUM_WAVELENGTHS];// [0..1000]
};

layout(std430, binding = 1) buffer Pixels {
//Pixel pixels[TEX_WIDTH][TEX_HEIGHT];
// NVIDIAs linker takes ages to link if the sizes are specified :(
    Pixel[] pixels;
};

uniform float xAxisScalingFactor;

vec2 getHorizontalRectangleAt(int i) {
    float x = pow(float(i), xAxisScalingFactor);
    float w = pow(float(i + 1), xAxisScalingFactor);
    return vec2(x, w);
}

uniform float rectangleHeight;

struct Rectangle {
    float x;
    float y;
    float w;
    float h;
};

layout (local_size_variable) in;

void addToPixel(uvec2 idx, uint wavelength, uint intensity) {
    if (idx.x >= 0u && idx.x < TEX_WIDTH && idx.y >= 0u && idx.y < TEX_HEIGHT) {
        uint index = (idx.y * TEX_WIDTH) + idx.x;
        atomicAdd(pixels[index].intensityAtWavelengths[wavelength - MIN_WAVELENGTH], intensity);
    }
}

/// Returns the rectangle at the given indices.
Rectangle getRectangleAt(ivec2 indices) {
    vec2 horRect = getHorizontalRectangleAt(indices.x);
    return Rectangle(horRect.x, rectangleHeight * float(indices.y), horRect.y, rectangleHeight);
}

uniform float shadowLength;
uniform float shadowHeight;

/// Returns the indices of the rectangle at the given location
ivec2 getRectangleIdxAt(vec2 location) {
    if (location.x >= 0 && location.x < shadowLength && location.y >= 0 && location.y < shadowHeight) {
        int x = 0;
        int y = int(location.y / rectangleHeight);
        return ivec2(x, y);
    } else return ivec2(TEX_WIDTH, TEX_HEIGHT);
}

float getRayIntersectAtX(Photon ray, float x) {
    float slope = ray.direction.y / ray.direction.x;
    return slope * (x - ray.position.x) + ray.position.y;
}

ivec2 getRayRectangleExitEdge(Photon ray, Rectangle rect) {
    float intersectHeight = getRayIntersectAtX(ray, rect.x + rect.w);

    // IF ONE OF THE FIRST TWO CONDITIONS GETS REMOVED IT WORKS WITH 1'000'000 PHOTONS OTHERWISE ONLY 100'000 WHY?
    if (intersectHeight < rect.y) {
        return ivec2(0, -1);
    } else if (intersectHeight > rect.y + rect.h) {
        return ivec2(0, 1);
    } else {
        return ivec2(1, 0);
    }
}

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= photons.length()) return;

    Photon photon = photons[gid];

    ivec2 photonTexIndices = getRectangleIdxAt(photon.position);
    while (photonTexIndices.x < TEX_WIDTH && photonTexIndices.y < TEX_HEIGHT &&
    photonTexIndices.x >= 0        && photonTexIndices.y >= 0) {
        // need to convert to uint for atomic add operations...
        addToPixel(uvec2(photonTexIndices), photon.wavelength, uint(photon.intensity * 100.0));

        ivec2 dir = getRayRectangleExitEdge(photon, getRectangleAt(photonTexIndices));
        photonTexIndices += dir;

        // When the ray goes out of bounds on the bottom then mirror it to simulate rays coming from
        // the other side of the planet. This works because of the rotational symmetry of the system.
        // IF COMMENTET OUT IT WORKS WITH 1'000'000 PHOTONS OTHERWISE ONLY 100'000 WHY?
        if (photonTexIndices.y < 0) {
            photonTexIndices.y = 0;
            photon.position.y *= -1.0;
            photon.direction.y *= -1.0;
        }
    }
}
