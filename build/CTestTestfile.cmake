# CMake generated Testfile for 
# Source directory: C:/PROJECT/HTMSR
# Build directory: C:/PROJECT/HTMSR/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[htmsr_smoke_test]=] "C:/PROJECT/HTMSR/build/Debug/htmsr_smoke_test.exe")
  set_tests_properties([=[htmsr_smoke_test]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/PROJECT/HTMSR/CMakeLists.txt;91;add_test;C:/PROJECT/HTMSR/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[htmsr_smoke_test]=] "C:/PROJECT/HTMSR/build/Release/htmsr_smoke_test.exe")
  set_tests_properties([=[htmsr_smoke_test]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/PROJECT/HTMSR/CMakeLists.txt;91;add_test;C:/PROJECT/HTMSR/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[htmsr_smoke_test]=] "C:/PROJECT/HTMSR/build/MinSizeRel/htmsr_smoke_test.exe")
  set_tests_properties([=[htmsr_smoke_test]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/PROJECT/HTMSR/CMakeLists.txt;91;add_test;C:/PROJECT/HTMSR/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[htmsr_smoke_test]=] "C:/PROJECT/HTMSR/build/RelWithDebInfo/htmsr_smoke_test.exe")
  set_tests_properties([=[htmsr_smoke_test]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/PROJECT/HTMSR/CMakeLists.txt;91;add_test;C:/PROJECT/HTMSR/CMakeLists.txt;0;")
else()
  add_test([=[htmsr_smoke_test]=] NOT_AVAILABLE)
endif()
