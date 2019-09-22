/*
	Name: Hugh Boy
	ID:   1001522789

	CSE3320 HW#1
		This program is a basic linux shell. Every feature from the specification 
		has been implemented and seems to work, however this has not been 
		thoroughly tested and bugs do happen occasionally(especiallly on OMEGA).

	Note: String parsing is strongly inspired by 
			"https://github.com/CSE3320/Shell-Assignment/blob/master/mfs.c"

	todo:
*/

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_ARGS 11
#define MAX_TOKENS 12
#define MAX_COMMAND_LEN 255
#define WHITESPACE " \t\n"
#define PATH {"./", "/usr/local/bin/", "/usr/bin/", "/bin/"}
#define MAX_PATH_LEN 200
#define PATH_LEN 4
#define MAX_HISTORY 50
#define DISABLE_NULL_PIDS

static pid_t running_pid = 0;
static pid_t suspended_pid = 0;

/*signal handler function for control-c and control-v*/
static void sig_handler( int signal){
	if(signal == SIGINT){
		if(running_pid == 0){//pid=0 means nothing running
			return;
		}else{
			kill(running_pid, SIGINT);
		}
	}
	if(signal == SIGTSTP){
		if(running_pid == 0){//pid=0 means nothing running
			return;
		}else{
			kill(running_pid, SIGTSTP);
			suspended_pid = running_pid;
		}
	}
}

/*data structure that holds the characters typed by the user and the 
pointers to tokenized input in that string*/
typedef struct cmd{
	char buffer[MAX_COMMAND_LEN];
	char *tokens[MAX_TOKENS];
	pid_t pid;
} cmd;

/*data structure that holds an array of commands used in history and 
variables that can keep track of them using a circular queueish design
(see insert_cmd and print_cmds for implementation)*/
typedef struct cmd_hist{
	cmd history[MAX_HISTORY];
	int recent;
	int oldest;
	int touched;
} cmd_hist;

/*same as cmd_hist but with pids instead*/
typedef struct pid_hist{
	pid_t history[MAX_HISTORY];
	int recent;
	int oldest;
	int touched;
} pid_hist;

/*deep copies command so that tokens point to self contained strings*/
void copycmd(cmd *to, cmd *from){
	int i;
	memcpy(to->buffer, from->buffer, MAX_COMMAND_LEN);
	for(i=0; i<MAX_TOKENS; i++){
		if(from->tokens[i] == NULL){
			to->tokens[i] = NULL;
		}else{
			to->tokens[i] = 
				from->tokens[i] - from->buffer + to->buffer;//position relative
		}
	}
	to->pid = from->pid;
}

/*insert a pid into the pid_hist properly handling everything(hopefully)*/
void insertpid(pid_hist *pids, pid_t to_add){
	#ifdef DISABLE_NULL_PIDS
	//-1 is inserted for commands that don't have pids
		if(to_add == -1){
			return;
		}
	#endif
	if(!pids->touched){
		//first insert special case
		pids->history[0] = to_add;
		pids->touched = 1;
	}else{
		//not first insert
		pids->recent++; 
		if(pids->recent == MAX_HISTORY) pids->recent = 0;//wraparound

		pids->history[pids->recent] = to_add;

		if(pids->recent == pids->oldest){
			pids->oldest++; 
			if(pids->oldest == MAX_HISTORY) pids->recent = 0;//wraparound
		}
	}

}

/*same as insert_pid() but for cmd_hist*/
void insertcmd(cmd_hist *cmds, cmd *to_add){
	if(!cmds->touched){
		//first insert special case
		copycmd(&cmds->history[0], to_add);
		cmds->touched = 1;
	}else{
		//not first insert
		cmds->recent++; 
		if(cmds->recent == MAX_HISTORY) cmds->recent = 0;//wraparound

		copycmd(&cmds->history[cmds->recent], to_add);

		if(cmds->recent == cmds->oldest){
			cmds->oldest++; 
			if(cmds->oldest == MAX_HISTORY) cmds->recent = 0;//wraparound
		}
	}

}

/*called by pidhist command to print numbered history*/
void print_pids(pid_hist *pids){
	int i = pids->oldest;

	int stop_at = pids->recent+1;
	if(stop_at == MAX_HISTORY) stop_at = 0;//wraparound

	while(i != stop_at){
		printf("%d: %d\n", i, pids->history[i]);
		i++;
	}
}

/*function used for debugging*/
void print_tokens(cmd *command){
	int i;
	for(i=0; i<MAX_TOKENS ; i++){
		printf("tokens[%d] = %s\n", i, command->tokens[i]);
	}
}

/*called by print_cmds; prints a single command with all its tokens*/
void print_cmd(cmd *command, int num){
	char temp[MAX_COMMAND_LEN];
	int i;
	temp[0] = 0;
	for(i=0; i<MAX_TOKENS; i++){
		if(command->tokens[i] == NULL){
			break;
		}
		strcat(temp, command->tokens[i]);
		strcat(temp, " ");
	}
	printf("%d. %s\n", num, temp);
}

/*prints all commands; called by history command*/
void print_cmds(cmd_hist *cmds){
	int i = cmds->oldest;

	int stop_at = cmds->recent+1;
	if(stop_at == MAX_HISTORY) stop_at = 0;//wraparound

	while(i != stop_at){
		print_cmd(&cmds->history[i], i);
		i++;
	}
}

/*
	Takes in a command string and uses it to populate tokens[]
	Note: destroys buffer string
*/
void tokenize_cmd(cmd *command){
	char *save_ptr;
	int token_count = 0;

	command->tokens[0] = 
		strtok_r(command->buffer, WHITESPACE, &save_ptr);//first token must exist
	token_count = 1;
	while(
		(token_count < MAX_TOKENS)/*note short circuiting prevents array out of bounds*/
		&&
		(command->tokens[token_count] = strtok_r(NULL, WHITESPACE, &save_ptr))
	){token_count++;}
	command->tokens[MAX_TOKENS-1] = (char *)NULL;//null terminate tokens array

	return;
}


/*executes a command without any ";" tokens
called after command is split by split_command()*/
void execute_command(cmd *command, pid_hist *pids, cmd_hist *cmds){
	char *path[] = PATH;
	int i;
	char program_path[MAX_PATH_LEN];
	pid_t pid = 0;
	pid_t successful_pid = -1;//assume pid isnt' success until it is

	
	//SPECIAL CASES
	if(command->tokens[0] == NULL){
		return;
	}
	if(     (strcmp(command->tokens[0], "exit") == 0) 
		|| 
		(strcmp(command->tokens[0], "quit") == 0)   ){
		exit(0);
	}
	if((strcmp(command->tokens[0], "cd") == 0)){
		chdir(command->tokens[1]);
		insertpid(pids,-1);
		return;
	}
	if((strcmp(command->tokens[0], "bg") == 0)){
		kill(suspended_pid, SIGCONT);
		suspended_pid = 0;
		insertpid(pids,-1);
		return;
	}
	if((strcmp(command->tokens[0], "listpids") == 0)){
		print_pids(pids);
		insertpid(pids,-1);
		return;
	}
	if((strcmp(command->tokens[0], "history") == 0)){
		print_cmds(cmds);
		insertpid(pids,-1);
		return;
	}

	for(i=0; i<PATH_LEN; i++){
		strcpy(program_path, path[i]);
		strcat(program_path, command->tokens[0]);
		pid = fork();
		if(pid == 0){
			execv(program_path, command->tokens);
			exit(12); //used to signal to parent that execv failed
		}else{
			running_pid = pid;
			int status;
			waitpid(running_pid, &status, 0);
			running_pid = 0;
			if(WEXITSTATUS(status) == 12){
				//child did not execute properly
			}else{
				successful_pid = pid;
				break;//need to stop searching path once command is executed
			}
		}
	}

	if(successful_pid == -1){
		printf("%s: Command not found.\n", command->tokens[0]);
	}

	insertpid(pids, successful_pid);
	return;
}

/*splits or otherwise processes command before passing into execute_command()*/
void split_command(cmd *command, pid_hist *pids, cmd_hist *cmds){
	int i;
	if(command->tokens[0] == NULL){
		return;//null command
	}

	//see if its a history request
	if(command->tokens[0][0] == '!'){
		int to_exec = atoi(command->tokens[0]+1);
		int i;
		i = cmds->oldest;

		int stop_at = cmds->recent+1;
		if(stop_at == MAX_HISTORY) stop_at = 0;//wraparound

		while(i != stop_at){
			if(i == to_exec){
				copycmd(command, &cmds->history[i]);
				split_command(command, pids, cmds);
				return;
			}
			i++;
		}
		printf("It looks like the history you entered wasn't valid\n");
		return;
	}
	
	insertcmd(cmds, command);//want to insert raw command typed by user before split

	//split
	int runner = 0;
	int split_pointer = 0;
	cmd splits[MAX_TOKENS];
	int tok_ptr = 0;
	for(i=0; i<MAX_TOKENS; i++){
		copycmd(&splits[i], command);
	}
	while(1){
		splits[split_pointer].tokens[tok_ptr] = splits[split_pointer].tokens[runner];
		if(command->tokens[runner] == NULL){
			splits[split_pointer].tokens[tok_ptr] = NULL;
			break;
		}else if(strcmp(command->tokens[runner],";")==0){
			splits[split_pointer].tokens[tok_ptr] = NULL;
			split_pointer++;
			runner++;
			tok_ptr = 0;
		}else{
			tok_ptr++;
			runner++;
		}
	}
	for(i=0; i<=split_pointer; i++){
		execute_command(&splits[i], pids, cmds);
	}
}

int main(){
	//vars
	cmd command;
	memset(&command, 0, sizeof(cmd));
	pid_hist pids;
	memset(&pids, 0, sizeof(pid_hist));
	cmd_hist cmds;
	memset(&cmds, 0, sizeof(cmd_hist));
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = &sig_handler;

	//add_handlers
	if(sigaction(SIGINT, &act, NULL)){
		perror("something may have gone wrong with signal handler\n");
		exit(0);
	}
	if(sigaction(SIGTSTP, &act, NULL)){
		perror("something may have gone wrong with signal handler2\n");
		exit(0);
	}

	//read_info loop
	while(1){
		printf("msh> ");
		while(NULL == fgets(command.buffer, MAX_COMMAND_LEN, stdin));
		tokenize_cmd(&command);
		split_command(&command, &pids, &cmds);
	}
}

