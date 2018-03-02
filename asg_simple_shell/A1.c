#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

//
// This code is given for illustration purposes. You need not include or follow this
// strictly. Feel free to writer better or bug free code. This example code block does not
// worry about deallocating memory. You need to ensure memory is allocated and deallocated
// properly so that your shell works without leaking memory.
//

pid_t process; 
int parent = 1;
pid_t originalPid = 0;

int getcmd(char *prompt, char *args[], int *background)
{
	int length, i = 0;
	char *token, *loc;
	char *line = NULL;
	size_t linecap = 0;

	printf("%s", prompt);
	length = getline(&line, &linecap, stdin);

	if (length <= 0) {
		exit(-1);
	}

	// Check if background is specified..
	if ((loc = index(line, '&')) != NULL) {
		*background = 1;
		*loc = ' ';
	} else
		*background = 0;

	while ((token = strsep(&line, " \t\n")) != NULL) {
		for (int j = 0; j < strlen(token); j++)
			if (token[j] <= 32)
				token[j] = '\0';
			if (strlen(token) > 0)
				args[i++] = token;
	}
	return i;
}

static void sigHandler(int sig){
	if(sig == SIGINT){
		if (parent == 0){
			kill(process, SIGTERM);
		}
		
	}
	else if (sig == SIGCHLD){
		while (waitpid(-1, NULL, WNOHANG) != -1){}
		//else return -1;
	}
}

int getCommand(char* args[], int* jobList, int listIndex){

	char dir[300];
	int waiting = -1;

	//--------------------------------------built-in commands--------------------------------

	if(args[0] == NULL){
		printf("Please stop trying to crash this program.");
		return -1;
	}
	else if(strcmp(args[0], "jobs") == 0){
		for(int i = 0; i < listIndex; i++){
			if(jobList[i] != -1){
				printf("Job no.%i ", i+1);
				printf(" PID: ");
				printf("%i", jobList[i]);
				printf("\n");
				}
			}
			return 1;
		}
	else if (strcmp(args[0], "cd") == 0){
		chdir(args[1]);
		return 1;
	}
	else if (strcmp(args[0], "pwd") == 0){
		printf("%s", getcwd(dir, sizeof(dir)));
		return 1; 
	}
	else if (strcmp(args[0], "exit") == 0){
			//for (int i = 0; i < listIndex; i++){
				//kill(jobList[listIndex], SIGTERM);
			//}
			//kill(originalPid, SIGTERM);
		kill(0, SIGTERM);
		return 1;
	}
	else if (strcmp(args[0], "fg") == 0){
		waiting = atoi(args[1]);

		//verify if this process is valid
		if (waiting <= 0 || jobList[waiting-1] < 0){
			perror("cannot locate process");
		}
		else waitpid(jobList[waiting-1], NULL, 0);

		return 1;

	}
	return 0; 

}

int main(void)
{
	char *args[20];
	int bg;

	int* jobList = (int*)calloc(200, sizeof(int));
	pid_t deadChild = -1;
	int handled = 0;
	int isCommand = -1;
	int listIndex = 0;
       
    //--------------signals------------------------------------------
	if(signal(SIGINT, sigHandler) == SIG_ERR){
		printf("could not handle SIGINT");
	}
	signal(SIGTSTP, SIG_IGN);
	

	while(1) {
		bg = 0;
		parent = 1;
		handled = 0;
		isCommand = -1;
	
		memset(&args[0], 0, sizeof(args)); //clears the array so we don't get weird stuff
		int cnt = getcmd("\n>>", args, &bg);

		//check for dead child processes
		deadChild = waitpid(-1, NULL, WNOHANG);
		if(deadChild == -1){
			//remove it from jobList
			for (int i = 0; i < listIndex; i++){
				if(jobList[i] == deadChild){
					jobList[i] = -1;
				}
			}
		}

		//handles commands
		isCommand = getCommand(args, jobList, listIndex);


		if (isCommand != 1){ //if what the user entered isn't a command
			pid_t child = fork();

			if(child == 0){ //we are in child 
						//------------------------piping & redirection-----------------------------------------------
				for (int i = 0; i < cnt; i++){
					//redirecting
					if(strcmp(args[i], ">") == 0){
						handled = 1;
						close(1);
						if(i == 0){
							perror("lacking command");
						}
						if(args[i+1] == NULL){
							perror("cannot redirect due to lack of destination");
						}
						open(args[i+1], O_WRONLY);
						args[i] = '\0'; //so execvp stops reading here
						execvp(args[0], args);
						//printf("Success");
					}
					//piping
					if(strcmp(args[i], "|") == 0){
						handled = 1;

						int fd[2]; 
						args[i] = '\0'; //separates the 2 commands

						pipe(fd);
						pid_t subChild = fork();

						if(subChild > 0){ //the child of the child that receives the input
							close(0);
							//close(1);
							dup(fd[0]);
							close(fd[1]);

							if(args[i+1] == NULL){
								perror("lacking right operand");
							}

							else execvp(args[i+1], &args[i+1]);
						}
						else { //parent (which is the first child)
							close(1);
							dup(fd[1]);
							close(fd[0]);

							if(i == 0){
								perror("lacking left operand");
							}
							else execvp(args[0], args);
						}
						//close(fd[0]);
						//close(fd[1]);
					}
				}
				if (handled == 0) {
					execvp(args[0], args);
				}

			}

			if(bg == 0){
				parent = 0;
				process = child;
				waitpid(child, NULL, 0); //wait for child to finish before going onto next task
			}
			else if(bg == 1){ //else, we store the pid into an array
			//printf("
				jobList[listIndex] = child;
				listIndex++;
			}
		}
	}
	free(jobList);
	exit(0);
}
