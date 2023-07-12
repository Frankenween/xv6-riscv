#include "bits/stdc++.h"
#include "../kernel/fs/fs.h"
#include "../kernel/param.h"

using std::string;
using std::cout;
using std::endl;

constexpr int INODE_NUMBER = 200;
constexpr int LOG_BLOCKS = LOGSIZE;
constexpr int INODES_PER_BLOCK = BSIZE / sizeof(dinode);

ushort riskv_short(ushort x) {
  ushort y;
  auto *a = (unsigned char*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint riskv_int(uint x) {
  uint y;
  auto *a = (unsigned char*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

template<class T>
T div_up(T x, T y) {
  return (x + y - 1) / y;
}

struct fs_builder {
  // Disk layout:
  // [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

  fs_builder(const string& output_name,
             uint data_blocks,
             uint inodes,
             uint log_blocks):
                 image_file(output_name,
                            std::ios::in | std::ios::out |
                            std::ios::trunc | std::ios::binary),
                 blocks(data_blocks),
                 inodes(inodes),
                 log_blocks(log_blocks) {
    if (!image_file.good()) {
      throw std::runtime_error("Couldn't open file \"" + output_name + "\"");
    }
    bitmap_size = div_up(data_blocks, BSIZE * 8u);
    inode_blocks = div_up(inodes, (uint)INODES_PER_BLOCK);
    metadata_blocks = 1 + 1 + log_blocks + inode_blocks + bitmap_size;
    free_block = metadata_blocks;

    init_file();
    init_superblock();
    print_fs_layout();
    make_root_inode();
  }

  void add_file(std::ifstream& file, const string& name, uint location) {
    uint inode = alloc_inode(T_FILE);
    dirent file_dentry{};
    memset(&file_dentry, 0, sizeof(dirent));

    file_dentry.inum = riskv_short(inode);
    if (name.size() >= DIRSIZ) {
      std::cerr << "Filename is too long: \"" << name << "\"" << endl;
    }
    strncpy(file_dentry.name, name.c_str(), DIRSIZ);
    append_to_inode(location, &file_dentry, sizeof(dirent));

    std::array<char, BSIZE> buffer;
    while (file.good()) {
      buffer.fill(0);
      file.read(buffer.data(), BSIZE);
      append_to_inode(inode, buffer.data(), file.gcount());
    }
    if (!file.eof()) {
      throw std::runtime_error("IO error while reading from file into " + name);
    }
  }

  void complete_fs() {
    mark_blocks_allocated(free_block);
    image_file.close();
  }

private:
  std::fstream image_file;
  superblock sb{};
  uint blocks;
  uint inodes;
  uint log_blocks;
  uint bitmap_size;
  uint inode_blocks;
  uint metadata_blocks;

  uint free_inode_index = 1;
  uint root_inode = -1;
  uint free_block = -1;

  static uint block_pos(uint i) {
    return i * BSIZE;
  }

  template <class T>
  void write_struct(const T& data, uint position) {
    image_file.seekp(position);
    image_file.write(reinterpret_cast<const char *>(&data), sizeof(T));
    if (!image_file.good()) {
      throw std::runtime_error(string("Failed writing struct ") +
                               typeid(T).name() +
                               " at position " +
                               std::to_string(position));
    }
  }

  template <class T>
  T read_struct(uint position) {
    T data;
    memset(&data, 0, sizeof(T));
    image_file.seekg(position);
    image_file.read(reinterpret_cast<char*>(&data), sizeof(T));
    if (!image_file.good()) {
      throw std::runtime_error(string("Failed reading struct ") +
                               typeid(T).name() +
                               " at position " +
                               std::to_string(position));
    }
    return data;
  }

  // Read specific block from file and into buffer.
  void fs_read_block(uint block, char* dst) {
    image_file.seekg(block * BSIZE);
    image_file.read(dst, BSIZE);
    if (!image_file.good()) {
      throw std::runtime_error("Failed to read block " +
                               std::to_string(block));
    }
  }

  // Write data from buffer into a specific block in file.
  void fs_write_block(uint block, const char* src) {
    image_file.seekp(block * BSIZE);
    image_file.write(src, BSIZE);
    if (!image_file.good()) {
      throw std::runtime_error("Failed to write block " +
                               std::to_string(block));
    }
  }

  // Create a file, where all blocks are filled with zeroes.
  void init_file() {
    std::array<char, BSIZE> zeroes;
    zeroes.fill(0);
    for (uint i = 0; i < blocks; i++) {
      fs_write_block(i, zeroes.data());
    }
  }

  // Fill superblock with fs data and write it to the file.
  void init_superblock() {
    sb.magic = FSMAGIC;
    sb.size = riskv_int(blocks);
    sb.nblocks = riskv_int(blocks - metadata_blocks);
    sb.ninodes = riskv_int(inodes);
    sb.nlog = riskv_int(log_blocks);
    sb.logstart = riskv_int(2);
    sb.inodestart = riskv_int(2 + log_blocks);
    sb.bmapstart = riskv_int(2 + log_blocks + inode_blocks);

    write_struct(sb, block_pos(1));
  }

  void print_fs_layout() const {
    cout << "FS layout:" << endl;
    cout << "[boot | superblock | log " << log_blocks << " blocks"
         << " | inode " << inode_blocks << " blocks"
         << " | bitmap " << bitmap_size << " blocks"
         << " | data " << blocks - metadata_blocks << " blocks]" << endl;
    cout << "Total blocks: " << blocks << endl;
    cout << "Max inodes: " << inodes << endl;
    cout << "Log size in blocks: " << log_blocks << endl;
  }

  uint inode_block(uint inode_number) const {
    return sb.inodestart + inode_number / INODES_PER_BLOCK;
  }

  static uint inode_inblock_pos(uint inode_number) {
    return inode_number % INODES_PER_BLOCK;
  }

  uint inode_byte_position(uint inode) {
      return block_pos(inode_block(inode)) +
             sizeof(dinode) * inode_inblock_pos(inode);
  }

  uint alloc_inode(ushort type) {
    uint inode_id = free_inode_index++;
    dinode inode{};
    memset(&inode, 0, sizeof(dinode));

    inode.type = riskv_short(type);
    inode.nlink = riskv_short(1);
    inode.size = riskv_int(0);

    write_struct(inode, inode_byte_position(inode_id));
    return inode_id;
  }

  void init_directory(uint inode_id, uint inode_parent) {
    dinode inode = read_struct<dinode>(inode_byte_position(inode_id));
    if (inode.type != T_DIR) {
      throw std::runtime_error("Cannot init inode " +
                               std::to_string(inode_id) +
                               ": not a directory");
    }
    dirent directory{};
    memset(&directory, 0, sizeof(dirent));
    // .
    directory.inum = riskv_int(inode_id);
    strcpy(directory.name, ".");
    append_to_inode(inode_id,
                    &directory,
                    sizeof(dirent));
    // ..
    directory.inum = riskv_int(inode_parent);
    strcpy(directory.name, "..");
    append_to_inode(inode_id,
                    &directory,
                    sizeof(dirent));
  }

  void make_root_inode() {
    root_inode = alloc_inode(T_DIR);
    assert(root_inode == ROOTINO);
    init_directory(root_inode, root_inode);
    cout << "Allocated root inode" << endl;
  }

  void append_to_inode(uint inode_id, const void* ptr, uint n) {
    const char* src = static_cast<const char*>(ptr);
    std::array<char, BSIZE> data_append_buffer;
    std::array<uint, NINDIRECT> indirect_buffer;

    dinode inode = read_struct<dinode>(inode_byte_position(inode_id));
    uint offset = riskv_int(inode.size);
    bool read_indirect = false;
    while (n > 0) {
      uint last_block = offset / BSIZE;
      if (last_block >= MAXFILE) {
        string reason = string("Inode exhausted: ") +
                        ((inode.type == T_FILE) ?
                          "file too big" : "too many elements in directory");
        throw std::runtime_error(reason);
      }
      uint data_block;
      // find last non-full block of the inode_id src
      if (last_block < NDIRECT) { // not a big file or dir.
        if (inode.addrs[last_block] == 0) {
          inode.addrs[last_block] = riskv_int(free_block++);
        }
        data_block = riskv_int(inode.addrs[last_block]);
      } else { // block id in indirect section
        if (riskv_int(inode.addrs[NDIRECT]) == 0) {
          inode.addrs[NDIRECT] = riskv_int(free_block++);
        }
        if (!read_indirect) {
          fs_read_block(riskv_int(inode.addrs[NDIRECT]),
                        reinterpret_cast<char*>(indirect_buffer.data()));
          read_indirect = true;
        }
        uint &cnt_entry = indirect_buffer[last_block - NDIRECT];
        if (riskv_int(cnt_entry) == 0) {
          cnt_entry = riskv_int(free_block++);
        }
        data_block = riskv_int(cnt_entry);
      }
      uint current_write = std::min(n, (last_block + 1) * BSIZE - offset);

      // append part of data from src to block
      fs_read_block(data_block, data_append_buffer.data());
      memcpy(data_append_buffer.data() + offset - last_block * BSIZE, src, current_write);
      fs_write_block(data_block, data_append_buffer.data());
      n -= current_write;
      offset += current_write;
      src += current_write;
    }

    // update on-file inode and indirect
    inode.size = riskv_int(offset);
    write_struct(inode, inode_byte_position(inode_id));
    if (read_indirect) {
      fs_write_block(riskv_int(inode.addrs[NDIRECT]),
                     reinterpret_cast<char*>(indirect_buffer.data()));
    }
  }

  void mark_blocks_allocated(uint used) {
    cout << "Marking first " << used << " blocks allocated" << endl;
    std::array<char, BSIZE> buffer;
    buffer.fill(0xff);
    uint block = 0;
    for (; block < used / (BSIZE * 8); block++) {
      fs_write_block(riskv_int(sb.bmapstart) + block,
                     buffer.data());
    }
    buffer.fill(0);
    used %= (BSIZE * 8);
    for (uint i = 0; i < used / 8; i++) {
      buffer[i] = 0xff;
    }
    buffer[used / 8] = (1 << (used % 8)) - 1;
    fs_write_block(riskv_int(sb.bmapstart) + block,
                   buffer.data());
  }
};

string transform_name(const string& name) {
  auto name_start = name.find_last_of('/');
  if (name_start == string::npos) {
    name_start = 0;
  } else {
    name_start++;
  }
  if (name[name_start] == '_') name_start++;
  return name.substr(name_start);
}

int main(int argc, char *argv[]) {
  static_assert(sizeof(int) == 4, "int must be 4 byte");
  static_assert(BSIZE % sizeof(dinode) == 0, "inode struct is not block-aligned");
  static_assert(BSIZE % sizeof(dirent) == 0, "dentry struct is not block-aligned");

  if (argc < 2) {
    std::cerr << "Usage make_fs fs.img files..." << endl;
    return 1;
  }
  fs_builder fs = fs_builder(string(argv[1]),
                             FSSIZE,
                             INODE_NUMBER,
                             LOG_BLOCKS);
  for (int i = 2; i < argc; i++) {
    string file = string(argv[i]);
    std::ifstream data(file, std::ios::in | std::ios::binary);
    fs.add_file(data, transform_name(file), ROOTINO);
  }
  fs.complete_fs();
}