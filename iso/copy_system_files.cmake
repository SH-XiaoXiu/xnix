# 辅助脚本：复制文件到 system.img (FAT32)

file(STRINGS "${FILELIST}" FILE_LINES)

set(FILE_COUNT 0)
foreach (LINE ${FILE_LINES})
    # 跳过空行和注释
    if (NOT "${LINE}" STREQUAL "" AND NOT "${LINE}" MATCHES "^#")
        # 分割 src:dst
        string(REGEX MATCH "^([^:]+):(.+)$" MATCH_RESULT "${LINE}")
        if (MATCH_RESULT)
            set(SRC "${CMAKE_MATCH_1}")
            set(DST "${CMAKE_MATCH_2}")

            if (EXISTS "${SRC}")
                math(EXPR FILE_COUNT "${FILE_COUNT} + 1")
                message(STATUS "  [${FILE_COUNT}] ${DST}")

                # 使用 mcopy 复制文件
                execute_process(
                        COMMAND mcopy -i ${IMG_FILE} -o ${SRC} ::${DST}
                        RESULT_VARIABLE MCOPY_RESULT
                        OUTPUT_QUIET
                        ERROR_QUIET
                )

                if (NOT MCOPY_RESULT EQUAL 0)
                    message(WARNING "    Failed to copy ${SRC}")
                endif ()
            else ()
                message(STATUS "  SKIP: ${SRC} (not found)")
            endif ()
        endif ()
    endif ()
endforeach ()

message(STATUS "system.img created: ${FILE_COUNT} files")
