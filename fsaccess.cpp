// run on cs2.utdallas.edu
// compile with: 
//   g++ -o fsaccess fsaccess.cpp
// run with:
//   ./fsaccess

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

unsigned int isize, fsize, nfree, ninode, fd, fd2, buf, temp;
unsigned int free_blocks[100]; // free is a keyword
unsigned int free_inodes[100];
unsigned short block_size = 2048, avail_blocks;

int get_offset(int block, int offset=0)
{
	return block * block_size + offset;
}

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

int alloc_block()
{
	nfree--;
	if(nfree > 0)
	{
		// cout << endl << "allocating " << free_blocks[nfree] << endl;
		avail_blocks --;
		return free_blocks[nfree];
	}
	unsigned int new_block = free_blocks[0];
	fill_free_list(new_block);
	// cout << endl << "allocating " << free_blocks[nfree] << endl;
	avail_blocks --;
	return new_block;
}

struct inode
{
	unsigned short flags;
	char nlinks;
	char uid;
	char gid;
	unsigned int size;
	unsigned int addr[27];
	unsigned int actime;
	unsigned int modtime;
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
	ALLOC,
	PLAIN,
	DIR,
	LARGE
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

struct dir_entry
{
	unsigned short inode_num;
	char name[14];
};

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

int alloc_inode()
{
	if(ninode > 0)
	{
		ninode--;
		return free_inodes[ninode];
	}
	// ninode is 0 if it gets here
	fill_inode_list();
	if(ninode > 0)
		return alloc_inode(); 
	//no inodes left!
	return -1;
}

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

void init_inode(int dir, inode_flag type)
{
	current = get_inode(dir);
	set_flag(current.flags, ALLOC);
	set_flag(current.flags, type);
	write_inode(dir, current);
}

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
void copy_to_inode(int inum, int size)
{
	current = get_inode(inum);
	char buffer[block_size];
	int nread, position;
	current.size = size;
	position = lseek(fd2, 0, SEEK_SET);
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

void copy_from_inode(int inum)
{
	current = get_inode(inum);
	char buffer[block_size];
	int nread;
	unsigned int num_blocks = current.size / block_size + 1;
	lseek(fd2, 0, SEEK_SET);
	if(flag_is_set(current.flags, PLAIN))
	{
		for(int i = 0; i < num_blocks; i++)
		{
			lseek(fd, get_offset(current.addr[i]), SEEK_SET);
			nread = read(fd, buffer, (i < num_blocks - 1) ? sizeof(buffer) : current.size % block_size);
			write(fd2, buffer, nread);
		}
	}
	else if(flag_is_set(current.flags, LARGE))
	{
		int counter,counter2, last_block;
		last_block = current.size / block_size;
		unsigned int second_block;
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

int get_inode_number(string name)
{
	inode root;
	dir_entry temp_entry;
	lseek(fd, 2 * block_size, SEEK_SET);
	read(fd, &root, sizeof(root));

	for(int i = 0; i < root.size / 16; i++)
	{
		if(i % sizeof(inode) == 0)
		{
			lseek(fd, get_offset(root.addr[i / sizeof(inode)]), SEEK_SET);
		}
		read(fd, &temp_entry, sizeof(temp_entry));
		if(strcmp(name.c_str(), temp_entry.name) == 0)
		{
			return temp_entry.inode_num;
		}
	}
	return -1;
}

int main()
{
	// main loop for reading commands
	string command = "";
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
			ss >> filename >> fsize >> isize;
			if(filename == "" || fsize < 1 || isize < 1 || fsize < isize)
			{
				cout << "Please input a filename for the disk, a number of disk blocks, and a number of inode blocks." << endl;
			}
			else
			{

				fd = open(filename.c_str(), O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);

				// setting length of file
				ftruncate(fd, (off_t)fsize * block_size);
				
				// init free list
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
						// cout << "FB: " << buf << endl;
					}	
				}
				nfree = (fsize - isize - 2) % 100;
				temp *= 100;
				temp += (2 + isize);
				for(int j = 0; j < nfree; j++)
				{
					free_blocks[j] = temp + j;
					// cout << "FB: " << temp + j << endl;
				}
				avail_blocks = fsize - isize - 2;

				// init inodes
				inode initial = {0};
				set_flag(initial.flags, ALLOC);
				set_flag(initial.flags, DIR);

				lseek(fd, 2 * block_size, SEEK_SET);
				write(fd, &initial, sizeof(initial));

				add_dir_entry(1, 1, ".");
				add_dir_entry(1, 1, "..");

				// find free inodes and put into inode list
				fill_inode_list();

			}
		}
		else if(first == "cpin")
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
						cout << "The file is too large, we need " << ceil(size / block_size) + (size / block_size) / (block_size / 4) + 1 << " free blocks to store it but we only have " << avail_blocks <<  "." << endl;
					}
					else
					{
						cout << avail_blocks << " available" << endl;
						int inum = alloc_inode();
						if(inum == -1)
						{
							cout << "There are no more inodes available." << endl;
						}
						else
						{
							// allocates and tells if large file or not
							init_inode(inum, (size > 27 * block_size) ? LARGE : PLAIN);
							add_dir_entry(1, inum, v6_file);
							copy_to_inode(inum, size);
							//check if large later
						}
					}
				}
				close(fd2);
			}
		}
		else if(first == "cpout")
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
					copy_from_inode(inum);
					close(fd2);
				}
			}
		}
		else if(first == "ls")
		{
			read_contents(1);
		}
		else if(first == "mkdir")
		{
			ss >> dirname;
			if(dirname == "")
			{
				cout << "Please input a name for the directory." << endl;
			}
			else if(avail_blocks == 0)
			{
				cout << "We need at least one block to make a new directory." << endl;
			}
			else
			{
				int inum = alloc_inode();
				init_inode(inum, DIR);
				// adds to root directory
				add_dir_entry(1, inum, dirname);
				add_dir_entry(inum, inum, ".");
				add_dir_entry(inum, 1, "..");
			}
		}
		else if(first == "q")
		{
			close(fd);
			cout << "Goodbye." << endl;
		}
		else
		{
			cout << "Sorry, I don't recognize that command." << endl;
		}

		ss.clear();
	}while(command != "q");

	return 0;

}