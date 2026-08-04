# CMake generated Testfile for 
# Source directory: /Volumes/SabrentM2/ML/projects/science/StormBitmaps
# Build directory: /Volumes/SabrentM2/ML/projects/science/StormBitmaps/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(storm_correctness "/Volumes/SabrentM2/ML/projects/science/StormBitmaps/build/test_storm")
set_tests_properties(storm_correctness PROPERTIES  _BACKTRACE_TRIPLES "/Volumes/SabrentM2/ML/projects/science/StormBitmaps/CMakeLists.txt;167;add_test;/Volumes/SabrentM2/ML/projects/science/StormBitmaps/CMakeLists.txt;0;")
add_test(cells_correctness "/Volumes/SabrentM2/ML/projects/science/StormBitmaps/build/test_cells")
set_tests_properties(cells_correctness PROPERTIES  _BACKTRACE_TRIPLES "/Volumes/SabrentM2/ML/projects/science/StormBitmaps/CMakeLists.txt;173;add_test;/Volumes/SabrentM2/ML/projects/science/StormBitmaps/CMakeLists.txt;0;")
subdirs("third_party/CRoaring")
