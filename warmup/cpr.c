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
int file_cp (char* pathName, char* destName){
    
    //open the source file , flag is read only
    int fd;
    fd = open(pathName, O_RDONLY);
    if(fd < 0){
        syserror(open, pathName);
    }
    
    //open the destination file
    int destFd = open(destName, O_RDWR|O_CREAT);
    if(destFd < 0){
        syserror(creat, destName);
    }
    
    //copy permission
    struct stat buffer;
    stat(pathName, &buffer);
    
    chmod(destName, buffer.st_mode);
    
    //read the source file and write to destination
    char buf[1024];
    int readNum;
    while((readNum = read(fd, buf, sizeof(buf)))){      
        //read the source file
        //readNum = read(fd, buf, 4096);
        //write to destination
        write(destFd, buf, readNum);
        /*if(size_written != readNum){
            printf("Not correctly copied\n");
        }*/
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

char* str_contact(char* string1, char* string2){
    char* output;
    output = (char*)malloc(strlen(string1) + strlen(string2) + 1);
    strcat(output, string1);
    strcat(output, string2);
    return output;
}

int checkDir(char* pathName){
    struct stat checker;
    stat(pathName, &checker);
    
    if(S_ISDIR(checker.st_mode)){
        return 1;
    }else{
        return 0;
    }
}

void recursive_cp(char* sourceName, char* destName){
        //need to copy the permission of the source dir or file to the destination
        struct stat buffer;
        stat(sourceName, &buffer);
        //printf("%x", buffer.st_mode);
        
        //copy the home dir
        int make_success = mkdir(destName, 0777);
        if(make_success == -1){
            syserror(mkdir, destName);
            //exit(1);
        }
        
        //chmod(destName, buffer.st_mode);
        
        //now everything works on a dir
        DIR* dir = opendir(sourceName);
        if(dir == NULL){
            syserror(opendir, sourceName);
        }
        struct dirent* Dirent;
        
        char new_path[4096];
        char new_dest[4096];
        while((Dirent = readdir(dir))){
        
            bzero(new_path, sizeof(new_path));
            bzero(new_dest, sizeof(new_dest));
            sprintf(new_path, "%s/%s", sourceName, Dirent->d_name);
            sprintf(new_dest, "%s/%s", destName, Dirent->d_name);
            
            //if this one is a dir
            if(checkDir(new_path) && (new_path[strlen(new_path)-1] != '.')){
                //recursive call on this dir
                recursive_cp(new_path, new_dest);
            //if this one if a file
            }else if(!checkDir(new_path) && (new_path[strlen(new_path)-1] != '.')){
                file_cp(new_path, new_dest);
                /*if(successful == 1){
                    printf("fail to copy file at %s", new_path);
                }*/
                //now copy the permission
            }
            //free(new_path);
            //free(new_dest);
        }
        
        chmod(destName, buffer.st_mode);
        //free(path);
        closedir(dir);
    
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		usage();
	}

        char* source_name = argv[1];
        //printf("%s", source_name);
        char* dest_name = argv[2];
        //printf("%s", dest_name);
        recursive_cp(source_name, dest_name);
        
	return 0;
}
