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
unsigned short block_size = 2048;

int get_offset(int block, int offset=0)
{
	return block * block_size + offset;
}

void fill_free_list(int block_num)
{
			lseek(fd, 402 * block_size, SEEK_SET);
			for(int i = 0; i < 101; i ++)
			{
				read(fd, &buf, sizeof(buf));
				cout << buf << endl;
			}
	lseek(fd, get_offset(block_num), SEEK_SET);
	read(fd, &nfree, sizeof(nfree));

	cout << nfree << endl << flush;
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
		return free_blocks[nfree];
	}
	unsigned int new_block = free_blocks[0];
	fill_free_list(new_block);
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
} initial, check;

struct dir_entry
{
	unsigned short inode_num;
	char name[14];
};

void fill_inode_list()
{
	lseek(fd, 2 * block_size, SEEK_SET);
	int counter = 0;
	inode current;
	for(int i = 0; i < 16 * isize; i ++) // 16 = 2048 (block size) / 128 (inode size)
	{
		if(counter >= 100)
		{
			break;
		}
		read(fd, &current, sizeof(current));
		if((current.flags & 0x8000) != 0x8000) // not alloc
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
	inode current;
	lseek(fd, 2 * block_size + (dir - 1) * sizeof(inode), SEEK_SET);
	read(fd, &current, sizeof(current));
	if(current.size % block_size == 0)
	{
		current.addr[current.size / block_size] = alloc_block();
	}
	lseek(fd, get_offset(current.addr[current.size / block_size], current.size % block_size), SEEK_SET);
	write(fd, &entry, sizeof(entry));

	current.size+=16;
	lseek(fd, 2 * block_size + (dir - 1) * sizeof(inode), SEEK_SET);
	write(fd, &current, sizeof(current));
}

void init_inode(int dir, int flag)
{
	inode current;
	lseek(fd, 2 * block_size + (dir - 1) * sizeof(inode), SEEK_SET);
	read(fd, &current, sizeof(current));
	current.flags |= 0x8000; // set alloc
	// set others to false here? not really needed since we don't have to worry about deletion...
	switch(flag)
	{
		case 1: current.flags |= 0x4000;
				current.flags &= ~(0x2000); // set dir
				break;
		case 2: current.flags |= 0x6000; // set plain
				break;
		case 3: current.flags |= 0x1000;
		cout << "setting to large" << endl;
				break;
	}
	lseek(fd, 2 * block_size + (dir - 1) * sizeof(inode), SEEK_SET);
	write(fd, &current, sizeof(current));
}

void read_contents(int inum)
{
	inode current;
	lseek(fd, 2 * block_size + (inum - 1) * sizeof(inode), SEEK_SET);
	read(fd, &current, sizeof(current));
	cout << endl << "current size " << current.size << endl;
	lseek(fd, get_offset(current.addr[0]), SEEK_SET);
	dir_entry temp_entry;
	for(int i = 0; i < current.size / 16; i++)
	{
		read(fd, &temp_entry, sizeof(temp_entry));
		cout << temp_entry.inode_num << " ";
		printf("%s", temp_entry.name);
		cout << endl;
	}

}
void copy_to_inode(int inum, int size)
{
	inode current;
	lseek(fd, 2 * block_size + (inum - 1) * sizeof(inode), SEEK_SET);
	read(fd, &current, sizeof(current));
	char buffer[block_size];
	int nread, position;
	current.size = size;
	position = lseek(fd2, 0, SEEK_SET);

	if((current.flags & 0x6000) == 0x6000) // plain
	{
		while((nread=read(fd2, buffer, sizeof(buffer))) > 0)
		{
			current.addr[position / block_size] = alloc_block();
			lseek(fd, get_offset(current.addr[position / block_size]), SEEK_SET);
			write(fd, buffer, sizeof(buffer));
			position+=nread;
		}
	}
	else if((current.flags & 0x1000) == 0x1000) // large
	{
		unsigned int counter = 0, indirect_block, new_block;

		while((nread=read(fd2, buffer, sizeof(buffer))) > 0 && counter < (26 * block_size / 4))
		{
			if(counter % (block_size / 4) == 0)
			{
				indirect_block = alloc_block();
				current.addr[counter / (block_size / 4)] = indirect_block;
			}
			new_block = alloc_block();
			lseek(fd, get_offset(new_block), SEEK_SET);
			write(fd, buffer, sizeof(buffer));

			lseek(fd, get_offset(indirect_block, (counter % (block_size / 4)) * sizeof(new_block)), SEEK_SET);
			write(fd, &new_block, sizeof(new_block));
			counter++;
		}
		lseek(fd, -nread, SEEK_CUR); // Unread the overflow

		current.addr[26] = alloc_block(); // Double indirect block

		while((nread=read(fd2, buffer, sizeof(buffer))) > 0)
		{
			if(counter % (block_size / 4) == 0)
			{
				indirect_block = alloc_block();
				lseek(fd, get_offset(current.addr[26], (counter / (block_size / 4) - 26) * sizeof(new_block)), SEEK_SET);
				write(fd, &indirect_block, sizeof(indirect_block));
			}
			new_block = alloc_block();
			lseek(fd, get_offset(new_block), SEEK_SET);
			write(fd, buffer, sizeof(buffer));
			
			lseek(fd, get_offset(indirect_block, (counter % (block_size / 4)) * sizeof(new_block)), SEEK_SET);
			write(fd, &new_block, sizeof(new_block));
			counter++;
		}
		cout << counter << endl;
	}
	lseek(fd, 2 * block_size + (inum - 1) * sizeof(inode), SEEK_SET);
	write(fd, &current, sizeof(current));
}

void copy_from_inode(int inum)
{
	inode current;
	lseek(fd, 2 * block_size + (inum - 1) * sizeof(inode), SEEK_SET);
	read(fd, &current, sizeof(current));
	char buffer[block_size];
	int nread;
	unsigned int num_blocks = current.size / block_size + 1;
	lseek(fd2, 0, SEEK_SET);
	if((current.flags & 0x6000) == 0x6000) // plain
	{
		for(int i = 0; i < num_blocks; i++)
		{
			lseek(fd, get_offset(current.addr[i]), SEEK_SET);
			nread = read(fd, buffer, (i < num_blocks - 1) ? sizeof(buffer) : current.size % block_size);
			write(fd2, buffer, nread);
		}
	}
	else if((current.flags & 0x1000) == 0x6000) // large
	{
		unsigned int counter = 0, indirect_block, next_block;

		while(counter < num_blocks && counter < 26 * block_size / 4)
		{
			indirect_block = current.addr[counter / (block_size / 4)];
			do{
				lseek(fd, get_offset(indirect_block, counter % (block_size / 4) * sizeof(next_block)), SEEK_SET);
				read(fd, &next_block, sizeof(next_block));

				lseek(fd, get_offset(next_block), SEEK_SET);
				nread = read(fd, buffer, sizeof(buffer));
				write(fd2, buffer, nread);
				counter++;
			}while(counter < num_blocks - 1 && !(counter % (block_size / 4) == 0));
		}
		while(counter < num_blocks)
		{
			lseek(fd, get_offset(current.addr[26], counter / (block_size / 4) - 26), SEEK_SET);
			read(fd, &indirect_block, sizeof(indirect_block));
			do{
				lseek(fd, get_offset(indirect_block, (counter % (block_size / 4) * sizeof(next_block))), SEEK_SET);
				read(fd, &next_block, sizeof(next_block));

				lseek(fd, get_offset(next_block), SEEK_SET);
				nread = read(fd, buffer, sizeof(buffer));
				write(fd2, buffer, nread);
				counter++;
			}while(counter < num_blocks && !(counter % (block_size / 4) == 0));
		}
		cout << counter << endl;
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
	do{
		cout << "> ";
		getline(cin, command);
		ss.str(command);
		ss >> first;
		if(first == "initfs")
		{
			cout << "initfs" << endl;
			// ss >> filename;
			// ss >> fsize;
			// ss >> isize;
			filename = "testing";
			fsize = 2000;
			isize = 300;
			fd = open(filename.c_str(), O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
			// setting length of file
			ftruncate(fd, (off_t)fsize * block_size);
			// init free list
			/*

				Block 0: empty
				Block 1: superblock
				Block 2: first inode block

				Block 301: last inode block
				Block 302: first free-chain block
				Block 303: empty (first free block listed in 302)

				Block 401: empty (last free block listed in 302)
				Block 402: second free-chain block

				Block 1902: last free-chain block

				Block 1999: empty (last free block listed in 1902)

			*/
			int free_chain_size = (fsize - isize - 3) / 100 + 1;
			for(int i = 0; i < free_chain_size; i++)
			{
				lseek(fd, (2 + isize + 100 * i) * block_size, SEEK_SET);
				if(i == free_chain_size - 1) // last free-chain block
				{
					buf = (fsize - isize - 3) % 100;
					write(fd, &buf, sizeof(buf));
					
					buf = 0;
					write(fd, &buf, sizeof(buf));

					for(int j = 1; j < (fsize - isize - 2) % 100; j++)
					{
						buf = 2 + isize + 100 * i + j;
						write(fd, &buf, sizeof(buf));
					}
				}
				else
				{
					buf = 100;
					write(fd, &buf, sizeof(buf));

					buf = 2 + isize + 100 * (i+1);
					write(fd, &buf, sizeof(buf));

					for(int j = 1; j < 100; j++)
					{
						buf = 2 + isize + 100 * i + j;
						write(fd, &buf, sizeof(buf));
					}
				}
			}
			fill_free_list(2 + isize);

			// init inodes
			initial.flags = 0xC000;	// alloc, dir
			initial.size = 0;
			for(int i = 0; i < 27; i++)
				initial.addr[i] = 0;

			lseek(fd, 2 * block_size, SEEK_SET);
			write(fd, &initial, sizeof(initial));

			// add_dir_entry(inode_num, inode_to_add_num, file_name);
			add_dir_entry(1, 1, ".");
			add_dir_entry(1, 1, "..");
			read_contents(1);

			// find free inodes and put into inode list
			fill_inode_list();
		}
		else if(first == "cpin")
		{
			cout << "cpin" << endl;
			string external_file, v6_file;
			// ss >> external_file;
			external_file = "asdf2";
			// ss >> v6_file;
			v6_file = "test1";
			fd2 = open(external_file.c_str(), O_RDONLY);
			struct stat stats;
			fstat(fd2, &stats);
			int size = stats.st_size;
			if(size > 26 * block_size / 4 * block_size + block_size / 4 * block_size / 4 * block_size)
			{
				cout << "file too large, max is " << 26 * block_size / 4 * block_size + block_size / 4 * block_size / 4 * block_size << " bytes";
			}
			int inum = alloc_inode();
			if(inum == -1)
			{
				cout << "no more inodes" << endl;
			}
			else
			{
				// allocates and tells if large file or not
				init_inode(inum, (size > 27 * block_size) ? 3 : 2);
				add_dir_entry(1, inum, v6_file);
				copy_to_inode(inum, size);
				//check if large later
			}

			read_contents(1);
			close(fd2);
		}
		else if(first == "cpout")
		{
			cout << "cpout" << endl;
			string external_file, v6_file;
			// ss >> v6_file;
			v6_file = "test1";
			// ss >> external_file;
			external_file = "copied_to2";
			int inum = get_inode_number(v6_file);
			if(inum == -1)
			{
				cout << "invalid v6 file name" << endl;
			}
			else
			{
				fd2 = open(external_file.c_str(), O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
				copy_from_inode(inum);
				close(fd2);
			}
		}
		else if(first == "mkdir")
		{
			cout << "mkdir" << endl;
			ss >> dirname;
			int inum = alloc_inode();
			init_inode(inum, 1);
			// adds to root directory
			add_dir_entry(1, inum, dirname);
			add_dir_entry(inum, inum, ".");
			add_dir_entry(inum, 1, "..");
			read_contents(inum);
			read_contents(1);
		}
		else if(first == "q")
		{
			close(fd);
			cout << "quitting" << endl;
		}
		else
		{
			cout << "invalid command" << endl;
		}

		ss.clear();
	}while(command != "q");

	return 0;

}