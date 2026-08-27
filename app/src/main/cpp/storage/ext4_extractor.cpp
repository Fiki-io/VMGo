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
    uint16_t s_magic; // 0xEF53
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
    // 64-bit fields if desc_size >= 64
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
    uint16_t eh_magic;      // 0xF30A
    uint16_t eh_entries;    // Number of valid entries
    uint16_t eh_max;        // Capacity of entries
    uint16_t eh_depth;      // 0 for leaf, >0 for index
    uint32_t eh_generation;
};

struct Ext4Extent {
    uint32_t ee_block;      // First logical block extent covers
    uint16_t ee_len;        // Number of blocks covered
    uint16_t ee_start_hi;   // High 16 bits of physical block
    uint32_t ee_start_lo;   // Low 32 bits of physical block
};

struct Ext4ExtentIdx {
    uint32_t ei_block;      // Index covers logical blocks from this block
    uint32_t ei_leaf_lo;    // Low 32 bits of physical block of next level
    uint16_t ei_leaf_hi;    // High 16 bits of physical block of next level
    uint16_t ei_unused;
};

struct Ext4Inode {
    uint16_t i_mode;        // File mode
    uint16_t i_uid;         // Low 16 bits of Owner Uid
    uint32_t i_size_lo;     // Size in bytes (low 32 bits)
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;         // Low 16 bits of Group Id
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;       // 0x80000 = Extents used
    uint32_t i_osd1;
    uint8_t  i_block[60];   // Extents tree or direct blocks
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
    uint8_t  i_osd2[24];
    uint16_t i_extra_isize;
    uint16_t i_checksum_hi;
    uint32_t i_ctime_extra;
    uint32_t i_mtime_extra;
    uint32_t i_atime_extra;
    uint32_t i_crtime;
    uint32_t i_crtime_extra;
    uint32_t i_version_hi;
};

struct Ext4DirEntry2 {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type; // 1: file, 2: dir, 7: symlink
    char     name[255];
};

#pragma pack(pop)

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

static void readExtents(int fd, uint64_t blockSize, const uint8_t* extentData, std::vector<std::pair<uint64_t, uint32_t>>& blockList) {
    const auto* header = reinterpret_cast<const Ext4ExtentHeader*>(extentData);
    if (header->eh_magic != 0xF30A) return;

    if (header->eh_depth == 0) {
        // Leaf node
        const auto* extents = reinterpret_cast<const Ext4Extent*>(extentData + sizeof(Ext4ExtentHeader));
        for (int i = 0; i < header->eh_entries; ++i) {
            uint64_t physBlock = (static_cast<uint64_t>(extents[i].ee_start_hi) << 32) | extents[i].ee_start_lo;
            uint32_t count = (extents[i].ee_len > 32768) ? (extents[i].ee_len - 32768) : extents[i].ee_len;
            blockList.emplace_back(physBlock, count);
        }
    } else {
        // Index node -> recurse
        const auto* indices = reinterpret_cast<const Ext4ExtentIdx*>(extentData + sizeof(Ext4ExtentHeader));
        std::vector<uint8_t> childBuf(blockSize);
        for (int i = 0; i < header->eh_entries; ++i) {
            uint64_t leafBlock = (static_cast<uint64_t>(indices[i].ei_leaf_hi) << 32) | indices[i].ei_leaf_lo;
            if (pread(fd, childBuf.data(), blockSize, leafBlock * blockSize) == static_cast<ssize_t>(blockSize)) {
                readExtents(fd, blockSize, childBuf.data(), blockList);
            }
        }
    }
}

static bool readInodeData(int fd, uint64_t blockSize, const Ext4Inode& inode, std::vector<uint8_t>& data) {
    uint64_t fileSize = (static_cast<uint64_t>(inode.i_size_high) << 32) | inode.i_size_lo;
    data.resize(fileSize);
    if (fileSize == 0) return true;

    if (inode.i_flags & 0x80000) {
        // Uses extents
        std::vector<std::pair<uint64_t, uint32_t>> blockList;
        readExtents(fd, blockSize, inode.i_block, blockList);

        uint64_t bytesReadTotal = 0;
        for (const auto& extent : blockList) {
            uint64_t physBlock = extent.first;
            uint32_t blockCount = extent.second;
            uint64_t bytesToRead = std::min(static_cast<uint64_t>(blockCount) * blockSize, fileSize - bytesReadTotal);

            if (pread(fd, data.data() + bytesReadTotal, bytesToRead, physBlock * blockSize) > 0) {
                bytesReadTotal += bytesToRead;
            }
            if (bytesReadTotal >= fileSize) break;
        }
        return bytesReadTotal >= fileSize;
    }
    return false;
}

static bool extractDirectory(
    int fd,
    uint64_t blockSize,
    uint16_t inodeSize,
    uint32_t inodesPerGroup,
    const std::vector<Ext4GroupDesc>& gdt,
    uint32_t inodeNum,
    const std::string& outPath,
    int depth
) {
    if (depth > 20) return false;
    mkdir(outPath.c_str(), 0755);

    // Read Inode
    uint32_t group = (inodeNum - 1) / inodesPerGroup;
    uint32_t index = (inodeNum - 1) % inodesPerGroup;
    if (group >= gdt.size()) return false;

    uint64_t itableBlock = (static_cast<uint64_t>(gdt[group].bg_inode_table_hi) << 32) | gdt[group].bg_inode_table_lo;
    uint64_t inodeOffset = itableBlock * blockSize + index * inodeSize;

    Ext4Inode inode{};
    if (pread(fd, &inode, sizeof(inode), inodeOffset) < static_cast<ssize_t>(sizeof(inode))) {
        return false;
    }

    std::vector<uint8_t> dirData;
    if (!readInodeData(fd, blockSize, inode, dirData)) {
        return false;
    }

    size_t offset = 0;
    while (offset < dirData.size()) {
        const auto* entry = reinterpret_cast<const Ext4DirEntry2*>(dirData.data() + offset);
        if (entry->rec_len == 0 || offset + entry->rec_len > dirData.size()) break;

        if (entry->inode != 0 && entry->name_len > 0) {
            std::string name(entry->name, entry->name_len);
            if (name != "." && name != "..") {
                std::string childPath = outPath + "/" + name;

                if (entry->file_type == 2) {
                    // Directory
                    extractDirectory(fd, blockSize, inodeSize, inodesPerGroup, gdt, entry->inode, childPath, depth + 1);
                } else if (entry->file_type == 1 || entry->file_type == 7) {
                    // Regular file or symlink
                    uint32_t cGroup = (entry->inode - 1) / inodesPerGroup;
                    uint32_t cIndex = (entry->inode - 1) % inodesPerGroup;
                    if (cGroup < gdt.size()) {
                        uint64_t cItable = (static_cast<uint64_t>(gdt[cGroup].bg_inode_table_hi) << 32) | gdt[cGroup].bg_inode_table_lo;
                        uint64_t cOffset = cItable * blockSize + cIndex * inodeSize;

                        Ext4Inode cInode{};
                        if (pread(fd, &cInode, sizeof(cInode), cOffset) >= static_cast<ssize_t>(sizeof(cInode))) {
                            std::vector<uint8_t> fileContent;
                            if (readInodeData(fd, blockSize, cInode, fileContent)) {
                                if (entry->file_type == 7) {
                                    // Symlink
                                    std::string target(reinterpret_cast<char*>(fileContent.data()), fileContent.size());
                                    unlink(childPath.c_str());
                                    symlink(target.c_str(), childPath.c_str());
                                } else {
                                    // File
                                    int outFd = open(childPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, (cInode.i_mode & 0777));
                                    if (outFd >= 0) {
                                        if (!fileContent.empty()) {
                                            write(outFd, fileContent.data(), fileContent.size());
                                        }
                                        close(outFd);
                                        if (childPath.find("/bin/") != std::string::npos || childPath.find(".so") != std::string::npos) {
                                            chmod(childPath.c_str(), 0755);
                                        }
                                    }
                                }
                            }
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
    LOGI("Extracting ext4 filesystem from: %s -> %s", imagePath.c_str(), targetDir.c_str());
    int fd = open(imagePath.c_str(), O_RDONLY);
    if (fd < 0) {
        LOGE("Failed to open image file: %s", imagePath.c_str());
        return false;
    }

    Ext4Superblock sb{};
    if (pread(fd, &sb, sizeof(sb), 1024) != sizeof(sb) || sb.s_magic != 0xEF53) {
        LOGE("Invalid ext4 superblock in %s", imagePath.c_str());
        close(fd);
        return false;
    }

    uint64_t blockSize = 1024ULL << sb.s_log_block_size;
    uint32_t groupCount = (sb.s_blocks_count_lo + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
    uint16_t descSize = (sb.s_desc_size > sizeof(Ext4GroupDesc)) ? sb.s_desc_size : sizeof(Ext4GroupDesc);

    uint64_t gdtOffset = (blockSize == 1024) ? 2048 : blockSize;
    std::vector<Ext4GroupDesc> gdt(groupCount);
    for (uint32_t i = 0; i < groupCount; ++i) {
        pread(fd, &gdt[i], sizeof(Ext4GroupDesc), gdtOffset + i * descSize);
    }

    if (onProgress) onProgress(20, "Extracting ext4 directory tree...");

    // Inode 2 is Root Directory
    mkdir(targetDir.c_str(), 0755);
    bool success = extractDirectory(
        fd,
        blockSize,
        sb.s_inode_size,
        sb.s_inodes_per_group,
        gdt,
        2, // Root inode
        targetDir,
        0
    );

    close(fd);

    if (success) {
        LOGI("Ext4 extraction completed successfully to: %s", targetDir.c_str());
        if (onProgress) onProgress(100, "Ext4 extracted successfully!");
    } else {
        LOGE("Ext4 extraction encountered errors");
    }

    return success;
}

} // namespace vmgo
