#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

/* make sure to use syserror() when a system call fails. see common.h */

void
usage()
{
	fprintf(stderr, "Usage: cpr srcdir dstdir\n");
	exit(1);
}

//the function that copies the file from pathName to destName, return 1 for fail, 0 for success
int file_cp (const char* pathName, const char* destName){
    
    //open the source file , flag is read only
    int fd, flags = O_RDONLY;
    fd = open(pathName, flags);
    if(fd < 0){
        syserror(open, pathName);
    }
    
    //open the destination file
    int destFd = open(destName, O_CREAT);
    if(destFd < 0){
        syserror(creat, destName);
    }
    
    //read the source file and write to destination
    int readNum = 1;
    while(readNum != 0){      
        //read the source file
        char buf[4096];
        readNum = read(fd, buf, 4096);
        //write to destination
        int size_written = write(destFd, buf, 4096);
        if(size_written != readNum){
            printf("Not correctly copied\n");
        }
    }
    
    //close two files
    int close1 = close(fd);
    if(close1 < 0){
        syserror(close, pathName);
    }
    int close2 = close(destFd);
    if(close2 < 0){
        syserror(close, destName);
    }
    
    return 0;
}

void recursive_cp(const char* sourceName, const char* destName){
    //need to copy the permission of the source dir or file to the destination
    struct stat buffer;
    int stat_res = stat(sourceName, &buffer);
    if(stat_res != 0){
        syserror(stat, sourceName);
    }
    //if currently is a dir
    if(S_ISDIR(buffer.st_mode)){
        //recursive_cp()
        //now everything works on a dir
        DIR* dir = opendir(sourceName);
        struct dirent* Dirent = NULL;
        while((Dirent = readdir(dir)) != NULL){
            //check if this is file or dir
            struct stat buf;
            //first figure out its path name and the destination new path name
            char new_path[100];
            strcpy(new_path, sourceName);
            strcat(new_path, "/\0");
            strcat(new_path, Dirent->d_name);
            char new_dest[100];
            strcpy(new_dest, destName);
            strcat(new_dest, "/\0");
            strcat(new_dest, Dirent->d_name);
            
            stat(new_path, &buf);
            //if this one is a dir
            if(S_ISDIR(buf.st_mode)){
                //create this new dir in dest
                //first get the permission of the old dir an it is just in buf
                mkdir(new_dest, buf.st_mode);
                //recursive call on this dir
                recursive_cp(new_path, new_dest);
            //if this one if a file
            }else if(S_ISREG(buf.st_mode)){
                int successful = file_cp(new_path, new_dest);
                if(successful == 1){
                    printf("fail to copy file at %s", new_path);
                }
                //now copy the permission
            }
        }
    }
    
    
    
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		usage();
	}
	//TBD();
        const char* source_name = argv[1];
        const char* dest_name = argv[2];
        recursive_cp(source_name, dest_name);
        
	return 0;
}
