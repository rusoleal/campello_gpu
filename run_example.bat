@echo off
setlocal

set BUILD_DIR=build_dx
set CONFIG=Debug
set EXE=%BUILD_DIR%\examples\windows\campello_example\%CONFIG%\campello_example.exe

echo [1/3] Configuring...
cmake -B %BUILD_DIR% -DBUILD_EXAMPLES=ON
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

echo [3/3] Launching %EXE%...
start "" "%EXE%"
