# 辅助脚本：复制 initramfs 文件到临时目录

file(STRINGS "${FILELIST}" FILE_LINES)

foreach (LINE ${FILE_LINES})
    # 跳过空行
    if (NOT "${LINE}" STREQUAL "")
        # 分割 src:dst
        string(REGEX MATCH "^([^:]+):(.+)$" MATCH_RESULT "${LINE}")
        if (MATCH_RESULT)
            set(SRC "${CMAKE_MATCH_1}")
            set(DST "${CMAKE_MATCH_2}")

            if (EXISTS "${SRC}")
                # 创建目标目录
                get_filename_component(DST_DIR "${TMPDIR}/${DST}" DIRECTORY)
                file(MAKE_DIRECTORY "${DST_DIR}")

                # 复制文件
                file(COPY "${SRC}" DESTINATION "${DST_DIR}")

                # 重命名（如果需要）
                get_filename_component(SRC_NAME "${SRC}" NAME)
                get_filename_component(DST_NAME "${DST}" NAME)
                if (NOT "${SRC_NAME}" STREQUAL "${DST_NAME}")
                    file(RENAME "${DST_DIR}/${SRC_NAME}" "${DST_DIR}/${DST_NAME}")
                endif ()

                message(STATUS "  Added: ${DST}")
            endif ()
        endif ()
    endif ()
endforeach ()
