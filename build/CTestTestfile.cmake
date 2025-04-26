# CMake generated Testfile for 
# Source directory: /app
# Build directory: /app/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test-orderbook "/app/build/test-orderbook")
set_tests_properties(test-orderbook PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;50;add_test;/app/CMakeLists.txt;0;")
subdirs("_deps/catch2-build")
