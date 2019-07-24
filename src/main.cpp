#include <GL/glew.h>
#include <GL/glut.h>

#include "AtmosphereEclipsePhotonMapper.hpp"

#include <sys/resource.h>
#include <iostream>

int main(int argc, char *argv[]) {
    const rlim_t stackSize = 4ul * 1024ul * 1024ul * 1024ul;
    rlimit rl{};
    getrlimit(RLIMIT_STACK, &rl);
    rl.rlim_cur = stackSize;
    setrlimit(RLIMIT_STACK, &rl);

    glutInit(&argc, argv);
    glutCreateWindow("Hurr durr OpenGL needs a window o.O");
    glewInit();

    auto mapper = gpu::AtmosphereEclipsePhotonMapper();

    mapper.createShadowMap();

    return 0;
}