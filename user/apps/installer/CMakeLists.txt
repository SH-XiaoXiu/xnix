# installer 应用程序
# 系统安装工具，不自动启动，由用户手动运行

set(APP_NAME "installer")
set(APP_SOURCES main.c)
set(APP_LIBS c vfs -Wl,--whole-archive serial -Wl,--no-whole-archive)
set(APP_INSTALL_TO_SYSTEM ON)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
