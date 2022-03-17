/* Author: Thomas Citrowske - Date: 2/24/22 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <time.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include "config.h"


/* Function prototypes */
void lock(int);
void use_resource(int);
void unlock(int);
char *getOutputPerror(char*);
void logMessage(char*, int, char*);


struct shmseg {
	int resource;
	int tickets[MAX_PROCS];
	int choosing[MAX_PROCS];
};
struct shmseg *shmp;


int main (int argc, char *argv[]) {
	/* Parse arguments */
	int procNumber = atoi(argv[1]) + 1;
	int shmid = atoi(argv[2]);
	int semid = atoi(argv[3]);
	
	/* Variables/startup functions for random number generator */
	int randomNum;
	int randLower = 1;
	int randUpper = 5;
	srand(time(0) * procNumber); /* Seed the random number generator */
	
	/* Attach shared memory to child process */
	shmp = shmat(shmid, NULL, 0);
	if (shmp == (void *) -1) {
		char *output = getOutputPerror(argv[0]);
		perror(output);
		exit(1);
	}
	
	/* Set logfile name */
	char intToString[3];
	sprintf(intToString, "%d", procNumber);
	char logfile[10] = "logfile.";
	strcat(logfile, intToString);
	
	/* Semaphore implementation of critical section */
	int i = 0;
	for (i = 0; i < 5; i++) {
		struct sembuf sb = {0, -1, 0};
		logMessage("Requested to join critical section by process number: ", procNumber, logfile);
		if (semop(semid, &sb, 1) == -1) { /* Lock */
			char *output = getOutputPerror(argv[0]);
			perror(output);
			exit(1);
		}
		logMessage("Entered critical section by process number: ", procNumber, logfile);
		randomNum = (rand() % (randUpper - randLower + 1)) + randLower; /* Random number from randLower to randUpper */
		sleep(randomNum);
		use_resource(procNumber - 1);
		logMessage("Wrote in 'cstest' file by process number: ", procNumber, logfile);
		randomNum = (rand() % (randUpper - randLower + 1)) + randLower; /* Random number from randLower to randUpper */
		sleep(randomNum);
		logMessage("Exited critical section by process number: ", procNumber, logfile);
		sb.sem_op = 1;
		if (semop(semid, &sb, 1) == -1) {
			char *output = getOutputPerror(argv[0]);
			perror(output);
			exit(1);
		}
	}
	
	
        return 0;
}

void lock(int procNumber) {
	/* Before getting the ticket number, "choosing" variable is set to be true. */
	shmp->choosing[procNumber] = 1;
	MEMBAR; /* Memory barrier applied */
	
	int max_ticket = 0;
	
	/* Finding Maximum ticket value among current threads */
	int i;
	for (i = 0; i < MAX_PROCS; ++i) {
		int ticket = shmp->tickets[i];
		max_ticket = ticket > max_ticket ? ticket : max_ticket;
	}
	
	/* Allotting a new ticket value as MAXIMUM + 1 */
	shmp->tickets[procNumber] = max_ticket + 1;
	
	MEMBAR;
	shmp->choosing[procNumber] = 0;
	MEMBAR;
	
	/* The ENTRY Section starts from here */
	int other;
	for (other = 0; other < MAX_PROCS; ++other) {
		
		/* Applying the bakery algorithm conditions */
		while (shmp->choosing[other]) {
		}
		
		MEMBAR;
		
		while (shmp->tickets[other] != 0 && (shmp->tickets[other] < shmp->tickets[procNumber] || (shmp->tickets[other] == shmp->tickets[procNumber] && other < procNumber))) {
		}
		
	}
}

/* EXIT Section */
void unlock(int procNumber) {
	MEMBAR;
	shmp->tickets[procNumber] = 0;
}


/* The CRITICAL Section */
void use_resource(int procNumber) {
	if (shmp->resource != 0) {
		printf("Resource was acquired by %d, but is still in-use by %d!\n", procNumber, shmp->resource);
	}
	shmp->resource = procNumber;
	int realProcNumber = procNumber + 1;
	printf("Process %d just wrote to 'cstest' file...\n", realProcNumber);
	
	/* Write to 'cstest' file */
	time_t rawtime;
	struct tm * timeinfo;
	FILE *fptr = fopen("cstest", "a");
	if (fptr == NULL) {
		printf("Error: unable to open cstest file.\n");
		exit(0);
	}
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	fprintf(fptr, "%d:%d:%d Queue %d File modified by process number %d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, shmp->tickets[procNumber], realProcNumber);
	fclose(fptr);
	
	MEMBAR;
	
	shmp->resource = 0;
}

char *getOutputPerror(char *programName) {
	char* output = strdup(programName);
	strcat(output, ": Error");
	return output;
}

void logMessage(char *message, int procNumber, char *fileName) {
	time_t rawtime;
        struct tm * timeinfo;
	time(&rawtime);
        timeinfo = localtime(&rawtime);
	FILE *fptr = fopen(fileName, "a");
        if (fptr == NULL) {
                printf("Error: unable to open log file.\n");
                exit(0);
        }
        fprintf(fptr, "%d:%d:%d %s%d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, message, procNumber);
        fclose(fptr);	
}
