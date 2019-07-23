cmake_minimum_required(VERSION 3.12)
project(ComputeShaderDebug)

set(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ldl")

list(APPEND CMAKE_MODULE_PATH "${TOOLS}/cmake/cmake-find-scripts")

find_package(OpenGL REQUIRED)

set(GLM_INCLUDE_DIR "${TOOLS}/${GLM_INCLUDE}")
find_package(GLM REQUIRED)
if (GLM_FOUND)
    include_directories(${GLM_INCLUDE_DIRS})
endif ()

find_package(GLUT REQUIRED)
if (GLUT_FOUND)
    include_directories(${GLUT_INCLUDE_DIRS})
else ()
    MESSAGE(WARNING "GLUT NOT FOUND")
endif ()

set(GLEW_ROOT_DIR "${TOOLS}/${GLEW_ROOT}")
find_package(GLEW REQUIRED)
if (GLEW_FOUND)
    include_directories(${GLEW_INCLUDE_DIRS})
else ()
    MESSAGE(WARNING "GLEW NOT FOUND")
endif ()


file(GLOB SOURCE_FILES src/*.cpp)

add_executable(ComputeShaderDebug ${SOURCE_FILES})

target_link_libraries(ComputeShaderDebug
        ${OPENGL_gl_LIBRARY}
        ${GLEW_LIBRARIES}
        ${GLUT_LIBRARIES}
        )

file(COPY "resources" DESTINATION ".")