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

int isize, fsize, nfree, ninode, fd, fd2, buf, temp;
int free_blocks[100]; // free is a keyword
int free_inodes[100];
int block_size = 2048;

int get_offset(int block, int offset=0)
{
	return (isize + block) * block_size + offset;
}

int alloc_block()
{
	nfree--;
	if(nfree > 0)
	{
		return free_blocks[nfree];
	}
	int current = free_blocks[0];
	lseek(fd, (isize+free_blocks[0])*block_size, SEEK_SET);
	read(fd, &nfree, sizeof(nfree));
	for(int i=0; i < nfree; i++)
	{
		read(fd, &buf, sizeof(buf));
		free_blocks[i] = buf;
	}

	return current;
}

struct inode
{
	bool allocated;
	bool plain;
	bool directory;
	bool large;
	int size;
	unsigned short addr[62];
} initial, check;

struct dir_entry
{
	unsigned short inode_num;
	char name[14];
};

void inode_list()
{
	lseek(fd, 0, SEEK_SET);
	int counter = 0;
	inode current;
	for(int i = 0; i < 16 * isize; i ++) // 16 = 2048 (block size) / 128 (inode size)
	{
		if(counter >= 100)
		{
			break;
		}
		read(fd, &current, sizeof(current));
		if(!current.allocated)
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
	inode_list();
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
	lseek(fd, (dir - 1) * 128, SEEK_SET);
	read(fd, &current, sizeof(current));
	if(current.size % block_size == 0)
	{
		current.addr[current.size / block_size] = alloc_block();
	}
	lseek(fd, get_offset(current.addr[current.size / block_size], current.size % block_size), SEEK_SET);
	write(fd, &entry, sizeof(entry));

	current.size+=16;
	lseek(fd, (dir - 1) * 128, SEEK_SET);
	write(fd, &current, sizeof(current));
}

void inode_allocated(int dir, int flag)
{
	inode current;
	lseek(fd, (dir - 1) * 128, SEEK_SET);
	read(fd, &current, sizeof(current));
	current.allocated = true;
	// set others to false here? not really needed since we don't have to worry about deletion...	
	switch(flag)
	{
		case 1: current.directory = true;
				break;
		case 2: current.plain = true;
				break;
		case 3: current.large = true;
		cout << "setting to large" << endl;
				break;
	}
	lseek(fd, (dir - 1) * 128, SEEK_SET);
	write(fd, &current, sizeof(current));
}

void read_contents(int inum)
{
	inode current;
	lseek(fd, (inum - 1) * 128, SEEK_SET);
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
	lseek(fd, (inum - 1) * 128, SEEK_SET);
	read(fd, &current, sizeof(current));
	char buffer[block_size];
	int nread, position;
	current.size = size;
	lseek(fd2, 0, SEEK_SET);
	position = 0;
	if(current.plain)
	{
		while((nread=read(fd2, buffer, sizeof(buffer))) > 0)
		{
			current.addr[position / block_size] = alloc_block();
			lseek(fd, get_offset(current.addr[position / block_size]), SEEK_SET);
			write(fd, buffer, sizeof(buffer));
			position+=nread;
		}
	}
	else if(current.large)
	{
		int counter;
		unsigned short new_block;

		for(int i = 0; i < size / (block_size * block_size / 2)+1; i++)
		{
			counter = 0;
			current.addr[i] = alloc_block();
			while(counter < 1024 && (nread=read(fd2, buffer, sizeof(buffer))) > 0)
			{
				new_block = alloc_block();
				lseek(fd, get_offset(current.addr[i], 2*counter), SEEK_SET);
				write(fd, &new_block, sizeof(new_block));
				lseek(fd, get_offset(new_block), SEEK_SET);
				write(fd, buffer, sizeof(buffer));
				counter++;
			}
		}

	}
	lseek(fd, (inum - 1) * 128, SEEK_SET);
	write(fd, &current, sizeof(current));
}

void copy_from_inode(int inum)
{
	inode current;
	lseek(fd, (inum - 1) * 128, SEEK_SET);
	read(fd, &current, sizeof(current));
	char buffer[block_size];
	int nread;
	lseek(fd2, 0, SEEK_SET);
	if(current.plain)
	{
		// gets full blocks
		int i;
		for(i = 0; i < current.size / block_size; i++)
		{
			lseek(fd, get_offset(current.addr[i]), SEEK_SET);
			nread = read(fd, buffer, sizeof(buffer));
			write(fd2, buffer, nread);
		}
		// gets remaining
		lseek(fd, get_offset(current.addr[i]), SEEK_SET);
		nread = read(fd, buffer, current.size % block_size);
		write(fd2, buffer, nread);
	}
	else if(current.large)
	{
		int counter,counter2, last_block;
		last_block = current.size / block_size;
		unsigned short second_block;
		counter2 = 0;
		for(int i = 0; i < current.size / (block_size * block_size / 2)+1; i++)
		{
			counter = 0;
			current.addr[i] = alloc_block();
			while(counter < 1024)
			{
				lseek(fd, get_offset(current.addr[i], 2*counter), SEEK_SET);
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
	lseek(fd, 0, SEEK_SET);
	read(fd, &root, sizeof(root));

	for(int i = 0; i < root.size / 16; i++)
	{
		// check if 128 is right??
		if(i % 128 == 0)
		{
			lseek(fd, get_offset(root.addr[i / 128]), SEEK_SET);
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
	off_t ret;
	


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
			// init free blocks
			for(int i = 0; i < (fsize - isize - 1) / 100; i++)
			{
				ret = lseek(fd, (isize+100*(i+1))*block_size, SEEK_SET);
				if(ret == (off_t) -1)
				{
					//error handle here
				}
				buf = 100;
				write(fd, &buf, sizeof(buf));
				for(int j = 0; j < 100; j++)
				{
					buf = 100 * i + j;
					write(fd, &buf, sizeof(buf));
				}	
			}
			nfree = (fsize - isize - 1) % 100 + 1;
			temp = (fsize - isize - 1) / 100;
			temp *= 100;
			for(int j = 0; j < nfree; j++)
			{
				free_blocks[j] = temp + j;
			}

			// init inodes
			lseek(fd, 0, SEEK_SET);
			initial.allocated = true;
			initial.plain = false;
			initial.directory = true;
			initial.large = false;
			initial.size = 0; 
			for(int i = 0; i < 62; i++)
				initial.addr[i] = 0;

			write(fd, &initial, sizeof(initial));
			// add_dir_entry(inode_num, inode_to_add_num, file_name);
			add_dir_entry(1, 1, ".");
			add_dir_entry(1, 1, "..");
			read_contents(1);

			// init other inodes

			initial.allocated = false;
			initial.directory = false;

			for(int i = 1; i < 16 * isize; i++)
			{
				write(fd, &initial, sizeof(initial));
			}
			// find free inodes and put into inode list
			inode_list();
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
			int inum = alloc_inode();
			// allocates and tells if large file or not
			inode_allocated(inum, (size < 62 * block_size ) ? 2 : 3);
			add_dir_entry(1, inum, v6_file);
			copy_to_inode(inum, size);
			//check if large later

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
				fd2 = open(external_file.c_str(), O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
				copy_from_inode(inum);
				close(fd2);
			}
		}
		else if(first == "mkdir")
		{
			cout << "mkdir" << endl;
			ss >> dirname;
			int inum = alloc_inode();
			inode_allocated(inum, 1);
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
