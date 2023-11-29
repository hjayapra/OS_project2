#define FUSE_USE_VERSION 26

#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>

#include "cs1550.h"

//Root block
struct cs1550_root_directory *root;
//.disk file
FILE *disk_file;
//fields from input path 
int fields = 0; 

/**
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * `man 2 stat` will show the fields of a `struct stat` structure.
 */
static struct cs1550_directory_entry *get_dir(char directory[])
{
	//get the pointer to the struct directory entry 
	struct cs1550_directory_entry * dir = malloc(sizeof(struct cs1550_directory_entry)); 
	for(size_t i = 0; i < root->num_directories; i++)
		 {
			if(strcmp(directory, root->directories[i].dname) == 0)
			{
				
				//start from the first entry on the disk and find the directory entry using the offset 
				fseek(disk_file,((root->directories[i].n_start_block)*BLOCK_SIZE), SEEK_SET); 
				//read dir from disk
				fread(dir, BLOCK_SIZE, 1, disk_file); 
				return dir;
			}

		 }
		 return NULL; 
}
//get file
static struct cs1550_file_entry *get_file(char directory[], char filename[], char extension[])
{
	struct cs1550_directory_entry * dirEntry = get_dir(directory); 
	//if the dir is valid 
	if(dirEntry)
	{
		//get file 
		for(size_t i = 0; i < dirEntry->num_files; i++)
		{
			if(strcmp(filename, dirEntry->files[i].fname)==0)
			{
                 if(strcmp(extension, dirEntry->files[i].fext)==0)
				 { 
					struct cs1550_file_entry * fileEntry = malloc(sizeof(struct cs1550_file_entry)); 
					fileEntry = &(dirEntry->files[i]); 
					return fileEntry; 
				 }
			}
		}
	}
	//if the dirEntry is not valid 
	return NULL; 
}
//fseek and frwite to find and write to the block 
static int find_write_block(FILE *f,int offset,void*block,int size, int numEntry)
{
	fseek(f, (offset*BLOCK_SIZE), SEEK_SET); 
	fwrite(block, (size*BLOCK_SIZE), numEntry, f);
	return 0;  
}
//fseek and fread to find and read to the block
static int find_read_block(FILE * f, size_t size, int numEntry, void *block)
{
	//calculate the offset to the get to the start of the disk 
	long offset; 
	offset = size*BLOCK_SIZE; 

	fseek(f,offset, SEEK_SET);
	fread(block, BLOCK_SIZE, numEntry, f);  
	return 0; 
}
//free blocks and returns 0 upon success 
static void free_blocks(struct cs1550_directory_entry *dir, struct cs1550_file_entry *file, void*block)
{
	//if the directory, file, and block exists 
	if(dir != NULL)
		free(dir); 
	if(file != NULL)
		free(file); 
	if(block != NULL)
		free(block); 
}
//see if dir exists
static int isFound(char directory[])
{
	for(size_t i = 0; i < root->num_directories; i++)
	{
		if(strcmp(directory, root->directories[i].dname)== 0)
		    //directory exists in root 
			return 1; 
	}
	return 0; 
}
//init the root
static struct cs1550_root_directory *init_root(struct cs1550_root_directory *root)
{
	//init the root to block size 
	root = malloc(BLOCK_SIZE); 
	if(root != NULL)
		return root; 
	else 
		return NULL; 
}
static struct cs1550_root_directory *update_root(struct cs1550_root_directory *root_block)
{
	//increment the num of directories 
	root_block->num_directories++;
	//increments the last allocated block 
	root_block->last_allocated_block++; 
	return root; 
}
static int set_offset(int value)
{
	int offset = value; 
	return offset; 
}

static int cs1550_getattr(const char *path, struct stat *statbuf)
{
	// Clear out `statbuf` first -- this function initializes it.
	//en.cppreference.com (look at standard C functions)
	//bitmap seek end -neg offset to get bitmap (recitation)
	memset(statbuf, 0, sizeof(struct stat));
	//init var
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	int nFiles;
	//use the number of fields to check if input path is a dir or a file
	fields = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	// Check if the path is the root directory.
	//The root directory / will only contain other subdirectories, and no regular files.
	if(strcmp(path, "/") == 0) 
	{
		statbuf->st_mode = S_IFDIR | 0755;
		statbuf->st_nlink = 2;
		return 0;
	}
	// Check if the path is a directory 
	else if(fields == 1) 
	{
		//if the directory is not found  
		int dirExist = isFound(directory); 
		if(dirExist == 0)
		{	
			//dir is not found 
			return -ENOENT;
		}
		else
		{
			statbuf->st_mode = S_IFDIR | 0755;
			statbuf->st_nlink = 2;
			return 0; // no error
		}
	}
	// Check if the path is a file.
	else if(fields > 1) 
	{
		// Regular file
		statbuf->st_mode = S_IFREG | 0666;
		// Only one hard link to this file
		statbuf->st_nlink = 1;
		//get the directory 
		struct cs1550_directory_entry *dirEntry = get_dir(directory); 
		if(dirEntry != NULL)
		{
			//get the file 
			struct cs1550_file_entry *fileEntry = get_file(directory, filename, extension);
			if(fileEntry != NULL)
			{
				//get the size of the fileEntry 
				nFiles = fileEntry->fsize;
			}
			else
			{    
				free(dirEntry);
				return -ENOENT;
			}
		}
		else
		{   //if dir is not valid 
			return -ENOENT;
		}
		//set the size and return 0 
		statbuf->st_size = nFiles;
		free(dirEntry);
		return 0; 
	}
	else
	{
		// Otherwise, the path doesn't exist.
		return -ENOENT;
	}
}

/**
 * Called whenever the contents of a directory are desired. Could be from `ls`,
 * or could even be when a user presses TAB to perform autocompletion.
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			  off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;
	//init var
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	//scan the path and returns the number of fields ;
	fields = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	//if subdirectories exist (root) 
	if(strcmp(path, "/") == 0)
	{
		//This function should
		// list all subdirectories of the root
		// The filler function allows us to add entries to the listing.
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		for (size_t i = 0; i < root->num_directories; i++) 
		{
			filler(buf, root->directories[i].dname, NULL, 0);
		}
		return 0;
	}
	//if input path is a dir
	if(fields == 1)
	{
		// The filler function allows us to add entries to the listing.
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		//get the dir
		struct cs1550_directory_entry *dirEntry = get_dir(directory);
		//check if dir exists 
		int dirExist = isFound(directory);
		//if there is a directory list all of its files  
		if(dirExist != 0)
		{
			//list all the files
			//Initialize a char arr for the filename + extension
			char dirFile[MAX_FILENAME + MAX_EXTENSION +1];
			for (size_t i = 0; i < dirEntry->num_files; i++) 
			{
				//copy the file names to the dirFile
				strcpy(dirFile, dirEntry->files[i].fname);
				//append . and extension  
				strcat(dirFile, ".");
				strcat(dirFile, dirEntry->files[i].fext);
				//write out to the buffer 
				filler(buf, dirFile, NULL, 0);
			}
			free(dirEntry);
			return 0;	
		}
		//if dir is not found return -ENOENT 
		else
		{
			return -ENOENT;
		}
	}
	//input path is not valid (not root or directory)
	return -ENOENT; 
}

/**
 * Creates a directory. Ignore `mode` since we're not dealing with permissions.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	//compile 
	(void) mode;
	//var init
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1]; 
	int dirExists = 0; 
	//scan the input path 
	fields = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	//Check if the directory name is less than the max filename (8) 
	if(strlen(directory)> MAX_FILENAME)
		return -ENAMETOOLONG; 
	//if path is a directory 
	if (fields == 1) 
	{
		//see if the directory already exists 
		dirExists = isFound(directory); 
		if(dirExists == 1)
		    //directory already exists 
			return -EEXIST; 
        else
		{   //if there is enough space to add a new dir 
			if(root->num_directories < MAX_DIRS_IN_ROOT)
			{
				//Copy the new directory name into the root 
				strcpy(root->directories[root->num_directories].dname, directory);
				//update the starting block to the last allocated block + 1 because of a new dir 
				size_t endblock = root->last_allocated_block+1; 
				root->directories[root->num_directories].n_start_block = endblock; 
				//Increment num of dir and last alloc block
				//update directories and allocated blocks 
				root = update_root(root);  
				//init the size (1 * BLOCKSIZE) and the entry for 1 dir 
				int size = 1;
				int entry = 1;
				int result = 0;
				int offset = set_offset(0); 
				//find the start of block and write changes to the root block (seek and write)
				result = find_write_block(disk_file, offset, root, size, entry); 
				return result;
			}	
		}	
	}
	//input path is invalid 
	return -EPERM;
}


/**
 * Does the actual creation of a file. `mode` and `dev` can be ignored.
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	//compile 
	(void) mode;
	(void) dev;
	//init var 
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1]; 
	//parameters for seeking disk_file location, reading and writing
	int result = 0;  
	size_t entry = 1; 
	int offset = 0; 
	int size = 1; 
	size_t last_block; 
	//scan the input path 
	fields = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	//check if filename and extension name are greater than the MAx file and extension 
	if((strlen(filename) > MAX_FILENAME) && (strlen(extension) > MAX_EXTENSION))
		return -ENAMETOOLONG; 
	//if the input path is a file
	if(fields > 1)
	{
		//get the dir entry 
		int dirExists = isFound(directory); 
		if(dirExists == 0)
		{
			//dir does not exist already 
			return -ENOENT;
		}
		//dir entry exists 
		else
		{
			//get the dir 
			struct cs1550_directory_entry *dirEntry = get_dir(directory);
			//get the file 
			struct cs1550_file_entry *fileEntry = get_file(directory, filename, extension);
			//can't create a new file no room in the directory 
			if(dirEntry->num_files >= MAX_FILES_IN_DIR)
			{
				free(dirEntry);
				return -ENOENT;
			}
			//If the file entry is invalid (not found) and there is space in the directory -> make and add the new file 
			else if((fileEntry == NULL) && (dirEntry->num_files < MAX_FILES_IN_DIR))
			{
				//Get starting block of directory from its root 
				int dir_start = 0;
				for(size_t i = 0; i < root->num_directories; i++)
				{
					//get the n_start_block 
					if(strcmp(directory, root->directories[i].dname)==0)
					{
						dir_start = root->directories[i].n_start_block; 
					}	
				}
				//Copy file and extension into the next free file in the directory 
				strcpy(dirEntry->files[dirEntry->num_files].fname, filename);
				strcpy(dirEntry->files[dirEntry->num_files].fext, extension);
				//incr num of files in dir
				size_t fileNum = dirEntry->num_files; 
				dirEntry->num_files++;
				//init an index block and mem allocation 
				struct cs1550_index_block *index_block = malloc(sizeof(struct cs1550_index_block));
				memset(index_block, 0, sizeof(struct cs1550_index_block));

				//get the last allocated block from the root 
				last_block = root->last_allocated_block; 
				//update the file's index block to the next last allocated block on the disk 
				dirEntry->files[dirEntry->num_files].n_index_block = last_block+1;
				//find the location of the file's index block on the disk and read from the index block 
				result = find_read_block(disk_file, (dirEntry->files[fileNum+1].n_index_block), entry, index_block);
				// data alloc for block entry 
				index_block->entries[0] = dirEntry->files[dirEntry->num_files].n_index_block;  
				//Increment the last block 
				last_block++;
				
				//find and write changes back to disk and dir for new file 
				//set the offset to the start block of the dir  
				offset = set_offset(dir_start);  
				result = find_write_block(disk_file, offset, dirEntry, size, entry); 
				//seek the location of the disk by the offset and write changes to index block
				//set the offset to the last allocated block 
				offset = set_offset(last_block);
				result = find_write_block(disk_file, offset, index_block, size, entry); 
				free_blocks(dirEntry, fileEntry, index_block);
				return result;
			}
			//If the file exists return -EEXISTS
			else
			{
				return -EEXIST;
			}
		}
	}
	else
	{
		//if the input path is not a file 
		return -EPERM;
	}

}

/**
 * Read `size` bytes from file into `buf`, starting from `offset`.
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
		       struct fuse_file_info *fi)
{
	(void) path; 
	(void) buf; 
	(void) offset; 
	(void) fi; 
	size = 0;

	 //compiles 

	return size;
}

/**
 * Write `size` bytes from `buf` into file, starting from `offset`.
 */
static int cs1550_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	(void) path; 
	(void) buf; 
	(void) size; 
	(void) offset;
	(void) fi; 

	 //compiles

	return size;
}

/**
 * Called when we open a file.
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	//compile
	(void) fi;
	//init var 
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	//scan the input path 
	fields = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	//if the input path is a directory 
	if(fields == 1)
	{
		//get the dir Entry 
		int dirExists = isFound(directory); 
		if(dirExists != 0)
		{
			//dir was found 
			return 0;
		}
		else
		{
			//If the directory isn't found return an error
			return -ENOENT;
		}
	}
	if(fields > 1)
	{
		//get the dir and see if it exists 
		int dirExists = isFound(directory); 
		if(dirExists == 0)
		{
			//dir does not exist 
			return -ENOENT;
		}
		else
		{
			//get the file 
			struct cs1550_file_entry *fileEntry = get_file(directory, filename, extension);
			//If the file doesn't exist, return an error
			if(fileEntry == NULL)
			{
				return -ENOENT;
			}
			//file exists, return 0
			else
			{
				return 0;
			}
		}
	}
	// the path is invalid
	return -ENOENT;	
}

/**
 * This function should be used to open and/or initialize your `.disk` file.
 */
static void *cs1550_init(struct fuse_conn_info *fi)
{
	(void) fi;
	//Read in first disk block(root)
	root = init_root(root); 
	disk_file = fopen(".disk", "rb+");
	if (disk_file != NULL)
	{
		fread(root, BLOCK_SIZE, 1, disk_file);
	}
	return NULL;
}

/**
 * This function should be used to close the `.disk` file.
 */
static void cs1550_destroy(void *args)
{
	(void) args;
	//Free the root node and close the .disk file
	free(root);
	fclose(disk_file);
}

/**
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush(const char *path, struct fuse_file_info *fi)
{	
	(void) path;
	(void) fi;
	// Success!
	return 0;
}

/**
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
	return 0;
}

/**
 * Called when a new file is created (with a 0 size) or when an existing file
 * is made shorter. We're not handling deleting files or truncating existing
 * ones, so all we need to do here is to initialize the appropriate directory
 * entry.
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;
	return 0;
}

/**
 * Deletes a file.
 */
static int cs1550_unlink(const char *path)
{
	(void) path;
	return 0;
}

/*
 * Register our new functions as the implementations of the syscalls.
 */
static struct fuse_operations cs1550_oper = {
	.getattr	= cs1550_getattr,
	.readdir	= cs1550_readdir,
	.mkdir		= cs1550_mkdir,
	.rmdir		= cs1550_rmdir,
	.read		= cs1550_read,
	.write		= cs1550_write,
	.mknod		= cs1550_mknod,
	.unlink		= cs1550_unlink,
	.truncate	= cs1550_truncate,
	.flush		= cs1550_flush,
	.open		= cs1550_open,
	.init		= cs1550_init,
	.destroy	= cs1550_destroy,
};

/*
 * Don't change this.
 */
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &cs1550_oper, NULL);
}





