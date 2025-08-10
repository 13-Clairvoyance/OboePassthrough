cmake_minimum_required(VERSION 3.10.2)

# Give the project a name
project(OboePassthrough)

# Pull in the Oboe build
add_subdirectory(oboe)

# Build your native library
add_library(
        native-lib
        SHARED
        native-lib.cpp
        kiss_fft.c
        kiss_fftr.c
)

# Include Oboe headers
target_include_directories(native-lib PRIVATE oboe/include)

# Link against Oboe and Android log
target_link_libraries(
        native-lib
        oboe
        log
)
