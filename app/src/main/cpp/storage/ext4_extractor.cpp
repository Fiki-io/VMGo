#include "ext4_extractor.h"
#include "../include/vm_types.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <vector>
#include <string>
#include <algorithm>

namespace vmgo {

#pragma pack(push, 1)

struct Ext4Superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
};

struct Ext4GroupDesc {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
    uint16_t bg_free_blocks_count_hi;
    uint16_t bg_free_inodes_count_hi;
    uint16_t bg_used_dirs_count_hi;
    uint16_t bg_itable_unused_hi;
    uint32_t bg_exclude_bitmap_hi;
    uint16_t bg_block_bitmap_csum_hi;
    uint16_t bg_inode_bitmap_csum_hi;
    uint32_t bg_reserved;
};

struct Ext4ExtentHeader {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
};

struct Ext4Extent {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
};

struct Ext4ExtentIdx {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
};

struct Ext4Inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint8_t  i_block[60];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
    uint8_t  i_osd2[12];
    uint16_t i_extra_isize;
    uint16_t i_checksum_hi;
    uint32_t i_ctime_extra;
    uint32_t i_mtime_extra;
    uint32_t i_atime_extra;
    uint32_t i_crtime;
    uint32_t i_crtime_extra;
    uint32_t i_version_hi;
    uint32_t i_projid;
};

struct Ext4DirEntry2 {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
};

#pragma pack(pop)

// Mode bits
#define EXT4_S_IFMT   0xF000
#define EXT4_S_IFLNK  0xA000
#define EXT4_S_IFREG  0x8000
#define EXT4_S_IFDIR  0x4000

// Inode flags
#define EXT4_EXTENTS_FL 0x00080000
#define EXT4_INLINE_DATA_FL 0x10000000

// File types in dir entry
#define EXT4_FT_REG_FILE 1
#define EXT4_FT_DIR      2
#define EXT4_FT_SYMLINK  7

static int extractedFiles = 0;
static int extractedDirs = 0;
static int extractedSymlinks = 0;
static int failedFiles = 0;

bool Ext4Extractor::isExt4Image(const std::string& imagePath) {
    int fd = open(imagePath.c_str(), O_RDONLY);
    if (fd < 0) return false;

    Ext4Superblock sb{};
    if (pread(fd, &sb, sizeof(sb), 1024) != sizeof(sb)) {
        close(fd);
        return false;
    }
    close(fd);
    return sb.s_magic == 0xEF53;
}

static bool readExtentBlocks(int fd, uint64_t blockSize, const uint8_t* extentData,
                             std::vector<std::pair<uint64_t, uint32_t>>& blockList, int depth) {
    if (depth > 5) return false;
    const auto* header = reinterpret_cast<const Ext4ExtentHeader*>(extentData);
    if (header->eh_magic != 0xF30A) {
        LOGE("Invalid extent magic: 0x%04X", header->eh_magic);
        return false;
    }

    if (header->eh_depth == 0) {
        const auto* extents = reinterpret_cast<const Ext4Extent*>(extentData + sizeof(Ext4ExtentHeader));
        for (int i = 0; i < header->eh_entries; ++i) {
            uint64_t physBlock = (static_cast<uint64_t>(extents[i].ee_start_hi) << 32) | extents[i].ee_start_lo;
            uint32_t count = extents[i].ee_len;
            if (count > 32768) count -= 32768; // uninitialized extent
            if (physBlock > 0 && count > 0) {
                blockList.emplace_back(physBlock, count);
            }
        }
    } else {
        const auto* indices = reinterpret_cast<const Ext4ExtentIdx*>(extentData + sizeof(Ext4ExtentHeader));
        std::vector<uint8_t> childBuf(blockSize);
        for (int i = 0; i < header->eh_entries; ++i) {
            uint64_t leafBlock = (static_cast<uint64_t>(indices[i].ei_leaf_hi) << 32) | indices[i].ei_leaf_lo;
            ssize_t rd = pread(fd, childBuf.data(), blockSize, leafBlock * blockSize);
            if (rd == static_cast<ssize_t>(blockSize)) {
                readExtentBlocks(fd, blockSize, childBuf.data(), blockList, depth + 1);
            }
        }
    }
    return true;
}

static bool readInodeData(int fd, uint64_t blockSize, const Ext4Inode& inode,
                          std::vector<uint8_t>& data) {
    uint64_t fileSize = (static_cast<uint64_t>(inode.i_size_high) << 32) | inode.i_size_lo;
    data.resize(fileSize);
    if (fileSize == 0) return true;

    // Check for inline data (small symlinks store target in i_block)
    if ((inode.i_flags & EXT4_INLINE_DATA_FL) || 
        (((inode.i_mode & EXT4_S_IFMT) == EXT4_S_IFLNK) && fileSize <= 60 && !(inode.i_flags & EXT4_EXTENTS_FL))) {
        // Inline data stored in i_block
        size_t copyLen = std::min(fileSize, static_cast<uint64_t>(60));
        memcpy(data.data(), inode.i_block, copyLen);
        return true;
    }

    if (inode.i_flags & EXT4_EXTENTS_FL) {
        std::vector<std::pair<uint64_t, uint32_t>> blockList;
        if (!readExtentBlocks(fd, blockSize, inode.i_block, blockList, 0)) {
            return false;
        }

        uint64_t bytesReadTotal = 0;
        for (const auto& extent : blockList) {
            uint64_t physBlock = extent.first;
            uint32_t blockCount = extent.second;
            for (uint32_t b = 0; b < blockCount && bytesReadTotal < fileSize; ++b) {
                uint64_t toRead = std::min(blockSize, fileSize - bytesReadTotal);
                ssize_t rd = pread(fd, data.data() + bytesReadTotal, toRead, (physBlock + b) * blockSize);
                if (rd <= 0) break;
                bytesReadTotal += rd;
            }
            if (bytesReadTotal >= fileSize) break;
        }
        return bytesReadTotal >= fileSize;
    } else {
        // Direct/indirect blocks (legacy, non-extent)
        // i_block[0..11] = direct block pointers
        // i_block[12] = single indirect
        // i_block[13] = double indirect
        // i_block[14] = triple indirect
        const uint32_t* blockPtrs = reinterpret_cast<const uint32_t*>(inode.i_block);
        uint64_t bytesRead = 0;
        uint32_t ptrsPerBlock = blockSize / 4;

        // Direct blocks (0-11)
        for (int i = 0; i < 12 && bytesRead < fileSize; ++i) {
            if (blockPtrs[i] == 0) continue;
            uint64_t toRead = std::min(blockSize, fileSize - bytesRead);
            ssize_t rd = pread(fd, data.data() + bytesRead, toRead, static_cast<uint64_t>(blockPtrs[i]) * blockSize);
            if (rd > 0) bytesRead += rd;
        }

        // Single indirect (12)
        if (bytesRead < fileSize && blockPtrs[12] != 0) {
            std::vector<uint32_t> indBlock(ptrsPerBlock);
            pread(fd, indBlock.data(), blockSize, static_cast<uint64_t>(blockPtrs[12]) * blockSize);
            for (uint32_t i = 0; i < ptrsPerBlock && bytesRead < fileSize; ++i) {
                if (indBlock[i] == 0) continue;
                uint64_t toRead = std::min(blockSize, fileSize - bytesRead);
                ssize_t rd = pread(fd, data.data() + bytesRead, toRead, static_cast<uint64_t>(indBlock[i]) * blockSize);
                if (rd > 0) bytesRead += rd;
            }
        }

        // Double indirect (13)
        if (bytesRead < fileSize && blockPtrs[13] != 0) {
            std::vector<uint32_t> dIndBlock(ptrsPerBlock);
            pread(fd, dIndBlock.data(), blockSize, static_cast<uint64_t>(blockPtrs[13]) * blockSize);
            for (uint32_t i = 0; i < ptrsPerBlock && bytesRead < fileSize; ++i) {
                if (dIndBlock[i] == 0) continue;
                std::vector<uint32_t> indBlock(ptrsPerBlock);
                pread(fd, indBlock.data(), blockSize, static_cast<uint64_t>(dIndBlock[i]) * blockSize);
                for (uint32_t j = 0; j < ptrsPerBlock && bytesRead < fileSize; ++j) {
                    if (indBlock[j] == 0) continue;
                    uint64_t toRead = std::min(blockSize, fileSize - bytesRead);
                    ssize_t rd = pread(fd, data.data() + bytesRead, toRead, static_cast<uint64_t>(indBlock[j]) * blockSize);
                    if (rd > 0) bytesRead += rd;
                }
            }
        }

        return bytesRead >= fileSize;
    }
}

static bool readInode(int fd, uint64_t blockSize, uint16_t inodeSize, uint32_t inodesPerGroup,
                      const std::vector<Ext4GroupDesc>& gdt, uint32_t inodeNum, Ext4Inode& inode) {
    if (inodeNum == 0) return false;
    uint32_t group = (inodeNum - 1) / inodesPerGroup;
    uint32_t index = (inodeNum - 1) % inodesPerGroup;
    if (group >= gdt.size()) return false;

    uint64_t itableBlock = (static_cast<uint64_t>(gdt[group].bg_inode_table_hi) << 32) | gdt[group].bg_inode_table_lo;
    uint64_t inodeOffset = itableBlock * blockSize + static_cast<uint64_t>(index) * inodeSize;

    memset(&inode, 0, sizeof(inode));
    ssize_t rd = pread(fd, &inode, std::min(static_cast<size_t>(inodeSize), sizeof(Ext4Inode)), inodeOffset);
    return rd >= 128; // minimum inode read
}

static void mkdirRecursive(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if (path[i] == '/' || i == path.size() - 1) {
            mkdir(current.c_str(), 0755);
        }
    }
}

static bool extractDirectory(
    int fd, uint64_t blockSize, uint16_t inodeSize, uint32_t inodesPerGroup,
    const std::vector<Ext4GroupDesc>& gdt, uint32_t inodeNum,
    const std::string& outPath, int depth
) {
    if (depth > 20) return false;
    mkdirRecursive(outPath);
    extractedDirs++;

    Ext4Inode inode{};
    if (!readInode(fd, blockSize, inodeSize, inodesPerGroup, gdt, inodeNum, inode)) {
        LOGE("Failed to read inode %u for dir %s", inodeNum, outPath.c_str());
        return false;
    }

    std::vector<uint8_t> dirData;
    if (!readInodeData(fd, blockSize, inode, dirData)) {
        LOGE("Failed to read dir data for inode %u at %s", inodeNum, outPath.c_str());
        return false;
    }

    size_t offset = 0;
    while (offset + 8 < dirData.size()) {
        const auto* entry = reinterpret_cast<const Ext4DirEntry2*>(dirData.data() + offset);
        if (entry->rec_len == 0 || entry->rec_len < 8) break;
        if (offset + entry->rec_len > dirData.size()) break;

        if (entry->inode != 0 && entry->name_len > 0) {
            std::string name(entry->name, entry->name_len);
            if (name != "." && name != "..") {
                std::string childPath = outPath + "/" + name;

                Ext4Inode childInode{};
                if (!readInode(fd, blockSize, inodeSize, inodesPerGroup, gdt, entry->inode, childInode)) {
                    failedFiles++;
                    offset += entry->rec_len;
                    continue;
                }

                uint16_t mode = childInode.i_mode & EXT4_S_IFMT;

                if (mode == EXT4_S_IFDIR) {
                    extractDirectory(fd, blockSize, inodeSize, inodesPerGroup, gdt,
                                     entry->inode, childPath, depth + 1);
                } else if (mode == EXT4_S_IFLNK) {
                    // Symlink
                    std::vector<uint8_t> linkData;
                    if (readInodeData(fd, blockSize, childInode, linkData)) {
                        std::string target(reinterpret_cast<char*>(linkData.data()), linkData.size());
                        // Remove null terminators
                        while (!target.empty() && target.back() == '\0') target.pop_back();
                        unlink(childPath.c_str());
                        if (symlink(target.c_str(), childPath.c_str()) == 0) {
                            extractedSymlinks++;
                        } else {
                            failedFiles++;
                        }
                    }
                } else if (mode == EXT4_S_IFREG) {
                    // Regular file
                    uint64_t fileSize = (static_cast<uint64_t>(childInode.i_size_high) << 32) | childInode.i_size_lo;
                    
                    if (fileSize == 0) {
                        // Create empty file
                        int outFd = open(childPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, childInode.i_mode & 0777);
                        if (outFd >= 0) close(outFd);
                        extractedFiles++;
                    } else if (fileSize <= 64 * 1024 * 1024) { // 64MB limit per file to avoid OOM
                        std::vector<uint8_t> fileContent;
                        if (readInodeData(fd, blockSize, childInode, fileContent)) {
                            int outFd = open(childPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, childInode.i_mode & 0777);
                            if (outFd >= 0) {
                                size_t written = 0;
                                while (written < fileContent.size()) {
                                    size_t chunk = std::min(fileContent.size() - written, static_cast<size_t>(256 * 1024));
                                    ssize_t w = write(outFd, fileContent.data() + written, chunk);
                                    if (w <= 0) break;
                                    written += w;
                                }
                                close(outFd);
                                // Set executable for binaries
                                if (childPath.find("/bin/") != std::string::npos ||
                                    childPath.find("/xbin/") != std::string::npos ||
                                    childPath.find(".so") != std::string::npos) {
                                    chmod(childPath.c_str(), 0755);
                                }
                                extractedFiles++;
                            } else {
                                failedFiles++;
                            }
                        } else {
                            failedFiles++;
                        }
                    } else {
                        // Large file: stream to disk block by block
                        int outFd = open(childPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, childInode.i_mode & 0777);
                        if (outFd >= 0) {
                            if (childInode.i_flags & EXT4_EXTENTS_FL) {
                                std::vector<std::pair<uint64_t, uint32_t>> blockList;
                                readExtentBlocks(fd, blockSize, childInode.i_block, blockList, 0);
                                uint64_t bytesWritten = 0;
                                std::vector<uint8_t> buf(blockSize);
                                for (const auto& ext : blockList) {
                                    for (uint32_t b = 0; b < ext.second && bytesWritten < fileSize; ++b) {
                                        uint64_t toRead = std::min(blockSize, fileSize - bytesWritten);
                                        ssize_t rd = pread(fd, buf.data(), toRead, (ext.first + b) * blockSize);
                                        if (rd > 0) {
                                            write(outFd, buf.data(), rd);
                                            bytesWritten += rd;
                                        }
                                    }
                                }
                            }
                            close(outFd);
                            if (childPath.find("/bin/") != std::string::npos ||
                                childPath.find(".so") != std::string::npos) {
                                chmod(childPath.c_str(), 0755);
                            }
                            extractedFiles++;
                        } else {
                            failedFiles++;
                        }
                    }
                }
            }
        }
        offset += entry->rec_len;
    }
    return true;
}

bool Ext4Extractor::extractExt4Image(
    const std::string& imagePath,
    const std::string& targetDir,
    ProgressCallback onProgress
) {
    LOGI("Ext4Extractor: Opening image: %s", imagePath.c_str());
    extractedFiles = 0;
    extractedDirs = 0;
    extractedSymlinks = 0;
    failedFiles = 0;

    int fd = open(imagePath.c_str(), O_RDONLY);
    if (fd < 0) {
        LOGE("Ext4Extractor: Failed to open image: %s (errno=%d)", imagePath.c_str(), errno);
        return false;
    }

    // Read superblock at offset 1024
    Ext4Superblock sb{};
    if (pread(fd, &sb, sizeof(sb), 1024) != sizeof(sb)) {
        LOGE("Ext4Extractor: Failed to read superblock");
        close(fd);
        return false;
    }

    if (sb.s_magic != 0xEF53) {
        LOGE("Ext4Extractor: Invalid superblock magic: 0x%04X (expected 0xEF53)", sb.s_magic);
        close(fd);
        return false;
    }

    uint64_t blockSize = 1024ULL << sb.s_log_block_size;
    uint32_t groupCount = (sb.s_blocks_count_lo + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
    uint16_t descSize = sb.s_desc_size;
    if (descSize < 32) descSize = 32;

    LOGI("Ext4Extractor: Superblock valid. BlockSize=%lu, Groups=%u, InodeSize=%u, InodesPerGroup=%u",
         (unsigned long)blockSize, groupCount, sb.s_inode_size, sb.s_inodes_per_group);

    if (onProgress) onProgress(10, "Reading group descriptors...");

    // Read Group Descriptor Table
    uint64_t gdtOffset = (blockSize == 1024) ? 2048 : blockSize;
    std::vector<Ext4GroupDesc> gdt(groupCount);
    for (uint32_t i = 0; i < groupCount; ++i) {
        memset(&gdt[i], 0, sizeof(Ext4GroupDesc));
        pread(fd, &gdt[i], std::min(static_cast<size_t>(descSize), sizeof(Ext4GroupDesc)),
              gdtOffset + static_cast<uint64_t>(i) * descSize);
    }

    LOGI("Ext4Extractor: Read %u group descriptors", groupCount);
    if (onProgress) onProgress(20, "Extracting root directory tree...");

    // Create target dir
    mkdirRecursive(targetDir);

    // Extract from inode 2 (root directory)
    bool success = extractDirectory(
        fd, blockSize, sb.s_inode_size, sb.s_inodes_per_group,
        gdt, 2, targetDir, 0
    );

    close(fd);

    LOGI("Ext4Extractor: Extraction %s. Files=%d, Dirs=%d, Symlinks=%d, Failed=%d",
         success ? "completed" : "had errors",
         extractedFiles, extractedDirs, extractedSymlinks, failedFiles);

    if (onProgress) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Extracted %d files, %d dirs, %d symlinks (%d failed)",
                 extractedFiles, extractedDirs, extractedSymlinks, failedFiles);
        onProgress(100, msg);
    }

    return (extractedFiles + extractedDirs) > 0;
}

} // namespace vmgo
