#include "sparse_parser.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <vector>

namespace vmgo {

bool SparseParser::isSparseImage(const std::string& filePath) {
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) return false;

    SparseHeader header{};
    ssize_t n = read(fd, &header, sizeof(header));
    close(fd);

    if (n != sizeof(header)) return false;
    return header.magic == SPARSE_HEADER_MAGIC;
}

bool SparseParser::unsparse(const std::string& srcSparsePath, const std::string& dstRawPath, ProgressCallback progressCb) {
    int inFd = open(srcSparsePath.c_str(), O_RDONLY);
    if (inFd < 0) {
        LOGE("Failed to open source sparse image: %s (%s)", srcSparsePath.c_str(), strerror(errno));
        return false;
    }

    SparseHeader header{};
    if (read(inFd, &header, sizeof(header)) != sizeof(header)) {
        LOGE("Failed to read sparse header");
        close(inFd);
        return false;
    }

    if (header.magic != SPARSE_HEADER_MAGIC) {
        LOGE("Invalid magic bytes: 0x%X (expected 0x%X)", header.magic, SPARSE_HEADER_MAGIC);
        close(inFd);
        return false;
    }

    int outFd = open(dstRawPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outFd < 0) {
        LOGE("Failed to create destination raw image: %s (%s)", dstRawPath.c_str(), strerror(errno));
        close(inFd);
        return false;
    }

    // Set file size upfront to support sparse holes cleanly
    uint64_t totalOutputSize = static_cast<uint64_t>(header.total_blks) * header.blk_sz;
    if (totalOutputSize > 0) {
        ftruncate(outFd, totalOutputSize);
    }

    // Skip extra header bytes if any
    if (header.file_hdr_sz > sizeof(SparseHeader)) {
        lseek(inFd, header.file_hdr_sz - sizeof(SparseHeader), SEEK_CUR);
    }

    std::vector<uint8_t> buffer(64 * 1024); // 64KB copy buffer
    uint32_t totalBlocksProcessed = 0;

    for (uint32_t chunk = 0; chunk < header.total_chunks; ++chunk) {
        ChunkHeader chunkHdr{};
        if (read(inFd, &chunkHdr, sizeof(chunkHdr)) != sizeof(chunkHdr)) {
            LOGE("Failed to read chunk header at chunk %u", chunk);
            close(inFd);
            close(outFd);
            return false;
        }

        // Skip extra chunk header bytes if any
        if (header.chunk_hdr_sz > sizeof(ChunkHeader)) {
            lseek(inFd, header.chunk_hdr_sz - sizeof(ChunkHeader), SEEK_CUR);
        }

        uint64_t chunkBytes = static_cast<uint64_t>(chunkHdr.chunk_sz) * header.blk_sz;

        switch (chunkHdr.chunk_type) {
            case CHUNK_TYPE_RAW: {
                uint64_t remaining = chunkBytes;
                while (remaining > 0) {
                    size_t toRead = std::min(remaining, static_cast<uint64_t>(buffer.size()));
                    ssize_t nRead = read(inFd, buffer.data(), toRead);
                    if (nRead <= 0) break;
                    write(outFd, buffer.data(), nRead);
                    remaining -= nRead;
                }
                break;
            }
            case CHUNK_TYPE_FILL: {
                uint32_t fillVal = 0;
                read(inFd, &fillVal, sizeof(fillVal));
                std::vector<uint32_t> fillBuffer(buffer.size() / sizeof(uint32_t), fillVal);
                uint64_t remaining = chunkBytes;
                while (remaining > 0) {
                    size_t toWrite = std::min(remaining, static_cast<uint64_t>(buffer.size()));
                    write(outFd, fillBuffer.data(), toWrite);
                    remaining -= toWrite;
                }
                break;
            }
            case CHUNK_TYPE_DONT_CARE: {
                lseek(outFd, chunkBytes, SEEK_CUR);
                break;
            }
            case CHUNK_TYPE_CRC32: {
                uint32_t crc = 0;
                read(inFd, &crc, sizeof(crc));
                break;
            }
            default:
                LOGW("Unknown chunk type: 0x%X at chunk %u", chunkHdr.chunk_type, chunk);
                break;
        }

        totalBlocksProcessed += chunkHdr.chunk_sz;
        if (progressCb && header.total_blks > 0) {
            int percent = static_cast<int>((static_cast<uint64_t>(totalBlocksProcessed) * 100) / header.total_blks);
            progressCb(percent, "Extracting GSI filesystem...");
        }
    }

    close(inFd);
    close(outFd);
    LOGI("Successfully converted sparse image to raw image: %s", dstRawPath.c_str());
    return true;
}

} // namespace vmgo
