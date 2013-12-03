// CS 4348.HON Project 2: Unix V6 -based file system
// Joshua Cai and Casey Ross

// run on cs2.utdallas.edu
// compile with: 
//   g++ -o fsaccess fsaccess.cpp
// run with:
//   ./fsaccess

// available commands are:
// initfs, cpin, cpout, mkdir, q, ls

#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <sstream>
#include <cmath>
#include <cstring>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

using namespace std;

unsigned int isize, fsize, nfree, ninode, buf, temp, current_inode;
int fd, fd2; // can't be unsigned because will return -1 on error
unsigned int free_blocks[100]; // free is a keyword
unsigned int free_inodes[100];
unsigned short block_size = 2048, avail_blocks;

// Writes super block to the file system
void write_super()
{
	lseek(fd, 1 * block_size, SEEK_SET);
	write(fd, &isize, sizeof(isize));
	write(fd, &fsize, sizeof(fsize));
	write(fd, &nfree, sizeof(nfree));
	write(fd, &ninode, sizeof(ninode));
	write(fd, &avail_blocks, sizeof(avail_blocks));
	write(fd, free_blocks, sizeof(free_blocks));
	write(fd, free_inodes, sizeof(free_inodes));
}

// Reads from the super block of the file system
void read_super()
{
	lseek(fd, 1 * block_size, SEEK_SET);
	read(fd, &isize, sizeof(isize));
	read(fd, &fsize, sizeof(fsize));
	read(fd, &nfree, sizeof(nfree));
	read(fd, &ninode, sizeof(ninode));	
	read(fd, &avail_blocks, sizeof(avail_blocks));
	read(fd, free_blocks, sizeof(free_blocks));
	read(fd, free_inodes, sizeof(free_inodes));
}

int get_offset(int block, int offset=0)
{
	return block * block_size + offset;
}
 
// Copy block numbers found in the given block to the free list.
// The first word of the block should have the number of numbers in the next 100 blocks.
// The second word of the block should be the number of another such block.
void fill_free_list(int block_num)
{
	lseek(fd, get_offset(block_num), SEEK_SET);
	read(fd, &nfree, sizeof(nfree));

	for(int i = 0; i < 100; i++)
	{
		read(fd, &buf, sizeof(buf));
		free_blocks[i] = buf;
	}
}

// Get a free block number from the free list.
// Don't call this if there are none left.
int alloc_block()
{
	nfree--;
	if(nfree > 0)
	{
		avail_blocks --;
		return free_blocks[nfree];
	}

	// Refill list using free_blocks[0]
	unsigned int new_block = free_blocks[0];
	fill_free_list(new_block);
	
	avail_blocks --;
	return new_block;
}

// A 128-byte (with padding) inode.
struct inode
{
	unsigned short flags;
	/*
		Flags currently in use (from left to right):
		Bit		Meaning
		0		Allocated if 1
		1-2		Plain file if 11, directory if 10
		3		Large file if 1
	*/
	char nlinks; // Number of links to inode (unused)
	char uid; // User id of owner (unused)
	char gid; // Group id of owner (unused)
	unsigned int size; // Size in bytes
	unsigned int addr[27]; // Addresses of direct or indirect data blocks
	unsigned int actime; // Time of last access (unused)
	unsigned int modtime; // Time of last modification (unused)
} initial, current;

inode get_inode(int inum)
{
	lseek(fd, 2 * block_size + (inum - 1) * sizeof(inode), SEEK_SET);
	read(fd, &current, sizeof(current));
	return current;	
}

void write_inode(int inum, inode node)
{
	lseek(fd, 2 * block_size + (inum - 1) * sizeof(inode), SEEK_SET);
	write(fd, &node, sizeof(node));
}

enum inode_flag
{
	ALLOC, // Is allocated
	PLAIN, // Is a regular file
	DIR, // Is a directory
	LARGE // Is a large file (more than ~55kB)
};

void set_flag(unsigned short &flags, inode_flag i_flag)
{
	switch(i_flag)
	{
		case ALLOC: flags |= 0x8000;
			break;
		case PLAIN: flags |= 0x6000;
			break;
		case DIR: flags |= 0x4000;
			flags &= ~(0x2000);
			break;
		case LARGE: flags |= 0x1000;
			break;
	}
}

int flag_is_set(unsigned short flags, inode_flag i_flag)
{
	switch(i_flag)
	{
		case ALLOC: return (flags & 0x8000) == 0x8000;
			break;
		case PLAIN: return (flags & 0x6000) == 0x6000;
			break;
		case DIR: return (flags & 0x4000) == 0x4000;
			break;
		case LARGE: return (flags & 0x1000) == 0x1000;
			break;
	}
}

// A 16-byte directory entry.
struct dir_entry
{
	unsigned short inode_num;
	char name[14];
};

// Search for and add free inodes to the inode list (up to 100).
void fill_inode_list()
{
	lseek(fd, 2 * block_size, SEEK_SET);
	int counter = 0;
	for(int i = 0; i < (block_size / sizeof(inode)) * isize; i ++) // 16, 2048 (block size) / 128 (inode size)
	{
		if(counter >= 100)
		{
			break;
		}
		read(fd, &current, sizeof(current));
		if(!flag_is_set(current.flags, ALLOC))
		{
			free_inodes[counter] = i + 1; // since root inode starts at 1 
			counter++; 
		}
	}
	ninode = counter;
}

// Get a free inode number from the inode list.
// Returns -1 if there are none.
int alloc_inode()
{
	if(ninode > 0)
	{
		ninode--;
		return free_inodes[ninode];
	}

	// List is empty, so...
	fill_inode_list();
	if(ninode > 0)
	{
		return alloc_inode(); 
	}
	
	// No inodes left!
	return -1;
}

// Make a new entry in the given directory with the given inode number and name.
void add_dir_entry(int dir, int inode_num, string name)
{
	dir_entry entry;
	entry.inode_num = inode_num;
	strcpy(entry.name, name.c_str());

	lseek(fd, 2 * block_size + (dir - 1) * sizeof(inode), SEEK_SET);
	read(fd, &current, sizeof(current));
	if(current.size % block_size == 0)
	{
		current.addr[current.size / block_size] = alloc_block();
	}
	lseek(fd, get_offset(current.addr[current.size / block_size], current.size % block_size), SEEK_SET);
	write(fd, &entry, sizeof(entry));

	current.size += sizeof(entry);
	lseek(fd, 2 * block_size + (dir - 1) * sizeof(inode), SEEK_SET);
	write(fd, &current, sizeof(current));
}

// Write a new inode into the given directory.
// Type should be PLAIN, LARGE, or DIR.
void init_inode(int dir, inode_flag type)
{
	current = get_inode(dir);
	set_flag(current.flags, ALLOC);
	set_flag(current.flags, type);
	write_inode(dir, current);
}

// Print the inode numbers and names of all files in a directory.
void read_contents(int inum)
{
	current = get_inode(inum);
	dir_entry temp_entry;
	int index = 0;

	while(index < current.size / sizeof(dir_entry))
	{
		if(index % (block_size / sizeof(dir_entry)) == 0)
		{
			lseek(fd, get_offset(current.addr[index / (block_size / sizeof(dir_entry))]), SEEK_SET);
		}
		read(fd, &temp_entry, sizeof(temp_entry));
		printf("%-8d %s \n", temp_entry.inode_num, temp_entry.name);
		index++;
	}

}

// Copy a file outside of the filesystem to an internal file.
void copy_to_inode(int inum, int size)
{
	current = get_inode(inum);
	char buffer[block_size];
	int nread, position;
	current.size = size;
	position = lseek(fd2, 0, SEEK_SET);

	// Regular file
	if(flag_is_set(current.flags, PLAIN))
	{
		while((nread=read(fd2, buffer, sizeof(buffer))) > 0)
		{
			current.addr[position / block_size] = alloc_block();
			lseek(fd, get_offset(current.addr[position / block_size]), SEEK_SET);
			write(fd, buffer, sizeof(buffer));
			position+=nread;
		}
	}

	// Large file
	else if(flag_is_set(current.flags, LARGE))
	{
		int counter;
		unsigned int new_block;

		for(int i = 0; i < size / (block_size * block_size / sizeof(new_block))+1; i++)
		{
			counter = 0;
			current.addr[i] = alloc_block();
			while(counter < (block_size / sizeof(new_block)) && (nread=read(fd2, buffer, sizeof(buffer))) > 0)
			{
				new_block = alloc_block();
				lseek(fd, get_offset(current.addr[i], sizeof(new_block)*counter), SEEK_SET);
				write(fd, &new_block, sizeof(new_block));
				lseek(fd, get_offset(new_block), SEEK_SET);
				write(fd, buffer, sizeof(buffer));
				counter++;
			}
		}

	}
	write_inode(inum, current);
}

// Copy a file in the filesystem to an external file.
void copy_from_inode(int inum)
{
	current = get_inode(inum);
	char buffer[block_size];
	int nread;
	unsigned int num_blocks = current.size / block_size + 1;
	lseek(fd2, 0, SEEK_SET);

	// Regular file
	if(flag_is_set(current.flags, PLAIN))
	{
		for(int i = 0; i < num_blocks; i++)
		{
			lseek(fd, get_offset(current.addr[i]), SEEK_SET);
			nread = read(fd, buffer, (i < num_blocks - 1) ? sizeof(buffer) : current.size % block_size);
			write(fd2, buffer, nread);
		}
	}

	// Large file
	else if(flag_is_set(current.flags, LARGE))
	{
		unsigned int counter, counter2, second_block, last_block;
		last_block = current.size / block_size;
		counter2 = 0;
		for(int i = 0; i < current.size / (block_size * block_size / sizeof(second_block))+1; i++)
		{
			counter = 0;
			while(counter < (block_size / sizeof(second_block)))
			{
				lseek(fd, get_offset(current.addr[i], sizeof(second_block)*counter), SEEK_SET);
				read(fd, &second_block, sizeof(second_block));
				lseek(fd, get_offset(second_block), SEEK_SET);
				counter2++;
				if(counter2 > last_block)
				{
					nread = read(fd, buffer, current.size % block_size);
					write(fd2, buffer, nread);
					break;
				}
				nread = read(fd, buffer, sizeof(buffer));
				write(fd2, buffer, nread);
				counter++;
			}
		}
	}
}

// Return the number of the inode with this name.
// Returns -1 if not found.
int get_inode_number(string name)
{
	current = get_inode(current_inode);
	dir_entry temp_entry;

	for(int i = 0; i < current.size / 16; i++)
	{
		if(i % sizeof(inode) == 0)
		{
			lseek(fd, get_offset(current.addr[i / sizeof(inode)]), SEEK_SET);
		}
		read(fd, &temp_entry, sizeof(temp_entry));
		if(strcmp(name.c_str(), temp_entry.name) == 0)
		{
			return temp_entry.inode_num;
		}
	}
	return -1;
}

// Checks if an inode is a directory or not
bool inode_is_dir(int inum)
{
	current = get_inode(inum);
	return flag_is_set(current.flags, DIR);
}

int main()
{
	// Main loop for reading commands
	cout << "Type 'help' to see list of available commands" << endl;
	string command = "";
	bool fs_active = false; // checks if there is a file system active
	string first, filename, dirname;

	stringstream ss;
	do
	{
		cout << "> ";
		getline(cin, command);
		ss.str(command);
		ss >> first;
		if(first == "initfs")
		{
			if(fs_active)
			{
				cout << "There is already an active file system. Please quit and start again." << endl;
				ss.clear();
				continue;
			}
			ss >> filename >> fsize >> isize;
			if(filename == "" || fsize < 6 || isize < 1 || fsize < isize)
			{
				cout << "Please input a filename for the disk, a number of disk blocks (6+), and a number of inode blocks (1+)." << endl;
			}
			else
			{
				fd = open(filename.c_str(), O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);

				// Set length of file
				ftruncate(fd, (off_t)fsize * block_size);
				
				// Initialize free list
				temp = (fsize - isize - 3) / 100;
				for(int i = 0; i < temp; i++)
				{
					lseek(fd, (2 + isize + 100*(i+1)) * block_size, SEEK_SET);
					buf = 100;
					write(fd, &buf, sizeof(buf));
					for(int j = 0; j < 100; j++)
					{
						buf = 2 + isize + 100 * i + j;
						write(fd, &buf, sizeof(buf));
					}	
				}
				nfree = (fsize - isize - 2) % 100;
				temp *= 100;
				temp += (2 + isize);
				for(int j = 0; j < nfree; j++)
				{
					free_blocks[j] = temp + j;
				}
				avail_blocks = fsize - isize - 2;

				// Initialize root directory
				inode initial = {0};
				set_flag(initial.flags, ALLOC);
				set_flag(initial.flags, DIR);
				current_inode = 1;

				lseek(fd, 2 * block_size, SEEK_SET);
				write(fd, &initial, sizeof(initial));

				add_dir_entry(1, 1, ".");
				add_dir_entry(1, 1, "..");

				// Find free inodes and add to inode list
				fill_inode_list();
				fs_active = true;

			}
		}
		else if(first == "cpin") // Copy an external file into the filesystem
		{
			if(fs_active)
			{
				string external_file, v6_file;
				ss >> external_file >> v6_file;
				if(v6_file == "" || external_file == "")
				{
					cout << "Please input a source file and a destination file." << endl;
				}
				else
				{
					fd2 = open(external_file.c_str(), O_RDONLY);
					if(fd2 == -1)
					{
						cout << "The source file couldn't be opened for reading." << endl;
					}
					else
					{
						// Get filesize
						struct stat stats;
						fstat(fd2, &stats);
						int size = stats.st_size;
						/*
							Max size:
							27 addr blocks * 512 indirect blocks
								- 1 indirect block for every 512 data blocks
								- 1 possible block to extend the directory listing
						*/
						if(size > (27 * block_size / 4 * block_size) / (1 + 1 / (block_size / 4)) - block_size)
						{
							cout << "The file is too large for the filesystem, max is " << 27 * (block_size / 4) * block_size * (1 - 1 / (block_size / 4)) - block_size << " bytes." << endl;
						}
						else if(size > (avail_blocks * block_size) / (1 + 1 / (block_size / 4)) - block_size)
						{
							cout << "The file is too large, we need " << ceil(size / block_size) + (size / block_size) / (block_size / 4) + 1 << " free blocks to store it but we only have " << avail_blocks / (1 + 1 / (block_size / 4)) - 1 <<  "." << endl;
						}
						else
						{
							int inum = alloc_inode();
							if(inum == -1)
							{
								cout << "There are no more inodes available." << endl;
							}
							else
							{
								// Allocate and report size of file
								init_inode(inum, (size > 27 * block_size) ? LARGE : PLAIN);
								add_dir_entry(current_inode, inum, v6_file);
								copy_to_inode(inum, size);
							}
						}
					}
					close(fd2);
				}
			}
			else
			{
				cout << "There is not a file system active right now. Please use 'initfs' or 'use' to start one." << endl;
			}
		}
		else if(first == "cpout") // Copy an internal file out of the filesystem
		{
			if(fs_active)
			{
				string external_file, v6_file;
				ss >> v6_file >> external_file;
				if(v6_file == "" || external_file == "")
				{
					cout << "Please input a source file and a destination file." << endl;
				}
				else
				{
					int inum = get_inode_number(v6_file);
					if(inum == -1)
					{
						cout << "The source file is invalid." << endl;
					}
					else
					{
						fd2 = open(external_file.c_str(), O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
						if(fd2 == -1)
						{
							cout << "The destination file couldn't be opened for writing." << endl;
						}
						else
						{
							copy_from_inode(inum);
						}
						close(fd2);
					}
				}
			}
			else
			{
				cout << "There is not a file system active right now. Please use 'initfs' or 'use' to start one." << endl;				
			}
		}
		else if(first == "ls") // List contents of the current directory
		{
			if(fs_active)
			{
				read_contents(current_inode);
			}
			else
			{
				cout << "There is not a file system active right now. Please use 'initfs' or 'use' to start one." << endl;
			}
		}
		else if(first == "mkdir") // Make a new directory in the root directory
		{
			if(fs_active)
			{
				ss >> dirname;
				if(dirname == "")
				{
					cout << "Please input a name for the directory." << endl;
				}
				else if(avail_blocks - 1 == 0)
				{
					cout << "We need at least one block to make a new directory." << endl;
				}
				else
				{
					// Create new directory inode
					int inum = alloc_inode();
					init_inode(inum, DIR);

					// Add to current directory
					add_dir_entry(current_inode, inum, dirname);
					add_dir_entry(inum, inum, ".");
					add_dir_entry(inum, current_inode, "..");
				}
			}
			else
			{
				cout << "There is not a file system active right now. Please use 'initfs' or 'use' to start one." << endl;
			}
		}
		else if(first == "cd") // Changes current directory
		{
			if(fs_active)
			{
				ss >> dirname;
				if(dirname != "")
				{
					int inum = get_inode_number(dirname);
					if(inum!= -1 && inode_is_dir(inum))
					{
						current_inode = inum;
					}
					else
					{
						cout << "Invalid directory" << endl;
					}
				}
			}
			else
			{
				cout << "There is not a file system active right now. Please use 'initfs' or 'use' to start one." << endl;
			}

		}
		else if(first == "q") // Quit
		{
			if(fs_active)
			{
				write_super();
				close(fd);
			}
			cout << "Goodbye." << endl;
		}
		else if(first == "use") // Open existing file system and use it
		{
			if(!fs_active)
			{
				ss >> filename;
				if(filename == "")
				{
					cout << "Please specify a file name" << endl;
				}
				else
				{
					fd = open(filename.c_str(), O_RDWR);
					if(fd == -1)
					{
						cout << "The file could not be opened for reading" << endl;
					}
					else
					{
						read_super();
						current_inode = 1;
						fs_active = true;
					}
				}
			}
			else
			{
				cout << "There is already an active file system, please quit and start again" << endl;
			}


		}
		else if(first == "help")
		{
			cout << endl << "Available commands: " << endl;
			cout << "initfs <file name of file system> <# of total blocks> <# of inode blocks>" << endl;
			cout << "cpin <external file> <v6 file>" << endl;
			cout << "cpout <v6 file> <external file>" << endl;
			cout << "mkdir <v6 dir>" << endl;
			cout << "use <file name of file system> (use existing file system file)" << endl;
			cout << "ls (lists current directory entries)" << endl;
			cout << "cd <v6 dir> (changes directories)" << endl;
			cout << "q" << endl << endl;
		}
		else
		{
			cout << "Sorry, I don't recognize that command." << endl;
		}

		ss.clear();
	}while(command != "q");

	return 0;

}