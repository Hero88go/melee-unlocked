# Dolphin RVZ/WIA disc image reader (port/app/disc_rvz.cpp) and the decoders it uses: Zstandard,
# LZMA/LZMA2 and bzip2, decompression only; and .slpz replays (replay_slpz.cpp, zstd both ways). Included by the Windows and Linux builds; the
# launchers link port_disc_rvz.
if(NOT TARGET port_disc_rvz)
  enable_language(C)
  set(DISC_RVZ_APP "${CMAKE_CURRENT_LIST_DIR}")
  set(DISC_RVZ_TP "${CMAKE_CURRENT_LIST_DIR}/../third_party")
  file(GLOB DISC_RVZ_ZSTD "${DISC_RVZ_TP}/zstd/common/*.c" "${DISC_RVZ_TP}/zstd/decompress/*.c" "${DISC_RVZ_TP}/zstd/compress/*.c")
  add_library(port_disc_rvz STATIC "${DISC_RVZ_APP}/disc_rvz.cpp" "${DISC_RVZ_APP}/replay_slpz.cpp" ${DISC_RVZ_ZSTD}
    "${DISC_RVZ_TP}/lzma/LzmaDec.c" "${DISC_RVZ_TP}/lzma/Lzma2Dec.c"
    "${DISC_RVZ_TP}/bzip2/blocksort.c" "${DISC_RVZ_TP}/bzip2/huffman.c" "${DISC_RVZ_TP}/bzip2/crctable.c"
    "${DISC_RVZ_TP}/bzip2/randtable.c" "${DISC_RVZ_TP}/bzip2/compress.c" "${DISC_RVZ_TP}/bzip2/decompress.c"
    "${DISC_RVZ_TP}/bzip2/bzlib.c")
  target_include_directories(port_disc_rvz PUBLIC "${DISC_RVZ_APP}" PRIVATE "${DISC_RVZ_TP}")
  target_compile_features(port_disc_rvz PRIVATE cxx_std_17)
  # No assembly (zstd's Huffman fast path is a .S file), bzlib linked in, zstd's xxhash kept private.
  target_compile_definitions(port_disc_rvz PRIVATE ZSTD_DISABLE_ASM=1 XXH_NAMESPACE=ZSTD_ BZ_EXPORT
    _CRT_SECURE_NO_WARNINGS NOMINMAX)
  if(MSVC)
    target_compile_options(port_disc_rvz PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc> $<$<COMPILE_LANGUAGE:C>:/W1>)
  else()
    set_property(TARGET port_disc_rvz PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_compile_options(port_disc_rvz PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)
  endif()
endif()
