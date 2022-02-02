# Simple File System

A rudimentary implementation of a file system that interacts with the fuse interface, supporting creating, reading, writing, removing, and getting a small number of attributes from files and directories.


## Usage
A binary named nufs along with the source code in nufs.c. A make file is provided with the following notable targets:

* `make nufs` compiles the nufs.c file into a binary.
* `make mount` compiles the nufs.c file into a binary, creates a new disk image if one does not exist, and runs to program. The data.nufs disk image is mounted and the custom file system operations are provided to FUSE.
* `make unmount` unmounts the data.nufs disk image
* `make clean` unmounts and removes the data.nufs disk image and removes the binary file


## Design

This program uses an inode table to keep track of the blocks that files occupy similar to how a real file system would work. Although only basic file operations are currently supported, the program design is flexible enough to allow the addition of new features in the future. A next step would be to add support for indirection, as the maximum file size is currently limited by the size of a block (4KB). 


## Purpose
This project was created in order to better understand the complexities and design considerations of file systems, and to understand how virtual file systems allow custom implementations to inferface with the kernel.


## Technologies

* Ubuntu - used for development
* FUSE - interface with the virtual file system
* C - used to implement the file system operations


## Liscense
 
Portions of this code were provided by CS3650 at Northeastern University.