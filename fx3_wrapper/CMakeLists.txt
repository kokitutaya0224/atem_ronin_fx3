cmake_minimum_required(VERSION 3.16)
project(fx3cli CXX)
set(CMAKE_CXX_STANDARD 17)

# Sony Camera Remote SDKの展開先を指定してビルド:
#   cmake -B build -DCRSDK_DIR=/path/to/CrSDK_vX.XX.XX_20XXXXXXa_Mac
#   cmake --build build
# CRSDK_DIR未指定の場合はスタブモード（プロトコル確認用）でビルドされる

add_executable(fx3cli main.cpp)

if(DEFINED CRSDK_DIR)
    target_include_directories(fx3cli PRIVATE
        ${CRSDK_DIR}/app/CRSDK
        ${CRSDK_DIR}/include)
    # SDKの動的ライブラリ (macOS: libCr_Core.dylib ほか)
    file(GLOB CRSDK_LIBS ${CRSDK_DIR}/external/crsdk/*.dylib)
    target_link_libraries(fx3cli PRIVATE ${CRSDK_LIBS})
    # 実行時にdylibを見つけられるようにする
    set_target_properties(fx3cli PROPERTIES
        BUILD_RPATH "${CRSDK_DIR}/external/crsdk")
    message(STATUS "CrSDK: ${CRSDK_DIR}")
else()
    message(WARNING "CRSDK_DIR未指定 — スタブモードでビルドします")
endif()
