#ifndef SPARSE_PARSER_H
#define SPARSE_PARSER_H

#include "../include/vm_types.h"
#include <string>
#include <functional>

namespace vmgo {

using ProgressCallback = std::function<void(int percent, const std::string& status)>;

// Android Sparse Image Header (magic 0xED26FF3A)
#define SPARSE_HEADER_MAGIC 0xED26FF3A

#define CHUNK_TYPE_RAW       0xCAC1
#define CHUNK_TYPE_FILL      0xCAC2
#define CHUNK_TYPE_DONT_CARE 0xCAC3
#define CHUNK_TYPE_CRC32     0xCAC4

struct SparseHeader {
    uint32_t magic;          // 0xed26ff3a
    uint16_t major_version;  // (0x1) - reject images with higher major versions
    uint16_t minor_version;  // (0x0) - allow images with higer minor versions
    uint16_t file_hdr_sz;    // 28 bytes for first revision of the format
    uint16_t chunk_hdr_sz;   // 12 bytes for first revision of the format
    uint32_t blk_sz;         // block size in bytes, must be a multiple of 4 (4096 default)
    uint32_t total_blks;     // total blocks in the non-sparse output image
    uint32_t total_chunks;   // total chunks in the sparse input image
    uint32_t image_checksum; // CRC32 checksum of the original data, counting "don't care" as 0
};

struct ChunkHeader {
    uint16_t chunk_type;     // 0xCAC1 -> raw; 0xCAC2 -> fill; 0xCAC3 -> don't care
    uint16_t reserved1;
    uint32_t chunk_sz;       // in blocks in output image
    uint32_t total_sz;       // in bytes of chunk input file including chunk header and data
};

class SparseParser {
public:
    static bool isSparseImage(const std::string& filePath);
    static bool unsparse(const std::string& srcSparsePath, const std::string& dstRawPath, ProgressCallback progressCb = nullptr);
};

} // namespace vmgo

#endif // SPARSE_PARSER_H
