@echo off
setlocal

set BUILD_DIR=build_test
set CONFIG=Debug
set CMAKE_FLAGS=-DBUILD_TESTS=ON

:: Pass --integration to also build and run GPU integration tests
if "%1"=="--integration" (
    set CMAKE_FLAGS=-DBUILD_INTEGRATION_TESTS=ON
)

echo [1/3] Configuring...
cmake -B %BUILD_DIR% %CMAKE_FLAGS%
if %ERRORLEVEL% neq 0 (
    echo Configure failed.
    exit /b %ERRORLEVEL%
)

echo [2/3] Building...
cmake --build %BUILD_DIR% --config %CONFIG%
if %ERRORLEVEL% neq 0 (
    echo Build failed.
    exit /b %ERRORLEVEL%
)

echo [3/3] Running tests...
ctest --test-dir %BUILD_DIR% --build-config %CONFIG% --output-on-failure
exit /b %ERRORLEVEL%
