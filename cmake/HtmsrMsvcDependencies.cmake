# MSVC 的本地化 /showIncludes 前缀可能随 VS、终端编码发生变化，导致 Ninja
# 静默记录零条头文件依赖。配置和编译统一使用 ASCII 前缀，避免新旧类布局混编。
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_GENERATOR MATCHES "^Ninja")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "Note: including file: ")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}")
    # 保留用户已有的编译启动器；只改变本次编译子进程的语言，不更改系统设置。
    list(PREPEND CMAKE_CXX_COMPILER_LAUNCHER
        "${CMAKE_COMMAND}" "-E" "env" "VSLANG=1033" "--")
endif()
