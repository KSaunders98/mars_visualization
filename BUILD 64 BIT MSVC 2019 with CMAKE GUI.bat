REM Ensure cmake and cmake-gui are in your Environment's PATH
cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE="<vcpkg install root>\scripts\buildsystems\vcpkg.cmake" -S ./src/ -B ./cwin64
start cmake-gui ./cwin64
