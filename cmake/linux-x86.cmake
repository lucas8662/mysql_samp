# 32-bit Linux build on a 64-bit host. SA-MP and open.mp servers are 32-bit,
# so the plugin must be too. Selected automatically by CMakeLists.txt on Linux.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_FLAGS_INIT "-m32")
set(CMAKE_CXX_FLAGS_INIT "-m32")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-m32")

# Debian/Ubuntu multiarch location of the :i386 packages (libssl-dev:i386 ...).
set(CMAKE_LIBRARY_ARCHITECTURE i386-linux-gnu)
