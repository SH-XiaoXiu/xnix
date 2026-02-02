# generate_rootfs.cmake
# 生成 rootfs.img FAT 镜像
#
# 变量:
#   ROOTFS_IMG      - 输出镜像路径
#   ROOTFS_SIZE     - 镜像大小 (KB)
#   ROOTFS_FILELIST - 文件列表文件，每行格式: src:dst

if (NOT ROOTFS_IMG)
    message(FATAL_ERROR "ROOTFS_IMG not set")
endif ()

if (NOT ROOTFS_SIZE)
    set(ROOTFS_SIZE 4096)  # 默认 4MB
endif ()

# 创建空镜像
execute_process(
        COMMAND dd if=/dev/zero of=${ROOTFS_IMG} bs=1024 count=${ROOTFS_SIZE}
        RESULT_VARIABLE DD_RESULT
        OUTPUT_QUIET ERROR_QUIET
)
if (NOT DD_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to create image file")
endif ()

# 格式化为 FAT12 (FAT16 需要更大的镜像)
execute_process(
        COMMAND mkfs.fat -F 12 ${ROOTFS_IMG}
        RESULT_VARIABLE MKFS_RESULT
        OUTPUT_QUIET ERROR_QUIET
)
if (NOT MKFS_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to format image")
endif ()

# 创建目录结构
execute_process(COMMAND mmd -i ${ROOTFS_IMG} ::sbin RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND mmd -i ${ROOTFS_IMG} ::drivers RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND mmd -i ${ROOTFS_IMG} ::bin RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND mmd -i ${ROOTFS_IMG} ::etc RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND mmd -i ${ROOTFS_IMG} ::demos RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)

# 从文件列表读取并复制文件
if (ROOTFS_FILELIST AND EXISTS ${ROOTFS_FILELIST})
    file(STRINGS ${ROOTFS_FILELIST} FILE_SPECS)
    foreach (FILE_SPEC ${FILE_SPECS})
        # 跳过空行
        string(STRIP "${FILE_SPEC}" FILE_SPEC)
        if ("${FILE_SPEC}" STREQUAL "")
            continue()
        endif ()

        string(REPLACE ":" ";" FILE_PARTS "${FILE_SPEC}")
        list(LENGTH FILE_PARTS PARTS_LEN)
        if (PARTS_LEN LESS 2)
            message(WARNING "Invalid file spec: ${FILE_SPEC}")
            continue()
        endif ()

        list(GET FILE_PARTS 0 SRC_FILE)
        list(GET FILE_PARTS 1 DST_PATH)

        if (EXISTS ${SRC_FILE})
            execute_process(
                    COMMAND mcopy -i ${ROOTFS_IMG} ${SRC_FILE} ::${DST_PATH}
                    RESULT_VARIABLE MCOPY_RESULT
                    OUTPUT_QUIET ERROR_QUIET
            )
            if (NOT MCOPY_RESULT EQUAL 0)
                message(WARNING "Failed to copy ${SRC_FILE} to ${DST_PATH}")
            else ()
                message(STATUS "  ${SRC_FILE} -> ${DST_PATH}")
            endif ()
        else ()
            message(WARNING "Source file not found: ${SRC_FILE}")
        endif ()
    endforeach ()
endif ()

message(STATUS "Generated rootfs.img (${ROOTFS_SIZE} KB)")
