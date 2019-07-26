#include <GL/glew.h>
#include <GL/freeglut.h>

#include "TextureTracer.hpp"

int main(int argc, char *argv[]) {
    glutInit(&argc, argv);
    glutCreateWindow("Hurr durr OpenGL needs a window o.O");
    glewInit();

    auto mapper = gpu::TextureTracer();

    // WITH 100'000 PHOTONS IT WORKS, WITH 1'000'000 PHOTONS NOT WHY?
    mapper.createShadowMap(10'000'000);

    return 0;
}