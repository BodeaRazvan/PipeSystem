#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

//I tried to work a bit on my code separation and indentation so it is easier to follow (unlike my previous assignments)

int fdr;
int fdw;
int memSize;
int shmId;
char *shmMemMap;
char *memMapFile;
int memMapFileSize;
int memMapFileId;

void handleError(char *error){
    printf("ERROR\n");
    printf("%s",error);
}

void writeMessage(char *buffer, int flag){
    unsigned long *buffer_length;
    buffer_length=malloc(sizeof (int));
    *buffer_length=strlen(buffer);
    write(fdw, buffer_length, 1);
    write(fdw,buffer,strlen(buffer));
    if(flag==0){
        *buffer_length=5;
        write(fdw, buffer_length, 1);
        write(fdw,"ERROR",5);
    }else{
        *buffer_length=7;
        write(fdw, buffer_length, 1);
        write(fdw,"SUCCESS",7);
    }
    free(buffer_length);
}

void pingPong(){
    int *nr;
    nr=malloc(sizeof(int));
    *nr=4;
    write(fdw, nr, 1);
    write(fdw,"PING",4);
    *nr=4;
    write(fdw, nr, 1);
    write(fdw,"PONG",4);
    *nr=88815;
    write(fdw, nr, 4);
    free(nr);
}

void createShm(char *buffer){
    int MemError=0;
    read(fdr,&memSize,4);
    shmId=shm_open("/lRoT44c",O_CREAT | O_RDWR,0664);
    if(shmId<0){
      MemError=1;
    }
    ftruncate(shmId,memSize);
    shmMemMap = mmap(0, memSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmId, 0);
    if(shmMemMap == MAP_FAILED){
        MemError=1;
    }
    if(MemError==1){
        writeMessage(buffer,0);
    }else{
        writeMessage(buffer,1);
    }
}

void writeToShm(char *buffer){
    int error=0;
    unsigned int offset=0, value = 0;
    read(fdr,&offset,sizeof(unsigned int));
    read(fdr,&value,sizeof(unsigned int));
    if(offset+4 > memSize){
        error=1;
    }
    if(error==1){
        writeMessage(buffer,0);
    }else{
        memcpy(&shmMemMap[offset], &value, sizeof(unsigned int));
         writeMessage(buffer,1);
    }
}

void mapFile(char *buffer){
    char path[100];
    int size;

    read(fdr,&size,1);
    read(fdr,path,size);
    path[size]='\0';
    memMapFileId = open(path, O_RDONLY);
    if(memMapFileId < 0){
        writeMessage(buffer,0);
    }
    else{
         memMapFileSize=lseek(memMapFileId, 0, SEEK_END);
         lseek(memMapFileId, 0, SEEK_SET);
         memMapFile = (char*)mmap(0, memMapFileSize, PROT_READ | PROT_WRITE , MAP_PRIVATE, memMapFileId, 0);
         if(memMapFile==MAP_FAILED){
            writeMessage(buffer,0);
         }else{
             writeMessage(buffer,1);
         }
    }
}

void readFromFileOffset(char *buffer){
    unsigned int offset,noOfBytes;
    read(fdr,&offset,sizeof (unsigned int));
    read(fdr,&noOfBytes,sizeof (unsigned int));

    if(offset + noOfBytes >= memMapFileSize){
        writeMessage(buffer,0);
    }else{
        memcpy(shmMemMap, &memMapFile[offset], noOfBytes);
        writeMessage(buffer,1);
    }
}

void readFromFileSection(char *buffer){
    //read from requested section number a number of bytes = noOfBytes from the offset offset
    unsigned int sectionNo,offset,noOfBytes;
    read(fdr,&sectionNo,sizeof (unsigned int));
    read(fdr,&offset,sizeof (unsigned int));
    read(fdr,&noOfBytes,sizeof (unsigned int));

    if(offset+noOfBytes>memMapFileSize){
        writeMessage(buffer,0);
    }

    //Here it was extremely important to use the specific data types to correspond with the required sizes
    //ex  unsigned char -> 1 byte   ||  short -> 2 bytes
    short header_size;
    int version;
    unsigned char no_of_sections;
    int section_offset;
    int section_size;
    int bytesToSkip=0;

    memcpy(&header_size,&memMapFile[memMapFileSize-6],2);
    //  printf("%d\n\n",header_size);
    memcpy(&version,&memMapFile[memMapFileSize-header_size],4);
    //  printf("%d\n\n",version);
    memcpy(&no_of_sections,&memMapFile[memMapFileSize-header_size+4],1);
    //  printf("%d\n\n",no_of_sections);
    if(sectionNo>no_of_sections+1){
        writeMessage(buffer,0);
        return;
    }

    for(int i=0;i<sectionNo-1;i++){
        //we are interested only in the sectionNo-th appearance of offset and size so we just skip over the rest
        bytesToSkip+=23;
    }
    bytesToSkip+=15;  //also skip the last section name and type
    // printf("%d\n\n",bytesToSkip);
    memcpy(&section_offset,&memMapFile[memMapFileSize-header_size+5+bytesToSkip],4);
    memcpy(&section_size,&memMapFile[memMapFileSize-header_size+5+bytesToSkip+4],4);
    // printf("%d\n\n",section_offset);
    // printf("%d\n\n",section_size);

    if(offset+noOfBytes >= section_size){
        writeMessage(buffer,0);
        return;
    }
    memcpy(shmMemMap,&memMapFile[offset+section_offset],noOfBytes);
    writeMessage(buffer,1);
}

int main() {
    if(mkfifo("RESP_PIPE_88815",0600)<0){
        handleError("cannot create the response pipe");
    }
    fdr=open("REQ_PIPE_88815",O_RDONLY);
    fdw=open("RESP_PIPE_88815",O_WRONLY);
    if(fdr < 0){
        handleError("cannot open the request pipe");
    }
    if(fdw < 0){
        handleError("cannot open the response pipe");
    }
    int *nr;
    nr=malloc(sizeof(int));
    *nr=7;
    write(fdw, nr, 1);
    write(fdw,"CONNECT",7);
    printf("SUCCESS\n");
    while(1){
        char *buffer = (char *)malloc(100 * sizeof(char));
        unsigned long buffer_length = strlen(buffer);
        read(fdr,&buffer_length,sizeof (char));
        read(fdr,buffer,buffer_length);
        if(strcmp(buffer,"PING")==0){
            pingPong();
        }
        if(strcmp(buffer,"CREATE_SHM")==0){
            createShm(buffer);
        }
        if(strcmp(buffer,"WRITE_TO_SHM")==0){
            writeToShm(buffer);
        }
        if(strcmp(buffer,"MAP_FILE")==0){
            mapFile(buffer);
        }
        if(strcmp(buffer,"READ_FROM_FILE_OFFSET")==0){
            readFromFileOffset(buffer);
        }
        if(strcmp(buffer,"READ_FROM_FILE_SECTION")==0){
            readFromFileSection(buffer);
        }
        if(strcmp(buffer,"READ_FROM_LOGICAL_SPACE_OFFSET")==0){
            //not solved
            return 0;
        }
        if(strcmp(buffer,"EXIT")==0){
            free(buffer);
            unlink("RESP_PIPE_88815");
            close(fdr);
            close(fdw);
            free(nr);
            return 0;
        }
    }
}
