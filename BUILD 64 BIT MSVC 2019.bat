REM Ensure cmake is in your Environment's PATH
cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE="<vcpkg install root>\scripts\buildsystems\vcpkg.cmake" -S ./src/ -B ./cwin64
