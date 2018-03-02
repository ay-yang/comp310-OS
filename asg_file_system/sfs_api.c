#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include "disk_emu.h"
#include "tests.h"

#define BLOCK_SIZE 1024
#define BLOCK_NUM 1024	
#define ROOT_NAME "THE LAST OF THE ASG"
#define INODE_NUM 200
#define NAME_LENGTH 10
#define FILE_CAP 199
#define DIR_POINTERS 14
#define IND_PTR_CAP  BLOCK_SIZE/sizeof(int)

/** Yue Yang
* Comp310 asg3
**/

//defines an i-node
typedef struct _inode_t{
	int size; 
	int direct[14];
	int indirect;
}inode_t;

//defines a superblock
typedef struct _superblock_t{
	unsigned char magic[4];
	int bsize;
	int inode_count;

	inode_t root;
	inode_t shadow[4];
	int last_shadow;
}superblock_t;

//defines a regular block
typedef struct _block{
	unsigned char bytes[BLOCK_SIZE];
}block_t;

typedef struct _dir_entry{
	char filename[NAME_LENGTH];
	int i_node;
}dir_entry;
//defines basic information of a file
typedef struct _file{
	char filename[NAME_LENGTH];
	int readPtr;
	int writePtr;
	int i_node;
}file;

superblock_t superblock;
block_t fbm; //1 indicates unused, 0 indicates used
inode_t j_node;
inode_t inode_file[INODE_NUM]; //to keep track of the 200 inodes

//This acts mostly as a temporary variable 
int nodesSize = (sizeof(inode_t)*FILE_CAP + BLOCK_SIZE)/BLOCK_SIZE;
int indirect_pointers[IND_PTR_CAP]; //indicates how many indirect pointers a block can hold

dir_entry root[FILE_CAP]; //the root directory, containing the filename and their i-node
file fd_table[FILE_CAP]; //the table that hold open files

/**This is a helper function that finds and returns the first free block
**/
int get_free_block(int occupy){

	for (int i =0; i < BLOCK_SIZE; i++){
		if (fbm.bytes[i] == 0){
			if(occupy == 1){
				fbm.bytes[i] = 1; //to indicate this block is now being used
				write_blocks(1023, 1, &fbm); //save changes of fbm into memory
			}
			return i;
		}
	}
	return -1; // no free blocks found
}

int init_indirect_pointers(){
	for(int j=0; j < (int)BLOCK_SIZE/sizeof(int); j++){
		indirect_pointers[j] = -1;
	}
	return 0; 
}

/**Creates the file entire system (root)or opens it, 
Initializes:
all fbm entries to 0
superblock and its attributes
j_node(the root node) and its attributes
root directory and fd_table's i_node field to -1 to indicate they are free

Then writes the superblock, inode_file, root(directory), fmb and wm to disk
**/
void mkssfs(int fresh){

	for (int i = 0; i < FILE_CAP; i++){
		fd_table[i].i_node = -1; //initializes fd_table
	}
	init_indirect_pointers();

	if(fresh == 1){ //if flag is true, create fs
		init_fresh_disk(ROOT_NAME, BLOCK_SIZE, BLOCK_NUM); //the root

		//initialize file with the i-nodes
		for(int j =0; j < INODE_NUM; j++){
			inode_file[j].size = -1;
		}

		//initialize fbm, wm 
		for (int i = 0; i < BLOCK_SIZE; i++){
			fbm.bytes[i] = 0; //all blocks are free by default
		}

		//initialize super block
		superblock.bsize = BLOCK_SIZE;
		//strcpy(superblock.magic, 0xACBD0005);
		superblock.root = j_node;
		superblock.inode_count = INODE_NUM; //here we have 200 i-nodes

		//initialize root node
		j_node.size = 64; 
		j_node.direct[0] = 1; //points to first data block, root directory
		inode_file[0] = j_node; //adds j-node to the list of nodes
		inode_file[0].size = 0; //indicates this inode is no longer free

		//initialize root directory and fd table
		for(int k =0; k <199; k++){
			root[k].i_node =-1; //there are no i-nodes in the root directory initially
			fd_table[k].i_node =-1; 
		}

		//write to blocks
		write_blocks(0, 1, &superblock);
		write_blocks(1, 4, root);
		write_blocks(5, nodesSize, inode_file);
		write_blocks(1023, 1, &fbm); //fbm goes to the before last block of the fs
		
		//reserved spots 
		fbm.bytes[1023] = 1;

		for(int i=0; i <nodesSize+5; i++)
			fbm.bytes[i] = 1; 
	}
	else{ //else, opens it
		init_disk(ROOT_NAME, BLOCK_SIZE, BLOCK_NUM);

		//opens the fbm, superblock, root directory from previously initiated data
		read_blocks(0, 1, &superblock);
		read_blocks(1, 4, root);
		read_blocks(5, nodesSize, inode_file);

		read_blocks(1023, 1, &fbm);
	}
}

/**This function returns the first free node in the inode file
and then marks it as occupied
@returns -1 if no such index found
@return count the index of the free inode if found**/
int get_free_inode(){
	for(int count = 0; count < INODE_NUM; count++){
		if(inode_file[count].size == -1){
			inode_file[count].size = 0;

			//initializes the i-node pointers to -1
			for(int i =0; i < 14; i++)
				inode_file[count].direct[i] = -1;
			inode_file[count].indirect = -1;
			return count;
		}
	}
	return -1; //no free nodes found
}

/**This function resets the fd_table's filename at the desired index to null characters
**/
void freename(int tableIndex){  
	for(int i =0; i < NAME_LENGTH; i++)
		fd_table[tableIndex].filename[i] = '\0';
}

/**
This looks for a specific file from the root directory and adds it to the fd_table
If such file is not found, it will attempt to create one at its name in the root directory 
@ return the fd index on the fd_table or -1 if not found and all indexes are occupied
**/
int ssfs_fopen(char *name){ 

	int free_fd = -1;
	int found = -1;

	//finds first free fd entry in the fd table
	for (int f_count =0; f_count < FILE_CAP; f_count++){

		//if file was previously opened, return its slot on the fd table
		if(strcmp(fd_table[f_count].filename, name) ==0){
			return f_count; 
		}
		if(fd_table[f_count].i_node == -1){ //else keeps track of the next free space in the table 
			free_fd = f_count; 
			f_count = INODE_NUM; // to stop loop
		}
	}

	//else go through all entries in root to look for the file and add it to fd table
	if(free_fd != -1){
		for(int i_count =0; i_count < FILE_CAP; i_count++){
			if (strcmp(root[i_count].filename, name) == 0){
				//entry found, copies it into the fd table
				freename(free_fd); //so we don't get garbage values in the name
				strcpy(fd_table[free_fd].filename,name);
				fd_table[free_fd].i_node = root[i_count].i_node;
				fd_table[free_fd].readPtr = inode_file[root[i_count].i_node].direct[0]*BLOCK_SIZE;
				fd_table[free_fd].writePtr = inode_file[root[i_count].i_node].direct[0]*BLOCK_SIZE;
				found =1; 
			}
		}
		//file not found, create a new empty file with its name	
		if(found == -1){
			int availNode = get_free_inode();
			int availBlock =get_free_block(1);

			if(availBlock != -1)
				inode_file[availNode].direct[0] = availBlock; //gives it a free block
			else return -1; 
			//find a free inode
			if(availNode == -1) //free node not found
				return -1;
			//Now updates the root directory
			for(int i_count = 0; i_count < FILE_CAP; i_count++){
				if(root[i_count].i_node == -1){
					strcpy(root[i_count].filename, name);
					root[i_count].i_node = availNode;
					i_count = FILE_CAP;
				}
			}
			//and opens this file 
			strcpy(fd_table[free_fd].filename, name);
			fd_table[free_fd].i_node = availNode;
			fd_table[free_fd].readPtr = availBlock*BLOCK_SIZE;
			fd_table[free_fd].writePtr = availBlock*BLOCK_SIZE; 		
		}
	}
	//update the disk
	write_blocks(1,4,root);
	write_blocks(5, nodesSize, inode_file);	
	return free_fd; 
}

/**
Removes the requested fileID from the fd_table
Note: will do nothing if the file is not open. 
@return 0: success or did nothing
@return -1: invalid input
**/
int ssfs_fclose(int fileID){
	//removes it from the fd table
	if(fileID < 0 || fileID > 198 ||fd_table[fileID].i_node == -1)
		return -1;	
	
	else{
		fd_table[fileID].i_node = -1;
		freename(fileID);
		return 0;
	}
}

/**This function first validates fileID and loc.
If both are valid, then loc will becomes the fileID's new readPtr in the fd table
@return 0: success
@return -1: failure
**/
int ssfs_frseek(int fileID, int loc){

	int block;
	int inode;
	int blockFound = -1; 
	int temp; 

	printf("READS ");
	printf("loc: %d ", loc);

	if (fileID > 198 || fileID < 0 || loc < 0){ 
		//printf("size: %d ", inode_file[inode].size);
		return -1;
	}

	inode = fd_table[fileID].i_node;

	if(inode == -1) //this spot on the fd table was not yet allocated
		return -1;

	printf("file size: %d \n",inode_file[inode].size  );
	if(inode_file[inode].size < loc)
		return -1;

	else{ 
		block = loc/BLOCK_NUM; //block this pointer goes to

		//find ze inode of the block
		if( block < 14){ //this means we should look in the direct blocks 
			if(inode_file[inode].direct[block] != -1){
				temp = inode_file[inode].direct[block]; //note: use of temp here is to prevent weird values 
				fd_table[fileID].readPtr = temp*BLOCK_SIZE + loc%BLOCK_SIZE;
				blockFound =1;
			}
		}
		if(block >= 14){ //this indicates we should look in the indirect blocks 
			block = block - 14; 
			if(inode_file[inode].indirect != -1){
				read_blocks(inode_file[inode].indirect, 1, indirect_pointers);
				if(indirect_pointers[block] != -1){
					temp = indirect_pointers[block]; 
					fd_table[fileID].readPtr = temp*BLOCK_SIZE + loc%BLOCK_SIZE;
					blockFound = 1;
				}
				init_indirect_pointers();
			}
		}

	}
	return blockFound; 
}

/**This function first validates fileID and loc.
If both are valid, then loc will becomes the fileID's new writePtr in the fd table
@return 0: success
@return -1: failure
**/
int ssfs_fwseek(int fileID, int loc){

	int block;
	int inode;
	int blockFound = -1; 
	int temp; 
	int freeBlock = -1; 

	printf("READS ");
	printf("loc: %d ", loc);

	if (fileID > 198 || fileID < 0 || loc < 0){ 
		return -1;
	}
	inode = fd_table[fileID].i_node;

	if(inode == -1) //this spot on the fd table was not yet allocated
		return -1;

	else{ 
		block = loc/BLOCK_NUM; //block this pointer goes to

		if (loc == inode_file[inode].size + 1){//find a new block
			freeBlock = get_free_block(1);
		}

		//find ze inode of the block
		if( block < 14){ //if the loc is a direct block
			if(inode_file[inode].direct[block] != -1){
				temp = inode_file[inode].direct[block]; //note: use of temp here is to prevent weird values 
				fd_table[fileID].writePtr = temp*BLOCK_SIZE + loc%BLOCK_SIZE;
				blockFound =1;
			}
			else if(inode_file[inode].direct[block] == -1){ //since this is a write pointer, we may initialize it to next free block
				if(freeBlock != -1){
					inode_file[inode].direct[block] = freeBlock;
					fd_table[fileID].writePtr = freeBlock*BLOCK_SIZE;
					blockFound = 1;
				}
			}
		}
		if(block >= 14){//else the loc should be one of the indirect blocks
			block = block - 14; 
			if(inode_file[inode].indirect != -1){
				read_blocks(inode_file[inode].indirect, 1, indirect_pointers);
				if(indirect_pointers[block] != -1){
					temp = indirect_pointers[block]; 
					fd_table[fileID].writePtr = temp*BLOCK_SIZE + loc%BLOCK_SIZE;
					blockFound = 1;
				}
				else if (indirect_pointers[block] == -1 && freeBlock != -1){ 
					indirect_pointers[block] = freeBlock; 
					fd_table[fileID].writePtr = freeBlock*BLOCK_SIZE;
					write_blocks(inode_file[inode].indirect, 1, indirect_pointers); //update the indirect block in disk
					blockFound = 1;
				}
				init_indirect_pointers();
			}
		}
	}
	return blockFound; 
}

/**
This writes extra data into blocks 
This is a recursive implementation. It tries to write at most one full block at each iteration
and keeps doing so until all data in buf is written into disk 
@return: the length written into disk
**/
int ssfs_fwrite(int fileID, char *buf, int length){
	int startBlockPtr; //the starting block
	int start; //the staring position in the block
	int write_length = 0; //...
	int inode; 
	int overlap; //the difference between the start position and the data length
	int remain;
	int numBlocks;

	int found = -1; //for when we have to look for another block, when write overlaps into more than one block
	int first_ind = -1;
	int temp; 
	int freeBlock; //in case we need to find a free block 
	int freeBlock2;
	char data[BLOCK_SIZE+1]; // the buffer we read data into, if any 
	int index = 0;

	if(fileID < 0 || fileID > 198)
		return -1; 

	if(length <= 0 || strlen(buf) == 0)
		return 0; //note: since this is a recursive implementation, returning a 0 is alot better than returning -1

	//numBlocks = length/BLOCK_SIZE;
	memset(data, '\0', BLOCK_SIZE+1);

	startBlockPtr = fd_table[fileID].writePtr/BLOCK_SIZE;
	inode = fd_table[fileID].i_node;
	start = fd_table[fileID].writePtr%BLOCK_SIZE;

	if(length > BLOCK_SIZE-fd_table[fileID].writePtr%BLOCK_SIZE){//if we are going to overlap over more than 1 block
		//check if we have enough space
		freeBlock = get_free_block(-1);
		remain = 1023-freeBlock-1; //remaining free blocks
		//required number of blocks
		numBlocks = (length - (BLOCK_SIZE-fd_table[fileID].writePtr%BLOCK_SIZE) + BLOCK_SIZE)/BLOCK_SIZE;

		if (numBlocks + 1 > remain){ //not enough blocks left, you aren't fooling me
			return -1;
		}
	}

	read_blocks(startBlockPtr, 1, data);

	while(data[index] != '\0'){
		index++; 
	}
	overlap = index - start; //calculates how much data we overwrite 

	if(overlap > 0 && length < overlap)
		overlap = length;

	//we either write until the end of block, or until length, if length < remaining space in current block
	if(length < BLOCK_SIZE-start){
		memcpy(&data[start], buf, length);
		write_length = length;
	}

	else{
		memcpy(&data[start], buf, BLOCK_SIZE-start);
		write_length = BLOCK_SIZE-start; 
	}

	//if we overwrote previou data, substract the amount off the write_length 
	if(overlap > 0) 
		inode_file[inode].size = inode_file[inode].size + write_length -overlap;
	
	else 
		inode_file[inode].size = inode_file[inode].size + write_length;
	
	fd_table[fileID].writePtr = fd_table[fileID].writePtr + write_length;

	//if the write pointer goes beyond current block, we need to alocate another block
	if(fd_table[fileID].writePtr/BLOCK_SIZE != startBlockPtr){//we must find a new free block
			//update the direct inodes, if places remain
			for(int i =0; i < 14; i++){
				if(inode_file[inode].direct[i] == startBlockPtr){
					if(i+1 < 14){
						if(inode_file[inode].direct[i+1] == -1){ //in the case we need a new block
							freeBlock = get_free_block(1);
							found = 1;
							inode_file[inode].direct[i+1] = freeBlock; 
							fd_table[fileID].writePtr = freeBlock*BLOCK_SIZE;
							i = 14;
						}
						else{ //else, we move the pointer over to the next block following the current block, startBlockPtr 
							found = 1;
							temp = inode_file[inode].direct[i+1];
							fd_table[fileID].writePtr= temp*BLOCK_SIZE;
						}
					}
					if(i+1 == 14)
						first_ind = 1; 

				}
			}

			//update the indirect block, if no place in the direct pointers remain
			if(found == -1){
				//indirect blocks
				if(inode_file[inode].indirect == -1){ //if this is the first time we use indirect blocks
					freeBlock = get_free_block(1); //this is the new block for more data 
					freeBlock2= get_free_block(1); //this is the block the indirect pointer goes 
					if(freeBlock2 != -1){
						inode_file[inode].indirect = freeBlock2;
						indirect_pointers[0] = freeBlock; 
						write_blocks(freeBlock2, 1, indirect_pointers);
						init_indirect_pointers(); 
						found = 1;
						fd_table[fileID].writePtr = freeBlock*BLOCK_SIZE;
					}
				}
				else { //look for a free spot in the existing indirect pointer table
					init_indirect_pointers(); 
					read_blocks(inode_file[inode].indirect, 1, indirect_pointers);
					if(first_ind != -1){ //if we are looking at the first indirect pointer 
						found =1;
						temp = indirect_pointers[0]; 
						fd_table[fileID].writePtr = temp*BLOCK_SIZE;
					}
					else{ //else, must look through all the indirect pointer until we find the position of the current block, startBlockPtr 
						for(int i =0; i <IND_PTR_CAP; i++){
							if(indirect_pointers[i] == startBlockPtr){
								if(i+1 < IND_PTR_CAP){
									if(indirect_pointers[i+1] == -1){ //in the case the next spot in the pointer table is unoccupied, 
												 //we initialize it to a new block 
										freeBlock = get_free_block(1);
										found =1;
										if(freeBlock != -1){
											indirect_pointers[i+1] = freeBlock; 
											fd_table[fileID].writePtr = freeBlock*BLOCK_SIZE;
										}
									}
									else{ //if the next entry exits already, update the write Pointer to it 
										found =1;
										temp = indirect_pointers[i+1];
										fd_table[fileID].writePtr = temp*BLOCK_SIZE;
									}
								}
								i = IND_PTR_CAP;
							}
						}
					}
					write_blocks(inode_file[inode].indirect, 1, indirect_pointers);
					init_indirect_pointers(); 
				}
			}
			if(found == -1){ //if no space is found, simply resets the pointer towards the first spot
				fd_table[fileID].writePtr = inode_file[inode].direct[0]*BLOCK_SIZE; //else reset the pointer towards the beginning
				return write_length;
			}
	}

	if(write_length < length){ //to ensure we keep writing until something is exhausted 
		write_length =  write_length + ssfs_fwrite(fileID, buf+write_length, length - write_length);
	}

	//this check basically proves that the requested length could not be satisfied, due to lack of space 
	if(write_length != length) //this is the statement that saved me 2000+ errors in test 2 :) 
		//I call it the nightmare test, aka test_write_to_overflow
		//unfortunately, it is not full proof :/ 
		return 0; 

	//to ensure that we only write to blocks when all previous checks passed
	write_blocks(startBlockPtr, 1, data);
	write_blocks(5, nodesSize, inode_file);

	return write_length;
}

/**reads characters into buf
This is recursive implementation, at each iteration, it tries to read up to one full block
and so on, until the read length is satisfied (or cannot be satisfied)
@return: the length read
**/
int ssfs_fread(int fileID, char *buf, int length){
	int startBlockPtr; //the first block we read from
	int inode; 

	int blockFound = -1; //for when read has to overlap into another block
	int first_ind = -1;
	int start; //the starting position in the block
	int read_length = 0; //self-explanatory
	char data[BLOCK_SIZE+1]; //a buffer that temporarily stores data we read

	if (fileID < 0 || fileID > 198 || fd_table[fileID].i_node == -1 )
		return -1;

	if(length <= 0)
		return 0;
	memset(data, '\0', BLOCK_SIZE+1);
	start = fd_table[fileID].readPtr%BLOCK_SIZE;
	inode = fd_table[fileID].i_node;

	startBlockPtr = fd_table[fileID].readPtr/BLOCK_SIZE;

	read_blocks(startBlockPtr, 1, data);

	//we only copy data until the end of current block into buffer, or until length, whichever one is smaller 
	if(length < BLOCK_SIZE-start){
		memcpy(buf, &data[fd_table[fileID].readPtr%BLOCK_SIZE], length);
		read_length = length;
	}
	else{
		memcpy(buf, &data[fd_table[fileID].readPtr%BLOCK_SIZE], BLOCK_SIZE-start);
		read_length = BLOCK_SIZE-start; 
	}

	fd_table[fileID].readPtr = fd_table[fileID].readPtr + read_length; 

	if(fd_table[fileID].readPtr/BLOCK_SIZE != startBlockPtr){
		//we need to find the next block for this pointer
		for(int i =0; i < 14; i++){ //first look in the direct pointers
			if(inode_file[inode].direct[i] == startBlockPtr){
				if(i+1 < 14 && inode_file[inode].direct[i+1] != -1){
					fd_table[fileID].readPtr = inode_file[inode].direct[i+1]*BLOCK_SIZE;
					i = 14; 
					blockFound = 1; 
				}
				if(i+1 ==14){ //indicates that we will need the first indirect pointer
					first_ind = 1;
				}
			}
		}
		if(blockFound == -1){//if not found in the direct pointers, will look in the indirect pointers
				if(inode_file[inode].indirect != -1){
					read_blocks(inode_file[inode].indirect, 1, indirect_pointers);
					if(first_ind == 1){
						fd_table[fileID].readPtr = indirect_pointers[0]*BLOCK_SIZE;
						blockFound = 1;
					}
					else {
						for(int i = 0; i <IND_PTR_CAP; i++){
							if(indirect_pointers[i] == startBlockPtr){
								if(i+1 < IND_PTR_CAP && indirect_pointers[i+1] != -1){
									int temp = indirect_pointers[i+1];
									blockFound = 1;
									i = IND_PTR_CAP; //to stop loop 
									fd_table[fileID].readPtr = temp*BLOCK_SIZE; //updates the read pointer
								}
							}
						}
					}
					init_indirect_pointers(); //clears indirect pointers
				}
		}
		if(blockFound == -1){
				fd_table[fileID].readPtr = inode_file[inode].direct[0]*BLOCK_SIZE; //resets read pointer
				return read_length; //this may indicate that we have read as much as we could
		}
	}

	//keep on reading if we did not reach the desired length 
	if(read_length < length){
		read_length = read_length + ssfs_fread(fileID, buf+read_length, length-read_length);
		if(read_length > inode_file[inode].size) //this is to fix the read length, in case we added up too much
			read_length = inode_file[inode].size; 
	}
	
	return read_length; 
}

/**
Will remove the requested file from the root directory, 
and clear all of its occupied blocks, and clear its inode. 
Note: closes file as well, if it is open 
**/
int ssfs_remove(char *file){
	int found = -1;
	int inode; 
	char dummy[BLOCK_SIZE];

	memset(dummy, '\0', BLOCK_SIZE); //this is to reinitialize the disk

	for (int i = 0; i < FILE_CAP; i++){
		//find its location int the root directory
		if(strcmp(root[i].filename, file) == 0){
			inode = root[i].i_node;
			root[i].i_node = -1;

			inode_file[inode].size = -1;

			//remove all blocks ,free fbm
			for(int i= 0; i < 14; i++){
				if(inode_file[inode].direct[i] != -1){
					write_blocks(inode_file[inode].direct[i], 1, dummy);
					fbm.bytes[inode_file[inode].direct[i]] = 0; 
					inode_file[inode].direct[i] = -1;
				}
			}
			//look in the indirect pointers, too
			if(inode_file[inode].indirect != -1){
				read_blocks(inode_file[inode].indirect, 1, indirect_pointers);
				fbm.bytes[inode_file[inode].indirect ] = 0;
				for(int i = 0; i < IND_PTR_CAP; i++){
					if(indirect_pointers[i] != -1){
						write_blocks(indirect_pointers[i], 1, dummy);
						fbm.bytes[indirect_pointers[i]] = 0;
					}
				}
				init_indirect_pointers();
				write_blocks(inode_file[inode].indirect, 1, dummy);
			}

			//resets the name of the file in the root
			for(int k = 0; k < NAME_LENGTH; k++)
				root[i].filename[k] = '\0'; 
			
			found = 1;
		}
		//attempt to close the file 
		//find its location int the fd table, and closes it if it has been opened previously
		if(strcmp(fd_table[i].filename, file) == 0){
			ssfs_fclose(i);
		}
	}
	//update the disk
	write_blocks(5, nodesSize, inode_file);
	write_blocks(1,4, root);
	write_blocks(1023, 1, &fbm);
	return found;
}