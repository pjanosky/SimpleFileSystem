// based on cs3650 starter code

#include <assert.h>
#include <bsd/string.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

// bitmap macros
#define nth_bit_mask(n) (1 << (n))
#define byte_index(n) ((n) / 8)
#define bit_index(n) ((n) % 8)

// blocks constants
const int BLOCK_COUNT = 256; // we split the "disk" into 256 blocks
const int BLOCK_SIZE = 4096; // = 4K
const int NUFS_SIZE = BLOCK_SIZE * BLOCK_COUNT; // = 1MB

const int BLOCK_BITMAP_SIZE = BLOCK_COUNT / 8;
const int INODE_BITMAP_SIZE = BLOCK_COUNT / 8;
const int NUM_INODES = 251;
// Note: assumes block count is divisible by 8

static int blocks_fd = -1;
static void *blocks_base = 0;


// nufs constants
#define NUM_INODES 3


typedef struct inode {
  int size; // this size of the file
  int block; // the block number where the file is stored
} inode_t;

// ----------------------------------------------------------------
// bitmap helper code
// ----------------------------------------------------------------

// Get the given bit from the bitmap.
int bitmap_get(void *bm, int i) {
  uint8_t *base = (uint8_t *) bm;

  return (base[byte_index(i)] >> bit_index(i)) & 1;
}

// Set the given bit in the bitmap to the given value.
void bitmap_put(void *bm, int i, int v) {
  uint8_t *base = (uint8_t *) bm;

  long bit_mask = nth_bit_mask(bit_index(i));

  if (v) {
    base[byte_index(i)] |= bit_mask;
  } else {
    bit_mask = ~bit_mask;
    base[byte_index(i)] &= bit_mask;
  }
}

// Pretty-print the bitmap (with the given no. of bits).
void bitmap_print(void *bm, int size) {

  for (int i = 0; i < size; i++) {
    putchar(bitmap_get(bm, i) ? '1' : '0');

    if ((i + 1) % 64 == 0) {
      putchar('\n');
    } else if ((i + 1) % 8 == 0) {
      putchar(' ');
    }
  }
}



// ----------------------------------------------------------------
// blocks helper code
// ----------------------------------------------------------------

// Get the number of blocks needed to store the given number of bytes.
int bytes_to_blocks(int bytes) {
  int quo = bytes / BLOCK_SIZE;
  int rem = bytes % BLOCK_SIZE;
  if (rem == 0) {
    return quo;
  } else {
    return quo + 1;
  }
}


// Get the given block, returning a pointer to its start.
void *blocks_get_block(int bnum) { return blocks_base + BLOCK_SIZE * bnum; }

// Return a pointer to the beginning of the block bitmap.
// The size is BLOCK_BITMAP_SIZE bytes.
void *get_blocks_bitmap() { return blocks_get_block(0); }

// Load and initialize the given disk image.
void blocks_init(const char *image_path) {
  blocks_fd = open(image_path, O_CREAT | O_RDWR, 0644);
  assert(blocks_fd != -1);

  // make sure the disk image is exactly 1MB
  int rv = ftruncate(blocks_fd, NUFS_SIZE);
  assert(rv == 0);

  // map the image to memory
  blocks_base =
      mmap(0, NUFS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, blocks_fd, 0);
  assert(blocks_base != MAP_FAILED);

  // block 0 stores the block bitmap and the inode bitmap
  void *bbm = get_blocks_bitmap();
  for(int i = 0; i < 5; i ++) {
    bitmap_put(bbm, i, 1);
  }
}

// Close the disk image.
void blocks_free() {
  int rv = munmap(blocks_base, NUFS_SIZE);
  assert(rv == 0);
}

// Return a pointer to the beginning of the inode table bitmap.
void *get_inode_bitmap() {
  uint8_t *block = blocks_get_block(0);

  // The inode bitmap is stored immediately after the block bitmap
  return (void *) (block + BLOCK_BITMAP_SIZE);
}

// get the given inode
inode_t *get_inode(int inum) {
  uint8_t *block = blocks_get_block(0);
  return ((inode_t *) (block + BLOCK_BITMAP_SIZE + INODE_BITMAP_SIZE)) + inum;
}

// gets the directory entry with the given index
char *directory_get(int dnum) {
  char *dir_base = (char *) blocks_get_block(1);
  return dir_base + 64 * dnum;
}

// puts a name in the directory entry with the given index
void directory_put(int dnum, const char *name) {
  strncpy(directory_get(dnum), name, 64);
}

// looks up the inode number corresponding to a given filename in the directory
int directory_lookup(const char *name) {
  for(int i = 0; i < BLOCK_COUNT; ++i) {
    if (strcmp(name, directory_get(i)) == 0) {
      return i;
    }
  }
  return -1;
}

// Allocate a new block and return its index.
int alloc_block() {
  void *bbm = get_blocks_bitmap();

  for (int ii = 5; ii < BLOCK_COUNT; ++ii) {
    if (!bitmap_get(bbm, ii)) {
      bitmap_put(bbm, ii, 1);
      printf("+ alloc_block() -> %d\n", ii);
      return ii;
    }
  }

  return -1;
}

// Deallocate the block with the given index.
void free_block(int bnum) {
  printf("+ free_block(%d)\n", bnum);
  void *bbm = get_blocks_bitmap();
  bitmap_put(bbm, bnum, 0);
}


// ----------------------------------------------------------------
// nufs functions
// ----------------------------------------------------------------

// implementation for: man 2 access
// Checks if a file exists.
int nufs_access(const char *path, int mask) {
  int rv = 0;

  if(directory_lookup(path) >= 0) {
    rv = 0; // file exists
  } else {
    rv = -ENOENT; // file does not exist
  }

  printf("access(%s, %04o) -> %d\n", path, mask, rv);
  return rv;
}

// Gets an object's attributes (type, permissions, size, etc).
// Implementation for: man 2 stat
// This is a crucial function.
int nufs_getattr(const char *path, struct stat *st) {
  int rv = 0;

  int inum = directory_lookup(path);
  if (inum < 0) {
    rv = -ENOENT;
  } else {
    inode_t *node = get_inode(inum);
    st->st_mode = 0100644;
    st->st_size = node->size;
    st->st_uid = getuid();
  }
  printf("getattr(%s) -> (%d) {mode: %04o, size: %ld}\n", path, rv, st->st_mode,
         st->st_size);
  return rv;
}

// implementation for: man 2 readdir
// lists the contents of a directory
int nufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi) {
  struct stat st;
  int rv;

  rv = nufs_getattr("/", &st);
  assert(rv == 0);

  filler(buf, ".", &st, 0);

  rv = nufs_getattr("/hello.txt", &st);
  assert(rv == 0);
  filler(buf, "hello.txt", &st, 0);

  printf("readdir(%s) -> %d\n", path, rv);
  return 0;
}

// mknod makes a filesystem object like a file or directory
// called for: man 2 open, man 2 link
// Note, for this assignment, you can alternatively implement the create
// function.
int nufs_mknod(const char *path, mode_t mode, dev_t rdev) {
  int rv = -1;
  printf("mknod(%s, %04o) -> %d\n", path, mode, rv);
  return rv;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int nufs_mkdir(const char *path, mode_t mode) {
  int rv = nufs_mknod(path, mode | 040000, 0);
  printf("mkdir(%s) -> %d\n", path, rv);
  return rv;
}

int nufs_unlink(const char *path) {
  int rv = -1;
  printf("unlink(%s) -> %d\n", path, rv);
  return rv;
}

int nufs_link(const char *from, const char *to) {
  int rv = -1;
  printf("link(%s => %s) -> %d\n", from, to, rv);
  return rv;
}

int nufs_rmdir(const char *path) {
  int rv = -1;
  printf("rmdir(%s) -> %d\n", path, rv);
  return rv;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int nufs_rename(const char *from, const char *to) {
  int rv = -1;
  printf("rename(%s => %s) -> %d\n", from, to, rv);
  return rv;
}

int nufs_chmod(const char *path, mode_t mode) {
  int rv = -1;
  printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
  return rv;
}

int nufs_truncate(const char *path, off_t size) {
  int rv = -1;
  printf("truncate(%s, %ld bytes) -> %d\n", path, size, rv);
  return rv;
}

// This is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
// You can just check whether the file is accessible.
int nufs_open(const char *path, struct fuse_file_info *fi) {
  int rv = 0;
  printf("open(%s) -> %d\n", path, rv);
  return rv;
}

// Actually read data
int nufs_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
  int rv = 6;
  strcpy(buf, "hello\n");
  printf("read(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
  return rv;
}

// Actually write data
int nufs_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi) {
  int rv = -1;
  printf("write(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
  return rv;
}

// Update the timestamps on a file or directory.
int nufs_utimens(const char *path, const struct timespec ts[2]) {
  int rv = -1;
  printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n", path, ts[0].tv_sec,
         ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
  return rv;
}

// Extended operations
int nufs_ioctl(const char *path, int cmd, void *arg, struct fuse_file_info *fi,
               unsigned int flags, void *data) {
  int rv = -1;
  printf("ioctl(%s, %d, ...) -> %d\n", path, cmd, rv);
  return rv;
}

void nufs_init_ops(struct fuse_operations *ops) {
  memset(ops, 0, sizeof(struct fuse_operations));
  ops->access = nufs_access;
  ops->getattr = nufs_getattr;
  ops->readdir = nufs_readdir;
  ops->mknod = nufs_mknod;
  // ops->create   = nufs_create; // alternative to mknod
  ops->mkdir = nufs_mkdir;
  ops->link = nufs_link;
  ops->unlink = nufs_unlink;
  ops->rmdir = nufs_rmdir;
  ops->rename = nufs_rename;
  ops->chmod = nufs_chmod;
  ops->truncate = nufs_truncate;
  ops->open = nufs_open;
  ops->read = nufs_read;
  ops->write = nufs_write;
  ops->utimens = nufs_utimens;
  ops->ioctl = nufs_ioctl;
};

struct fuse_operations nufs_ops;

int main(int argc, char *argv[]) {
  assert(argc > 2 && argc < 6);
    
  nufs_init_ops(&nufs_ops);
  return fuse_main(argc, argv, &nufs_ops, NULL);
}
