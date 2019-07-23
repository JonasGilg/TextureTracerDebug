#include <GL/glew.h>
#include <GL/glut.h>

#include "AtmosphereEclipsePhotonMapper.hpp"
#include <iostream>

#include <sys/resource.h>

int main(int argc, char *argv[]) {
    const rlim_t stackSize = 64ul * 1024ul * 1024ul * 1024ul;
    rlimit rl{};
    getrlimit(RLIMIT_STACK, &rl);
    rl.rlim_cur = stackSize;
    setrlimit(RLIMIT_STACK, &rl);

    glutInit(&argc, argv);
    glutCreateWindow("Hurr durr OpenGL needs a window o.O");
    glewInit();

    std::cout << "Init Photon Mapper." << std::endl;
    auto mapper = gpu::AtmosphereEclipsePhotonMapper();
    std::cout << "Ready! Set! GO!" << std::endl;
    const auto planet = gpu::PlanetWithAtmosphere{6'371'000.0, 42'000, 2.504e19};

    mapper.createShadowMap(planet);

    return 0;
}