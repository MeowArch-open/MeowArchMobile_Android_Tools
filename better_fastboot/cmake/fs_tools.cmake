# fs_tools.cmake — Android-forked mke2fs + make_f2fs for the fastboot package
# ============================================================================
# `fastboot -w` (and `fastboot flash <part>` with filesystem generation) shells
# out to mke2fs / make_f2fs next to the fastboot binary, exactly like Google's
# platform-tools dist. Official AOSP builds these from:
#   external/e2fsprogs  (Android fork — `-E android_sparse` emits sparse images)
#   external/f2fs-tools (Android fork — `-S` sparse mode, `-g android` preset)
# Upstream autotools builds of vanilla e2fsprogs/f2fs-tools do NOT have these
# Android extensions, so we compile the forks directly, mirroring each
# Android.bp host module (mke2fs host static_libs, make_f2fs_host_defaults)
# — no configure/make needed; the forks carry checked-in Android config
# headers (e2fsprogs lib/config.h with ENABLE_LIBSPARSE, f2fs
# include/android_config.h activated by -DWITH_ANDROID).

# ------------------------------------------------------------ e2fsprogs
set(E2_LIB "${E2FSPROGS}/lib")

# Common flags from the soong "e2fsprogs-defaults" (minus -Werror).
set(E2FS_C_FLAGS
    -Wall
    -Wno-pointer-arith
    -Wno-sign-compare
    -Wno-type-limits
    -Wno-unused-parameter
)

set(E2_INCS
    ${E2_LIB}                    # "config.h", <ext2fs/...>, <et/...>, <uuid/...>,
                                 # <blkid/...>, <e2p/...>, <support/...>
    ${E2_LIB}/ext2fs             # flat private headers ("ext2fs.h" etc.)
    ${E2_LIB}/blkid
    ${E2_LIB}/uuid
    ${E2_LIB}/et
    ${E2_LIB}/e2p
    ${E2_LIB}/support
    ${E2FSPROGS}                 # "../version.h" from misc/
    ${E2FSPROGS}/misc
    ${E2FSPROGS}/e2fsck          # soong include_dirs for mke2fs
)

# --- libext2_com_err (lib/et)
add_library(ext2_com_err STATIC
    ${E2_LIB}/et/error_message.c
    ${E2_LIB}/et/et_name.c
    ${E2_LIB}/et/init_et.c
    ${E2_LIB}/et/com_err.c
    ${E2_LIB}/et/com_right.c
)
target_include_directories(ext2_com_err PUBLIC ${E2_INCS})
target_compile_options(ext2_com_err PRIVATE ${E2FS_C_FLAGS})

# --- libext2_uuid (lib/uuid) — the fork's uuid lib (BSD), self-contained
add_library(ext2_uuid STATIC
    ${E2_LIB}/uuid/clear.c
    ${E2_LIB}/uuid/compare.c
    ${E2_LIB}/uuid/copy.c
    ${E2_LIB}/uuid/gen_uuid.c
    ${E2_LIB}/uuid/isnull.c
    ${E2_LIB}/uuid/pack.c
    ${E2_LIB}/uuid/parse.c
    ${E2_LIB}/uuid/unpack.c
    ${E2_LIB}/uuid/unparse.c
    ${E2_LIB}/uuid/uuid_time.c
)
target_include_directories(ext2_uuid PUBLIC ${E2_INCS})
target_compile_options(ext2_uuid PRIVATE ${E2FS_C_FLAGS})

# --- libext2_blkid (lib/blkid)
add_library(ext2_blkid STATIC
    ${E2_LIB}/blkid/cache.c
    ${E2_LIB}/blkid/dev.c
    ${E2_LIB}/blkid/devname.c
    ${E2_LIB}/blkid/devno.c
    ${E2_LIB}/blkid/getsize.c
    ${E2_LIB}/blkid/llseek.c
    ${E2_LIB}/blkid/probe.c
    ${E2_LIB}/blkid/read.c
    ${E2_LIB}/blkid/resolve.c
    ${E2_LIB}/blkid/save.c
    ${E2_LIB}/blkid/tag.c
    ${E2_LIB}/blkid/version.c
)
target_include_directories(ext2_blkid PUBLIC ${E2_INCS})
target_link_libraries(ext2_blkid PUBLIC ext2_uuid)
target_compile_options(ext2_blkid PRIVATE ${E2FS_C_FLAGS})

# --- libext2_e2p (lib/e2p)
add_library(ext2_e2p STATIC
    ${E2_LIB}/e2p/encoding.c
    ${E2_LIB}/e2p/errcode.c
    ${E2_LIB}/e2p/feature.c
    ${E2_LIB}/e2p/fgetflags.c
    ${E2_LIB}/e2p/fsetflags.c
    ${E2_LIB}/e2p/fgetproject.c
    ${E2_LIB}/e2p/fsetproject.c
    ${E2_LIB}/e2p/fgetversion.c
    ${E2_LIB}/e2p/fsetversion.c
    ${E2_LIB}/e2p/getflags.c
    ${E2_LIB}/e2p/getversion.c
    ${E2_LIB}/e2p/hashstr.c
    ${E2_LIB}/e2p/iod.c
    ${E2_LIB}/e2p/ljs.c
    ${E2_LIB}/e2p/ls.c
    ${E2_LIB}/e2p/mntopts.c
    ${E2_LIB}/e2p/parse_num.c
    ${E2_LIB}/e2p/pe.c
    ${E2_LIB}/e2p/pf.c
    ${E2_LIB}/e2p/ps.c
    ${E2_LIB}/e2p/setflags.c
    ${E2_LIB}/e2p/setversion.c
    ${E2_LIB}/e2p/uuid.c
    ${E2_LIB}/e2p/ostype.c
    ${E2_LIB}/e2p/percent.c
)
target_include_directories(ext2_e2p PUBLIC ${E2_INCS})
target_compile_options(ext2_e2p PRIVATE ${E2FS_C_FLAGS})

# --- libext2_quota (lib/support; soong's srcs list verbatim)
add_library(ext2_quota STATIC
    ${E2_LIB}/support/devname.c
    ${E2_LIB}/support/dict.c
    ${E2_LIB}/support/mkquota.c
    ${E2_LIB}/support/parse_qtype.c
    ${E2_LIB}/support/plausible.c
    ${E2_LIB}/support/profile.c
    ${E2_LIB}/support/profile_helpers.c
    ${E2_LIB}/support/prof_err.c
    ${E2_LIB}/support/quotaio.c
    ${E2_LIB}/support/quotaio_tree.c
    ${E2_LIB}/support/quotaio_v2.c
)
target_include_directories(ext2_quota PUBLIC ${E2_INCS})
target_link_libraries(ext2_quota PUBLIC ext2_blkid ext2_com_err)
target_compile_options(ext2_quota PRIVATE ${E2FS_C_FLAGS})

# --- libext2fs (lib/ext2fs; soong's srcs list verbatim, unix_io for linux).
# sparse_io.c compiles the REAL sparse manager because the checked-in
# lib/config.h defines ENABLE_LIBSPARSE=1 — it links our AOSP libsparse.
add_library(ext2fs STATIC
    ${E2_LIB}/ext2fs/ext2_err.c
    ${E2_LIB}/ext2fs/alloc.c
    ${E2_LIB}/ext2fs/alloc_sb.c
    ${E2_LIB}/ext2fs/alloc_stats.c
    ${E2_LIB}/ext2fs/alloc_tables.c
    ${E2_LIB}/ext2fs/atexit.c
    ${E2_LIB}/ext2fs/badblocks.c
    ${E2_LIB}/ext2fs/bb_inode.c
    ${E2_LIB}/ext2fs/bitmaps.c
    ${E2_LIB}/ext2fs/bitops.c
    ${E2_LIB}/ext2fs/blkmap64_ba.c
    ${E2_LIB}/ext2fs/blkmap64_rb.c
    ${E2_LIB}/ext2fs/blknum.c
    ${E2_LIB}/ext2fs/block.c
    ${E2_LIB}/ext2fs/bmap.c
    ${E2_LIB}/ext2fs/check_desc.c
    ${E2_LIB}/ext2fs/crc16.c
    ${E2_LIB}/ext2fs/crc32c.c
    ${E2_LIB}/ext2fs/csum.c
    ${E2_LIB}/ext2fs/closefs.c
    ${E2_LIB}/ext2fs/dblist.c
    ${E2_LIB}/ext2fs/dblist_dir.c
    ${E2_LIB}/ext2fs/digest_encode.c
    ${E2_LIB}/ext2fs/dirblock.c
    ${E2_LIB}/ext2fs/dirhash.c
    ${E2_LIB}/ext2fs/dir_iterate.c
    ${E2_LIB}/ext2fs/dupfs.c
    ${E2_LIB}/ext2fs/expanddir.c
    ${E2_LIB}/ext2fs/ext_attr.c
    ${E2_LIB}/ext2fs/extent.c
    ${E2_LIB}/ext2fs/fallocate.c
    ${E2_LIB}/ext2fs/fileio.c
    ${E2_LIB}/ext2fs/finddev.c
    ${E2_LIB}/ext2fs/flushb.c
    ${E2_LIB}/ext2fs/freefs.c
    ${E2_LIB}/ext2fs/gen_bitmap.c
    ${E2_LIB}/ext2fs/gen_bitmap64.c
    ${E2_LIB}/ext2fs/get_num_dirs.c
    ${E2_LIB}/ext2fs/get_pathname.c
    ${E2_LIB}/ext2fs/getsize.c
    ${E2_LIB}/ext2fs/getsectsize.c
    ${E2_LIB}/ext2fs/hashmap.c
    ${E2_LIB}/ext2fs/i_block.c
    ${E2_LIB}/ext2fs/icount.c
    ${E2_LIB}/ext2fs/imager.c
    ${E2_LIB}/ext2fs/ind_block.c
    ${E2_LIB}/ext2fs/initialize.c
    ${E2_LIB}/ext2fs/inline.c
    ${E2_LIB}/ext2fs/inline_data.c
    ${E2_LIB}/ext2fs/inode.c
    ${E2_LIB}/ext2fs/io_manager.c
    ${E2_LIB}/ext2fs/ismounted.c
    ${E2_LIB}/ext2fs/link.c
    ${E2_LIB}/ext2fs/llseek.c
    ${E2_LIB}/ext2fs/lookup.c
    ${E2_LIB}/ext2fs/mmp.c
    ${E2_LIB}/ext2fs/mkdir.c
    ${E2_LIB}/ext2fs/mkjournal.c
    ${E2_LIB}/ext2fs/namei.c
    ${E2_LIB}/ext2fs/native.c
    ${E2_LIB}/ext2fs/newdir.c
    ${E2_LIB}/ext2fs/nls_utf8.c
    ${E2_LIB}/ext2fs/openfs.c
    ${E2_LIB}/ext2fs/progress.c
    ${E2_LIB}/ext2fs/punch.c
    ${E2_LIB}/ext2fs/qcow2.c
    ${E2_LIB}/ext2fs/rbtree.c
    ${E2_LIB}/ext2fs/read_bb.c
    ${E2_LIB}/ext2fs/read_bb_file.c
    ${E2_LIB}/ext2fs/res_gdt.c
    ${E2_LIB}/ext2fs/rw_bitmaps.c
    ${E2_LIB}/ext2fs/sha256.c
    ${E2_LIB}/ext2fs/sha512.c
    ${E2_LIB}/ext2fs/swapfs.c
    ${E2_LIB}/ext2fs/symlink.c
    ${E2_LIB}/ext2fs/undo_io.c
    ${E2_LIB}/ext2fs/unix_io.c
    ${E2_LIB}/ext2fs/sparse_io.c
    ${E2_LIB}/ext2fs/unlink.c
    ${E2_LIB}/ext2fs/valid_blk.c
    ${E2_LIB}/ext2fs/version.c
    ${E2_LIB}/ext2fs/test_io.c
)
target_include_directories(ext2fs PUBLIC ${E2_INCS})
target_link_libraries(ext2fs PUBLIC ext2_com_err sparse_static PkgConfig::ZLIB)
target_compile_options(ext2fs PRIVATE ${E2FS_C_FLAGS})
target_compile_definitions(ext2fs PRIVATE _FILE_OFFSET_BITS=64)

# --- mke2fs binary (misc/; soong mke2fs_defaults srcs + create_inode.c)
add_executable(mke2fs
    ${E2FSPROGS}/misc/mke2fs.c
    ${E2FSPROGS}/misc/util.c
    ${E2FSPROGS}/misc/mk_hugefiles.c
    ${E2FSPROGS}/misc/default_profile.c
    ${E2FSPROGS}/misc/create_inode.c
)
target_include_directories(mke2fs PRIVATE ${E2_INCS})
target_link_libraries(mke2fs PRIVATE
    ext2fs ext2_blkid ext2_uuid ext2_quota ext2_com_err ext2_e2p
    sparse_static base_static PkgConfig::ZLIB
)
target_compile_options(mke2fs PRIVATE ${E2FS_C_FLAGS})
target_compile_definitions(mke2fs PRIVATE _FILE_OFFSET_BITS=64)
# C binary linking C++ static libs (libsparse/libbase) — link with the C++
# driver so libstdc++ (or libc++) gets pulled in.
set_target_properties(mke2fs PROPERTIES LINKER_LANGUAGE CXX)

install(TARGETS mke2fs RUNTIME DESTINATION bin)
# The Android mke2fs.conf profile fastboot points at via MKE2FS_CONFIG
# (system/extras/ext4_utils/mke2fs.conf is the prebuilt_etc soong ships
# next to the host mke2fs).
install(FILES ${EXT4UTIL}/mke2fs.conf DESTINATION bin)

# ------------------------------------------------------------ f2fs-tools
# soong "f2fs-tools-defaults" + "libf2fs_src_files" + "make_f2fs_src_files" +
# "libf2fs_fmt_host" + "make_f2fs_host_defaults". WITH_ANDROID switches
# f2fs_fs.h to include/android_config.h (the fork's checked-in host config);
# no autotools config.h involved.
set(F2FS_DEFS
    -DF2FS_MAJOR_VERSION=1
    -DF2FS_MINOR_VERSION=16
    "-DF2FS_TOOLS_DATE=\"2023-04-11\""
    "-DF2FS_TOOLS_VERSION=\"1.16.0\""
    -DWITH_ANDROID
    -DWITH_BLKDISCARD
    -D_FILE_OFFSET_BITS=64
)
set(F2FS_INCS
    ${F2FS}/include
    ${F2FS}/fsck
    ${F2FS}/mkfs
    ${LIBSPARSE}/include          # <sparse/sparse.h>
    ${E2_LIB}                     # <uuid/uuid.h> from the e2fsprogs fork
)
set(F2FS_C_FLAGS
    -Wall
    -Wno-macro-redefined
    -Wno-missing-field-initializers
    -Wno-pointer-arith
    -Wno-sign-compare
)
# soong libf2fs_src_files (the shared library; f2fs_format_main.c carries
# main() and is compiled once per executable instead) + libf2fs_io.c.
set(F2FS_LIB_SRCS
    ${F2FS}/lib/libf2fs.c
    ${F2FS}/lib/libf2fs_zoned.c
    ${F2FS}/lib/libf2fs_io.c
    ${F2FS}/lib/nls_utf8.c
    ${F2FS}/mkfs/f2fs_format.c
    ${F2FS}/mkfs/f2fs_format_utils.c
)

add_library(f2fs_fmt STATIC ${F2FS_LIB_SRCS})
target_include_directories(f2fs_fmt PUBLIC ${F2FS_INCS})
target_compile_definitions(f2fs_fmt PUBLIC ${F2FS_DEFS})
target_compile_options(f2fs_fmt PRIVATE ${F2FS_C_FLAGS})
target_link_libraries(f2fs_fmt PUBLIC sparse_static ext2_uuid PkgConfig::ZLIB)

# make_f2fs — plain (soong make_f2fs host)
add_executable(make_f2fs
    ${F2FS}/mkfs/f2fs_format_main.c
)
target_link_libraries(make_f2fs PRIVATE f2fs_fmt)
set_target_properties(make_f2fs PROPERTIES LINKER_LANGUAGE CXX)

# make_f2fs_casefold — same sources + CONF_CASEFOLD/CONF_PROJID baked in
# (soong's cc_binary_host make_f2fs_casefold; shipped alongside fastboot so
# `-O casefold` userdata images get an always-casefolding mkfs even when the
# plain binary is the one on PATH).
add_library(f2fs_fmt_casefold STATIC ${F2FS_LIB_SRCS})
target_include_directories(f2fs_fmt_casefold PUBLIC ${F2FS_INCS})
target_compile_definitions(f2fs_fmt_casefold PUBLIC ${F2FS_DEFS} -DCONF_CASEFOLD -DCONF_PROJID)
target_compile_options(f2fs_fmt_casefold PRIVATE ${F2FS_C_FLAGS})
target_link_libraries(f2fs_fmt_casefold PUBLIC sparse_static ext2_uuid PkgConfig::ZLIB)

add_executable(make_f2fs_casefold
    ${F2FS}/mkfs/f2fs_format_main.c
)
target_link_libraries(make_f2fs_casefold PRIVATE f2fs_fmt_casefold)
set_target_properties(make_f2fs_casefold PROPERTIES LINKER_LANGUAGE CXX)

install(TARGETS make_f2fs make_f2fs_casefold RUNTIME DESTINATION bin)
