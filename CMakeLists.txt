cmake_minimum_required(VERSION 3.14)

project(bayan VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost CONFIG REQUIRED COMPONENTS program_options filesystem)

add_executable(bayan main.cpp)
target_link_libraries(bayan PRIVATE Boost::program_options Boost::filesystem)

if(MSVC)
    target_compile_options(bayan PRIVATE /W4 /WX)
else()
    target_compile_options(bayan PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

install(TARGETS bayan RUNTIME DESTINATION bin)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_CONTACT "irb259@gmail.com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)
