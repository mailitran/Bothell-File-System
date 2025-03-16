// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) { 
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0; 
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {
  i32 inum = bfsFdToInum(fd);
  i32 curs = bfsTell(fd);      // Current cursor position
  i32 size = bfsGetSize(inum); // File size

  if (curs >= size) return 0;  // Cursor is at or beyond EOF (no bytes read)
  if ((curs + numb) > size) numb = size - curs; // Adjust numb to remaining bytes

  i32 fbn = curs / BYTESPERBLOCK;    // FBN of block that the cursor is in
  i32 offset = curs % BYTESPERBLOCK; // Offset of cursor within the block
  i32 num_blocks = (size + BYTESPERBLOCK - 1) / BYTESPERBLOCK; // Total number of blocks
  
  i32 bytes_read = 0;

  // If cursor is in the middle of a block, read the remainder of the current block 
  if (offset != 0) {
    i8 bioBuf[BYTESPERBLOCK];   // Allocate temporary buffer
    bfsRead(inum, fbn, bioBuf); // Read current block into the buffer
    i32 to_read = (numb < BYTESPERBLOCK - offset) ? numb : BYTESPERBLOCK - offset;
    memcpy(buf, bioBuf + offset, to_read);
    fsSeek(fd, to_read, SEEK_CUR); // Adjust cursor position by the bytes read
    bytes_read += to_read;
    if (bytes_read == numb) return bytes_read;
    fbn++;                         // Move to next FBN
  }

  while ((bytes_read < numb) && (fbn < num_blocks)) {
    i8 bioBuf[BYTESPERBLOCK];   // Allocate temporary buffer
    bfsRead(inum, fbn, bioBuf); // Read current block into the buffer
    i32 to_read = ((numb - bytes_read) < BYTESPERBLOCK) ? (numb - bytes_read) : BYTESPERBLOCK;
    memcpy(buf + bytes_read, bioBuf, to_read);
    fsSeek(fd, to_read, SEEK_CUR); // Adjust cursor position by the bytes read
    bytes_read += to_read;
    fbn++;                         // Move to next FBN
  }

  return bytes_read; // Return total bytes read
}


// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);
 
  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);
  
  switch(whence) {
    case SEEK_SET:
      g_oft[ofte].curs = offset;
      break;
    case SEEK_CUR:
      g_oft[ofte].curs += offset;
      break;
    case SEEK_END: {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
      }
    default:
        FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}



// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {
  i32 inum = bfsFdToInum(fd);
  i32 curs = bfsTell(fd);          // Current cursor position
  i32 size = bfsGetSize(inum); // File size

  i32 fbn = curs / BYTESPERBLOCK;    // FBN of block that the cursor is in
  i32 offset = curs % BYTESPERBLOCK; // Offset of cursor within the block
  i32 num_blocks = (size + BYTESPERBLOCK - 1) / BYTESPERBLOCK; // Total number of blocks

  if ((curs + numb) > size) {
    bfsExtend(inum, num_blocks);   // Extend file size
    bfsSetSize(inum, curs + numb); // Update file size
  }

  i32 bytes_written = 0;

  // If cursor is in the middle of a block, write the remainder of the current block 
  if (offset != 0) {
    i8 bioBuf[BYTESPERBLOCK];   // Allocate temporary buffer
    bfsRead(inum, fbn, bioBuf); // Read current block into the buffer
    i32 to_write = (numb < BYTESPERBLOCK - offset) ? numb : BYTESPERBLOCK - offset;
    memcpy(bioBuf + offset, buf, to_write); // Copy data from buf to temporary buffer
    i32 dbn = bfsFbnToDbn(inum, fbn); // Convert FBN to DBN
    bioWrite(dbn, bioBuf);            // Write temporary buffer to disk
    fsSeek(fd, to_write, SEEK_CUR);   // Adjust cursor position by the bytes read
    bytes_written += to_write;
    if (bytes_written == numb) return 0;
    buf = (i8*)buf + to_write; // Update buf pointer
    fbn++;                     // Move to next FBN
  }

  while (bytes_written < numb)  {
    i8 bioBuf[BYTESPERBLOCK];      // Allocate temporary buffer
    bfsRead(inum, fbn, bioBuf);    // Read current block into the buffer
    i32 to_write = ((numb - bytes_written) < BYTESPERBLOCK) ? (numb - bytes_written) : BYTESPERBLOCK;
    memcpy(bioBuf, buf, to_write);    // Copy data from buf to temporary buffer
    i32 dbn = bfsFbnToDbn(inum, fbn); // Convert FBN to DBN
    bioWrite(dbn, bioBuf);            // Write temporary buffer to disk
    fsSeek(fd, to_write, SEEK_CUR);   // Adjust cursor position by the bytes read
    bytes_written += to_write;
    buf = (i8*)buf + to_write; // Update buf pointer
    fbn++;                     // Move to next FBN
  }

  return 0;
}
