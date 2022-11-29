#define _POSIX_C_SOURCE
#define POSIX_SOURCE 1
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

// Global variables used within functions
char * argumentList[512];       // list of maximum 512 arguments
int wordNumber = 0;             // tracks number of words
int argumentNumber = 0;         // tracks argument number
char currDirectory[120];        // current director
int chstatus;                   // chstatus of fork
int background = 0;             // background checker either 0 or 1
int idle = 1;                   // if idle
int fgMode = 0;                 // tracks if in foreground or not
int blank;                      // checks if input is blank
int pidArray[512];              // array to track pid
int backgroundNum = 0;          // tracks how many background processes we have

//** Handler for SIGNINT **//
// Handles ctrl+c when called by user
void handle_SIGINT(){
    if (!idle) {
        fprintf(stderr, "Terminated by signal 2 \n");
    }
    else {
        fprintf(stderr, "\n: ");
    }
}

//** Handler for SIGTSTP **//
// Handles ctrl+z when called by user and alerts whether foreground mode is entered or exited
void handle_SIGTSTP(){
    fgMode = (fgMode == 1) ? 0: 1;
    // fprintf(stderr, "\n%i", fgMode);

    if (fgMode) {
        fprintf(stderr, "\n Entering foreground-only mode (& is now ignored)\n: ");
    }
    else {
        fprintf(stderr, "\n Exiting foreground-only mode\n: ");
    }
}

//** Function gets user input  **//
// This function takes user input, and inserts it into the global array argument list to be used by other functions. 
void getInput(char*args) {
    fprintf(stderr, ": ");
    fflush(stdin);
    fgets(args, 512, stdin);

    // count number of words and get string length size
    int count = 0;
    int sz = strlen(args);

    // checks if input is # for first char
    if (args[0] == '#') {
        blank = 1;
        return;
    }

    // checks if input is space for first char   ///////// DOUBLE CHECK HERE
    if (args[0] == ' '){
        fprintf(stderr, "space \n");
        return;
    }

    // if args == 1, means there was a blank line
    if (sz == 1) {
        blank = 1;
        return;
    }

    // gets the pid for handling cases of $
    // adapted from: https://edstem.org/us/courses/21025/discussion/1437434
    pid_t pid = getpid();
    char *pidstr;
    {
        int n = snprintf(NULL, 0, "%jd", pid);
        pidstr = malloc((n + 1) * sizeof *pidstr);
        sprintf(pidstr, "%jd", pid);
    }
    int pidLength = strlen(pidstr);

    // create a token to break up the arguments by either a space or \n 
    char*token = strtok(args, " \n");
    wordNumber = 0;

    // loop while the token is not NULL
    while (token != NULL) {

        // create tmp string to hold token string
        char *tmp = malloc(strlen(token) + 1);
        int howMany = 0; // keeps track of howmany characters
        int i = 0; // keeps track of current index
        int size = strlen(token);

        // loop while i < size of the token string
        while (i < size) {
            char character = token[i];

            //If character is $, check if following character is for expansion
            if (character == '$') {
                int next = i + 1;
                count ++; 
                //printf("count Hit \n");
            
                // check if next character is $ as well
                if (next < size && token[next] == '$') {

                    // if pid length and howmany is greater than the array size, realloc the tmp array
                    if (pidLength + howMany > size-1) {
                        tmp = realloc(tmp, size*2);
                    }

                    // append the pid to the tmp array
                    strcat(tmp, pidstr);
                    howMany += pidLength;
                    i+= 2;
                }

                // if following character is not $, just append current index character
                if (token[next] != '$') {
                    tmp[howMany] = token[i];
                    howMany++;
                    i++;
                }
            }

            // character is not a $ so just append as normally would do
            else {
                tmp[howMany] = token[i];
                howMany++;
                i++;
            }

            // if howmany is > than the size realloc tmp array
            if (howMany > size-1) {
                tmp = realloc(tmp, sz*2);
            }
        }

        // insert tmp string into argument list at index argument number
        argumentList[argumentNumber] = tmp;

        // find the next token, increment number of args, and words
        token = strtok(NULL, " \n");
        argumentNumber++;
        wordNumber++;
    }

    // dont forget to free up tmp
    //free(tmp);
    // set argument list last index == NULL
    argumentList[argumentNumber] = NULL;
    return;
}

//** Function change directory **//
// Function acts as cd fuction for shell. cd can be called with or without another argument.
void changeDirectory() {
    // cwd is current directory
    char cwd[256];
    int check;

    // By itself - with no arguments - it changes to the directory specified in the HOME environment variable
    if (argumentNumber == 1) {
        check = chdir(getenv("HOME"));
    }
    // with another argument, the path of the directory to change to
    else if (argumentNumber == 2) {
        check = chdir(argumentList[1]);
    }

    // does nothing, just used for a debugging
    if (check == 0) {
        //printf("directory success \n");
        //printf("%s \n", getcwd(currDirectory, 120));
    }
    // else the change directory has failed
    else {
        fprintf(stderr, "change directory has failed \n");
    }
}

//** handle fork function **//
// Function handle forks 
void handleFork() {
    pid_t spawnpid;
    int inputFile;
    int index;
    int redirectIn = 0;
    int redirectOut = 0;

    // check if the last argument is & and mark background = 1
    if (strcmp(argumentList[argumentNumber-1], "&") ==0) {
        background = (fgMode == 0) ? 1: 0;
        argumentList[argumentNumber-1] = NULL;
        argumentNumber --;
    }

	spawnpid = fork();
    alarm(600);
	switch (spawnpid){

        // Code in this branch will be exected by the parent when fork() fails and the creation of child process fails as well
		case -1:
			perror("fork() failed!");
			exit(1);

        // spawnpid is 0. This means the CHILD will execute the code in this branch    
		case 0:

            for(int i = 0; i < argumentNumber; i++) {

                // stores file contents argument of file[0] in file[2]
                if (strcmp(argumentList[i], ">") == 0) {
                    index = i;
                    inputFile = open(argumentList[i+1],  O_RDWR | O_CREAT | O_TRUNC, 0666);

                    //if inputFile == -1, there was an error opening the file
                    if (inputFile == -1) {
                        fprintf(stderr, "error opening file %s \n", argumentList[i+1]);
                        exit(1);
                    }
                    // dup2 input file then close. Set that argument list index to null and initialize redirect out
                    dup2(inputFile, 1);
                    close(inputFile);
                    argumentList[index] = NULL;
                    redirectOut = 1;

                // stores file contents argument of file[2] in file[0]
                } else if (strcmp(argumentList[i], "<") == 0) {
                    index = i;
                    inputFile = open(argumentList[i+1], O_RDONLY);
                    
                    //if inputFile == -1, there was an error opening the file
                    if (inputFile == -1) {
                        fprintf(stderr, "error opening file %s \n", argumentList[i+1]);
                        exit(1);
                    }
                    // dup2 input file then close. Set that argument list index to null and initialize redirect in
                    dup2(inputFile, 0);
                    close(inputFile);
                    argumentList[index] = NULL;
                    redirectIn = 1;
                }
            }

            // check if this command is not executed in the background
            if (!background) {

                // set status equal to return value from execvp, if -1 send error and exit
                int status = execvp(argumentList[0], argumentList);
                if (status == -1) {
                    perror(argumentList[0]);
                    exit(1); 
                    }
            }

            // else we need to fork again since this is executed in background 
            else {
                
                // ignore crtl+c in child fork
                signal(SIGINT, SIG_IGN);

                // background process is running to output to /dev/null
                if (!redirectOut){
                    int nullOutput = open("/dev/null", O_WRONLY);
                    dup2(nullOutput, 1);
                    close(nullOutput);

                }

                // background process is running to input to /dev/null
                if (!redirectIn) {
                    int nullInput = open("/dev/null", 0);
                    dup2(nullInput, 0);
                    close(nullInput);
                }

                // fork if the process is a child
                pid_t childPid;
                int childPidNumber;
                childPid = fork();
                alarm(600);
                switch (childPid){

                    // error in forking child
                    case -1:
                        perror("second child fork() failed!");
                        exit(1);

                    // childPid is 0 which means it will execute the background process
                    case 0:

                        // get child pid and fprintf
                        childPidNumber = getpid();
                        fprintf(stderr, "background pid is %i\n", childPidNumber);

                        // check the return status value
                        int status = execvp(argumentList[0], argumentList);

                        if (status == -1) {
                            perror(argumentList[0]);
                            exit(1); 
                            }

                    default:
                        // call waitpid on childPid
                        waitpid(childPid,&chstatus,0);

                        // check if the child was terminated normally and minipulate exitcode
                        if (WIFEXITED(chstatus)){
                            int exitCode = WEXITSTATUS(chstatus);
                            chstatus = exitCode;
                        }

                        // fprintf background pid is done and return exit value
                        fprintf(stderr, "\nbackground pid %i is done: exit value %i\n: ", childPid, chstatus);

                        // Loop through the array and remove the first match
                        for (int a = 0; a < 512; a++) {
                            if (pidArray[a] = childPid){
                                pidArray[a] = 0;
                                backgroundNum--;
                                break;
                            }
                        }
                        exit(0);
                }
            }
			exit(1);

        // spawnpid is the pid of the child. This means the PARENT will execute the code in this branch
		default:

            // If not a background wait for the child to end
            if (!background) {
                idle = 0;

                // Foreground represents the child status to be output to user. 
                waitpid(spawnpid,&chstatus,0);

                // check the parent fork terminated normally and minipulate exitcode
                if (WIFEXITED(chstatus)){
                    int exitCode = WEXITSTATUS(chstatus);
                    chstatus = exitCode;
                }
                idle = 1;
            }
            else {
                
                // Add the pid to the array 
                for (int a = 0; a < 512; a++) {
                    if (pidArray[a] == 0){
                        pidArray[a] = spawnpid + 1;
                        backgroundNum++;
                        break;
                    }
                }
                background = 0;
                usleep(1000);
            }
			break;
	}
}

//** Main function **//
// Handles ctrl+c and ctrl+z as well as gets user input and forks if necessary
int main() {

    // this holds the user input string, maximum of 2048 chars
    char args[2048];

    //Signal to handle ctrl+c
    struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = handle_SIGINT;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = SA_RESTART;
	sigaction(SIGINT, &SIGINT_action, NULL);

    // Signal to handle ctrl+z  SIGTSTP
    struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    
    while(1) {
        blank = 0;
        getInput(args);
        if (!blank) {
            fflush(stdin);

            // check if args is exit, kill any processes before the exit
            if (strcmp(argumentList[0], "exit") == 0) {
                // loop through pidarray and kill all pids
                int count =0 ;
                for (int a = 0; a < 512; a++) {
                    if (pidArray[a] != 0){
                        int currPid = pidArray[a];
                        kill(currPid, 15);
                        count ++;
                        //printf("just killed %i\n", currPid);
                    }
                }

                exit(0);
            }

            //check if args is cd and call function
            else if (strcmp(argumentList[0], "cd") == 0) {
                changeDirectory();
            }

            // check if argument is status
            else if (strcmp(argumentList[0], "status") == 0) {
            fprintf(stderr, "exit value %i \n", chstatus);
            }

            // this handles and does the Exec function 
            else {
                // call fork here
                handleFork();

            }

        // resets the argumentList and number for next round
        char * argumentList[512]; 
        argumentNumber = 0;
        }
    }
    return 0; 
}