# About
This repository contains a program written using AftrBurner Engine that visualizes Mars's surface. The program runs and the camera is placed on the surface of Mars. As the camera moves, data from a [Mars database](https://github.com/KSaunders98/mars_server) is loaded and rendered.
# Building
## Depedencies
In order to build this program, three dependencies are needed:
- The AftrBurner engine (most recent engine dev branch, most recent usr master branch)
- Boost libraries (if these are intalled, AftrBurner should link them automatically, just ensure the USE_BOOST flag is true)
- [Microsoft's C++ Rest SDK](https://github.com/microsoft/cpprestsdk)
    - See the README at the link above to install this dependency. After installing, CMake should find and link the library automatically. If you are on Windows and using vcpkg, you may need to follow these instructions:
        - Locate your installation of vcpkg.
        - Open the `BUILD 64 BIT MSVC 2019.bat` and/or the `BUILD 64 BIT MSVC 2019 with CMAKE GUI.bat` file(s) in this repo.
        - Locate your local installation of vcpkg.
        - In the build batch files change `<vcpkg install root>` to the path to your local vcpkg installation.
## Running CMake + Compiling
After all of the dependencies are installed, follow the following steps to build + run the program.
### Windows
- Run either `BUILD 64 BIT MSVC 2019.bat` or `BUILD 64 BIT MSVC 2019 with CMAKE GUI.bat`
- Open the newly created cwin64 folder in the project root, then open the MarsVisualization.sln solution file in Visual Studio.
- Simply build + run the program as you normally would in Visual Studio (Ctrl + F5 to build and run at once).
### Linux/Unix (*NOTE:* This program was not tested on Linux, but the steps should be the same as building any other AftrBurner module on Linux)
- Run either `build_linuxDbg.sh` or `build_linuxRel.sh`
- Follow the instructions that are output from the build script.
- Run the program from the newly created directory like so: `./MarsVisualization`