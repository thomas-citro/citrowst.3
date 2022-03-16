/* Author: Thomas Citrowske - Date: 2/24/22 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/sem.h>
#include <time.h>
#include "config.h"


/* Global variables */
pid_t children[MAX_PROCS];
int nprocs;
int ss;
int shmAllocated;
int shmid;
char *programName;
int activeProcesses;
int currentlyTerminating;
struct shmseg *shmp;

struct shmseg {
	int resource;
	int tickets[MAX_PROCS];
	int choosing[MAX_PROCS];
};


/* Function Prototypes */
int isANumber(char*);
char *getOutputPerror();
int deallocateSharedMemory();
void endProgramHandler(int, int);
void childTermHandler(int);
void ctrlCHandler(int);
void logTermination(char*);
static void timeoutHandler(int);
static int setupinterrupt(void);
static int setupitimer(void);


int main (int argc, char *argv[]) {
	/* Setup signals/interrupts */
	signal(SIGINT, ctrlCHandler);
	signal(SIGCHLD, childTermHandler);
	
	/* For semaphore */
	union semun {
		int val;
		struct semid_ds *buf;
		ushort array[1];
	} sem_attr;
	
	/* Initialize variables */
	programName = argv[0];
	shmAllocated = 0;
	activeProcesses = 0;
	ss = 100;
	nprocs = 0;
	currentlyTerminating = 0;
	
	int i;
	int usageStatement = 0;
	int option;
	while ((option = getopt(argc, argv, "ht:")) != -1) {
		/* Parse the options -h and -t */
		switch (option) {
			case 'h' :
				usageStatement = 1;
				break;
			case 't' :
				if (!isANumber(optarg)) {
					usageStatement = 1;
				} else if (atoi(optarg) < 1) {
					usageStatement = 1;
				} else {
					ss = atoi(optarg);
				}
				break;
			default:
				printf("Error\n");
		}
	}
	if (argv[optind] == NULL) {
		/* User did not enter a value for n */
		usageStatement = 1;
	} else {
		/* Parse the argument for n */
		if (!isANumber(argv[optind])) usageStatement = 1;
		else {
			if (atoi(argv[optind]) > MAX_PROCS) {
				printf("Warning: Maximum value for 'n' is 20. Setting number of processes to 20.\n");
				nprocs = 20;
			} else if (atoi(argv[optind]) < 1) {
				usageStatement = 1;
			} else {
				nprocs = atoi(argv[optind]);
			}
		}
	}
	
	/* Print usage statement if the user used -h or entered an argument improperly */
	if (usageStatement) {
		printf("Usage: ./master [-h] [-t ss] [n]\n");
		printf("Runs master program to fork 'n' slave processes.\n");
		printf("Options:\n");
		printf("-h      for help.\n");
		printf("-t ss   (integer) sets the maximum time in seconds (default 100) after which the process should terminate itself if not completed.\n");
		printf("n       (integer) sets the number of processes to fork off (max 20).\n");
		return 0;
	}	
	
	/* Allocate shared memory */
	shmAllocated = 1;
	shmid = shmget(SHM_KEY, sizeof(struct shmseg), SHM_PERM|IPC_CREAT);
	if (shmid == -1) {
		char *output = getOutputPerror();
		perror(output);
		return 1;
	}
	
	/* Attach shared memory */
	shmp = shmat(shmid, NULL, 0);
	if (shmp == (void *) -1) {
		char *output = getOutputPerror();
		perror(output);
		return 1;
	}
	
	/* Initialize struct for shared memory */
	shmp->resource = 0;
        for (i = 0; i < MAX_PROCS; i++) {
                shmp->tickets[i] = 0;
                shmp->choosing[i] = 0;
        }
	
	/* Create file for ftok */
	FILE *fp = fopen("ftokFile", "ab+"); /* creates file if it doesn't exist */
	fclose(fp);
	
	/* Initialize semaphore */
	key_t key;
	int semid;
	key = ftok("ftokFile", 'E');
	semid = semget(key, 1, 0666 | IPC_CREAT);
	sem_attr.val = 1; /* unlocked */
	semctl(semid, 0, SETVAL, sem_attr);
	
	/* Fork off child processes */
	pid_t childpid = 0;
	for (i = 0; i < nprocs; i++) {
		if ((childpid = fork()) == -1) {
			char *output = getOutputPerror();
			perror(output);
			return 1;
		} else if (childpid == 0) {
			/* Child process */
			char strProcNum[10];
			sprintf(strProcNum, "%d", i);
			char strShmid[100];
			sprintf(strShmid, "%d", shmid);
			char *args[] = {"./slave", strProcNum, strShmid, (char*)0};
			execvp("./slave", args);
			
			char *output = getOutputPerror();
			perror(output);
			return 1;
		} else {
			/* Parent process */
			activeProcesses++;
			children[i] = childpid;
		}
	}
	/* Parent process start timer */
	if (setupinterrupt() == -1) {
		char *output = getOutputPerror();
		perror(output);
		return 1;
	}
	if (setupitimer() == -1) {
		char *output = getOutputPerror();
		perror(output);
		return 1;
	}
	for( ; ; );
	
        return 0;
}

/* Returns false/true (0/1) value if the string is an integer */
int isANumber (char *str) {
	int i;
	for (i = 0; i < strlen(str); i++) {
		if (!isdigit(str[i])) return 0;
	}
	return 1;
}

/* Return the beginning of the error message for perror */
char *getOutputPerror () {
	char* output = strdup(programName);
	strcat(output, ": Error");
	return output;
}

/* Removes shared memory attachment and deletes/deallocates the shared memory segment */
int deallocateSharedMemory() {
	int returnValue;
	returnValue = shmdt(shmp);
	if (returnValue == -1) {
		char *output = getOutputPerror();
		perror(output);
		exit(1);
	}
	
	returnValue = shmctl(shmid, IPC_RMID, NULL);
	if (returnValue == -1) {
		char *output = getOutputPerror();
		perror(output);
		exit(1);
	}

	return 0;
}

/* Function to end program. Kills all children and calls function to deallocate shared memory */
void endProgramHandler (int s, int killChildren) {
	if (killChildren) {
		int i;
		for (i = 0; i < nprocs; i++) {
			if ((kill(children[i], SIGKILL)) == -1) {
				char *output = getOutputPerror();
				perror(output);
			}
		}
	}
	if (shmAllocated) deallocateSharedMemory();
	exit(0);
}

/* For timer */
static int setupitimer(void) {
	struct itimerval value;
	value.it_interval.tv_sec = ss;
	value.it_interval.tv_usec = 0;
	value.it_value = value.it_interval;
	return (setitimer(ITIMER_REAL, &value, NULL));
}

/* For timer */
static int setupinterrupt(void) {
	struct sigaction act;
	act.sa_handler = timeoutHandler;
	act.sa_flags = 0;
	return (sigemptyset(&act.sa_mask) || sigaction(SIGALRM, &act, NULL));
}

/* Detected ctrl+c, so we can end the program */
void ctrlCHandler(int s) {
	printf("\n Detected ctrl+c. Now exiting program...\n");
	currentlyTerminating = 1;
	logTermination("ctrl+C");
	endProgramHandler(1, 1);
}

/* The timer has ended, so we can end the program */
static void timeoutHandler(int s) {
	currentlyTerminating = 1;
	printf("Timer ended. Now exiting program...\n");
	logTermination("timeout");
	endProgramHandler(1, 1);
}

/* All children have terminated, so we can end the program */
void childTermHandler(int s) {
	activeProcesses--;
	if (activeProcesses < 1 && !currentlyTerminating) {
		printf("All children have terminated. Now exiting program...\n");
		logTermination("all children terminated");
		endProgramHandler(1, 0);
	}
}

/* Print message to 'cstest' about program termination */
void logTermination(char *method) {
	time_t rawtime;
	struct tm * timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	FILE *fptr = fopen("cstest", "a");
	if (fptr == NULL) {
		printf("Error: unable to open 'cstest' file.\n");
		exit(0);
	}
	fprintf(fptr, "%d:%d:%d Program ended. Termination method: %s\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, method);
}	
