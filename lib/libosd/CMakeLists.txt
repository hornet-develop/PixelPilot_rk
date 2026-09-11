find_package(PkgConfig REQUIRED)

pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(SPDLOG REQUIRED IMPORTED_TARGET spdlog)
pkg_check_modules(NLOHMANN REQUIRED IMPORTED_TARGET nlohmann_json)

add_library(osd STATIC
    source/osd.cpp
    source/fact.cpp
    source/layout.cpp
    source/widgets/base.cpp
    source/widgets/primitives.cpp
    source/widgets/components.cpp
    source/helpers/running_average.cpp
)

target_compile_features(osd
    PUBLIC cxx_std_17
)

target_include_directories(osd
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source
)

target_link_libraries(osd
    PUBLIC
        PkgConfig::CAIRO
        PkgConfig::NLOHMANN
    PRIVATE
        PkgConfig::SPDLOG
)
