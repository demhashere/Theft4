# CMake 4 no longer finds the legacy FindPythonInterp module for glslang's
# find_package(PythonInterp 3); answer it from the Python 3 interpreter.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
set(PYTHON_VERSION_STRING "${Python3_VERSION}")
set(PYTHON_VERSION_MAJOR "${Python3_VERSION_MAJOR}")
set(PYTHON_VERSION_MINOR "${Python3_VERSION_MINOR}")
set(PYTHONINTERP_FOUND TRUE)
set(PythonInterp_VERSION "${Python3_VERSION}")
