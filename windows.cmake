
#find_package(Metal)

add_library(${PROJECT_NAME} SHARED
    src/directx/context.cpp
    src/directx/device.cpp
)

target_link_libraries(${PROJECT_NAME}
    D3d12
)

target_include_directories (${PROJECT_NAME} PUBLIC 
                            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>"
                            )

target_include_directories(${PROJECT_NAME} PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           )

