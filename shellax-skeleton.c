#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

const char * sysname = "shellax";

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t * command)
{
	int i=0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background?"yes":"no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete?"yes":"no");
	printf("\tRedirects:\n");
	for (i=0;i<3;i++)
		printf("\t\t%d: %s\n", i, command->redirects[i]?command->redirects[i]:"N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i=0;i<command->arg_count;++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}


}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i=0; i<command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i=0;i<3;++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next=NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters=" \t"; // split at whitespace
	int index, len;
	len=strlen(buf);
	while (len>0 && strchr(splitters, buf[0])!=NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len>0 && strchr(splitters, buf[len-1])!=NULL)
		buf[--len]=0; // trim right whitespace

	if (len>0 && buf[len-1]=='?') // auto-complete
		command->auto_complete=true;
	if (len>0 && buf[len-1]=='&') // background
		command->background=true;

	char *pch = strtok(buf, splitters);
	command->name=(char *)malloc(strlen(pch)+1);
	if (pch==NULL)
		command->name[0]=0;
	else
		strcpy(command->name, pch);

	command->args=(char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index=0;
	char temp_buf[1024], *arg;
	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch) break;
		arg=temp_buf;
		strcpy(arg, pch);
		len=strlen(arg);

		if (len==0) continue; // empty arg, go for next
		while (len>0 && strchr(splitters, arg[0])!=NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len>0 && strchr(splitters, arg[len-1])!=NULL) arg[--len]=0; // trim right whitespace
		if (len==0) continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|")==0)
		{
			struct command_t *c=malloc(sizeof(struct command_t));
			int l=strlen(pch);
			pch[l]=splitters[0]; // restore strtok termination
			index=1;
			while (pch[index]==' ' || pch[index]=='\t') index++; // skip whitespaces

			parse_command(pch+index, c);
			pch[l]=0; // put back strtok termination
			command->next=c;
			continue;
		}

		// background process
		if (strcmp(arg, "&")==0)
			continue; // handled before

		// handle input redirection
		redirect_index=-1;
		if (arg[0]=='<')
			redirect_index=0;
		if (arg[0]=='>')
		{
			if (len>1 && arg[1]=='>')
			{
				redirect_index=2;
				arg++;
				len--;
			}
			else redirect_index=1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index]=malloc(len);
			strcpy(command->redirects[redirect_index], arg+1);
			continue;
		}

		// normal arguments
		if (len>2 && ((arg[0]=='"' && arg[len-1]=='"')
					|| (arg[0]=='\'' && arg[len-1]=='\''))) // quote wrapped arg
		{
			arg[--len]=0;
			arg++;
		}
		command->args=(char **)realloc(command->args, sizeof(char *)*(arg_index+1));
		command->args[arg_index]=(char *)malloc(len+1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count=arg_index;
	return 0;
}

void prompt_backspace()
{
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index=0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);


	//FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state=0;
	buf[0]=0;
	while (1)
	{
		c=getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c==9) // handle tab
		{
			buf[index++]='?'; // autocomplete
			break;
		}

		if (c==127) // handle backspace
		{
			if (index>0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c == 27 || c == 91 || c ==67 || c == 68) // handle multi-code keys
		{
			continue;
		}

		if (c==65) // up arrow
		{
			while (index>0)
			{
				prompt_backspace();
				index--;
			}	

			char tmpbuf[4096];
			printf("%s", oldbuf);
			strcpy(tmpbuf, buf);
			strcpy(buf, oldbuf);
			strcpy(oldbuf, tmpbuf);
			index += strlen(buf);
			continue;
		}	

		putchar(c); // echo the character
		buf[index++]=c;
		if (index>=sizeof(buf)-1) break;
		if (c=='\n') // enter key
			break;
		if (c==4) // Ctrl+D
			return EXIT;
	}
	if (index>0 && buf[index-1]=='\n') // trim newline from the end
		index--;
	buf[index++]='\0'; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}
int pipe_command(struct command_t *command, char *pathname, int *fd);
void rps(struct command_t *command);
void guessTheNumber(struct command_t *command);	
int myuniq(struct command_t *command);
void wiseman(struct command_t *command);
int io_redirect(struct command_t *command);
int process_command(struct command_t *command);
int main()
{
	while (1)
	{
		struct command_t *command=malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code==EXIT) break;

		code = process_command(command);
		if (code==EXIT) break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

//HELPER METHODS
void rps(struct command_t *command){
	int n;
	char pc; 
	char pcStr[10];
	char user[10];
	char userStr[10];
	srand(time(NULL)); //calculate random number btween 1-100
	n = rand() % 100;
	if(n < 33){ //choose r, p or s according to the number generated 
		pc = 'r'; 
		strcpy(pcStr, "ROCK");
	} else if (n > 33 && n < 66){
		pc = 'p';
		strcpy(pcStr, "PAPER");
	} else {
		pc = 's';
		strcpy(pcStr, "SCISSORS");
	}

	printf("\n\n\n\n\t\t\t\tEnter 'r' for Rock, 'p' for Paper, 's' for Scissors\n\t");	
	
	printf("Enter your choice: ");	//Take input from user r, p or s
	fgets(user, sizeof(char)*10, stdin);

	while(user[0] != 'r' && user[0] != 'p' && user[0] != 's'){
		printf("\n\tU should enter r, p or s\n");
		
		printf("\tEnter your choice: ");	//Take input from user r, p or s
		fgets(user, sizeof(char)*10, stdin);
	}

	if(user[0] == 'r'){
		strcpy(userStr, "ROCK");
	} else if (user[0] == 'p'){
		strcpy(userStr, "PAPER");
	} else {
		strcpy(userStr, "SCISSORS");	
	} 
	//Game started prints
	printf("\tYou choose: %s\n", userStr);	
	sleep(1); //Sleep calls added to make the game feel realistic
	printf("\t\t\t\t\tROCK!\n");
	sleep(1);
	printf("\t\t\t\t\t\tPAPER!\n");
	sleep(1);
	printf("\t\t\t\t\t\t\tSCISSORS!\n");
	sleep(1);
	printf("\t\t\t\t\t\tSHOOOT!\n");
	sleep(0.5);
	//printf("PC: %s\n", pcStr);	
	printf("\t\t\t\t\t   --%s vs %s--\n", pcStr, userStr);	
	
	//End game prints
	if(pc == user[0]){ 
		printf("\n\t\t\t\t\t    Its a tie\n\n");
	}
	else if (pc == 'r'){
		if(user[0] == 's'){
			printf("\n\t\t\t\t\t    Computer WINS!\n\n");
		} else if(user[0] == 'p'){
			printf("\n\t\t\t\t\t    User WINS!\n\n");
		}
	} else if (pc == 'p'){
		if(user[0] == 's'){
			printf("\n\t\t\t\t\t    User WINSs!\n\n");
		}else if(user[0] == 'r'){
			printf("\n\t\t\t\t\t    Computer WINS!\n\n");
		}
	} else {
		if(user[0] == 'p'){
			printf("\n\t\t\t\t\t    Computer WINS!\n\n");
		} else if (user[0] == 'r'){
			printf("\n\t\t\t\t\t    User WINS!\n\n");
		}
	}

}
//CUSTOM COMMAND BORA KOKEN
void guessTheNumber(struct command_t *command) {	
	int guess;
	int number;
	int numberOfGuess = 0;

	srand(time(NULL));

	number = rand() %101; //Random number generated btween 1-100

	printf("Welcome to the guessing game! You have 10 chances to guess the correct number.\n");
	
	//While the user has more lives (10 lives total)
	while(guess != number && numberOfGuess <= 9) {
		printf("Guess a number between 1 and 100: ");
		scanf("%d", &guess); //Take guess 

		if(guess > 100 || guess < 1) { //Check if its in range 1-100
			printf("Enter a number in range.\n");
		}

		if(guess > number && guess <= 100 && guess >= 1) { //If guess is higher
			printf("Enter a lower number than %d.\n", guess);
			numberOfGuess++;
		}

		else if(guess < number && guess <= 100 && guess >= 1) { //If guess is lower
			printf("Enter a higher number than %d.\n", guess);
			numberOfGuess++;
		}

		//End game prints
		if(numberOfGuess > 9) {
			printf("You are out of lives! Sorry :/\n"); 
		}

		else if (guess == number) {
			numberOfGuess++;
			printf("You guessed the right number in %d " "attempts. Congrats!\n", numberOfGuess);
		}
	}	
}
//WISEMAN
void wiseman(struct command_t *command){


	int isInteger; //will hold out integer value	
	if(command->args[0] != NULL){	

		isInteger = atoi(command->args[0]); //parsing str to int

		if(isInteger == 0){
			printf("\t Wrong Format - wiseman <minutes>\n");
		}
		else{
			int mins = isInteger;
			FILE *fptr = fopen("cron.txt", "w+"); //opening a crontab file
			if(fptr == NULL){
				printf("Error openin cron.txt\n");
			}
			fprintf(fptr, "*/%d * * * * /usr/games/fortune | /usr/games/cowsay >> /home/emirhanpolat/comp304/comp304Project1/wisecow.txt\n", mins); //Writing the specified cron schedule
			fclose(fptr); //close file ptr
			system("crontab cron.txt"); //call crontab on cron.txt
		}

	}
	else {
		printf("\t Wrong Format - wiseman <minutes>\n");
	}
}

//MYUNIQ COMMAND IMPLEMENTATION
int myuniq(struct command_t *command){

	char output[100][200]; //Our array we output

	//variables
	int i = 0; 
	FILE *fptr;
	char str[200];

	if(strcmp(command->args[0], "-c") == 0 || strcmp(command->args[0], "--count") == 0){
		fptr = fopen(command->args[1], "r"); //open file 
	} else{
		fptr = fopen(command->args[0], "r"); //open file 
	}

	if(fptr == NULL){
		printf("File cannot be found!\n");
		return EXIT;	
	}

	while(fgets(str,200,fptr) != NULL){ //Read from file
		strcpy(output[i], str); //Output becomes lines in file
		i++;
	}

	int counts[i];
	int count = 1;
	int y=0;
	int t=0;
	//Since the list should be sorted all the duplicates occur one after other
	while(y < i) { //comparing output arrays elements to count the duplicates
		if(strcmp(output[y], output[y+1]) == 0){ //if duplicate happens
			count++; //increment current count
			y++; //increment index 
		} else {
			counts[t] = count; //when duplicates end, meaning that count many same elements exits	
			t++; //assign count to counts[t] and increment t
			count=1; //restart count
			y++; 
		}
	}	

	int k, j, a;

	//Iterate over output and delete duplicates
	for(k = 0; k < i; k++){
		for(j = k+1; j < i; j++){
			if(k != j){
				if(strcmp(output[j],output[k]) == 0){ 	//IF duplicate detected
					//Iterating over array to reindex 
					for(a = j; a < i; a++){
						strcpy(output[a], output[a+1]);
					}
					k--; //decrement k
					i--; //decrement size
				}
			}	
		}			
	}
	//Display output
	int x;
	for(x = 0; x < i; x++){
		if(strcmp(command->args[0],"-c") == 0 || strcmp(command->args[0], "--count") == 0){	
			printf("\t%d ",counts[x]);
		}
		printf("%s",output[x]);

	}	

	return SUCCESS;
}
//IO REDIRECTION
int io_redirect(struct command_t *command){

	int fileNo;
	FILE *fptr;
	//IO REDIRECTION PART
	if(command->redirects[0] != NULL){ //IO REDIRECTION OP 1 "<"
		fptr = fopen(command->redirects[0], "r"); //open a file with the parsed name for reading
		if(fptr == NULL) { //null check
			printf("File not found\n"); 
			return EXIT;
		}
		char line[100]; //string to read from the file
		fileNo = fileno(fptr);	//taking int fileno to be able to dup2()
		if(dup2(fileNo, STDIN_FILENO) < 0){ //null check
			return EXIT;
		}
		while(fgets(line, 100, fptr) != NULL){	//read lines and print them
			printf("%s", line);	
		}
	}
	else if(command->redirects[1] != NULL) { //IO REDIRECTION OP 2 ">"
		fptr = fopen(command->redirects[1], "w"); //Open a file for writing
		fileNo = fileno(fptr);

		if(dup2(fileNo, STDOUT_FILENO) < 0){ //null check
			return EXIT;
		}
	}
	else if (command->redirects[2] != NULL){ //IO REDIRECTION OP 3 ">>"
		fptr = fopen(command->redirects[2], "a"); //Open a file for appending
		fileNo = fileno(fptr);

		if(dup2(fileNo, STDOUT_FILENO) < 0){ //null check
			return EXIT;
		}
	}		
	close(fileNo); //close the files after finishing 
}


//PIPING COMMANDS
int pipe_command(struct command_t *command, char *pathname, int *fd){
	//printf("Hello from temp command %s\n", temp_command->name);
	//printf("rdir0 %s, rdir1 %s\n", command->redirects[0],  command->redirects[1]);		
	struct command_t *nextCommand = command->next;

	//PIPE PART
	if(pipe(fd) == -1){
		return EXIT;
	}
	strcpy(pathname, "/usr/bin/");
	pid_t pid2 = fork(); //fork child to be executed

	if(pid2 == -1){ //fork fail check
		printf("Fork error\n");
		return EXIT;
	}

	else if(pid2 == 0){ //child process 	
		dup2(fd[1], 1); //Writing end of pipe takes input from STDOUT
	} else {
		close(fd[1]);	//Close writing end
		dup2(fd[0],0); //Reading end takes input from STDIN
		close(fd[0]); //Close reading end
		strcat(pathname, nextCommand->name); //Concat pathname with command->name

		int i = 0;
		char *temp;

		while(i < nextCommand->arg_count){ //Indentication of command->args
			temp = nextCommand->args[i];
			if(i == 0){
				nextCommand->args[i] = nextCommand->name;
			}
			nextCommand->args[i+1] = temp;
			i++;
		}
		execv(pathname, nextCommand->args); //Call piped command
		return SUCCESS;
	}
	if(nextCommand->next != NULL){
		int newfd[2];
		pipe_command(nextCommand, pathname, newfd);
	}

}
int chatroom(struct command_t *command){

	printf("Welcome to %s %s\n", command->args[0], command->args[1]);
	char filename[200];
	struct stat stats;
	
	//Editing the name of the folder to be created
	strcpy(filename, "/tmp/");
	strcat(filename, "chatroom-");
	strcat(filename, command->args[0]);
	char *room = strdup(filename); //setting it to a new str named room

	if(stat(room, &stats) == -1){ //If it doesn't exist 	
		mkdir(room, 0700); //create a new directory
	}

	strcat(filename, "/"); //Continue editing to be able to add pipes  
	strcat(filename, command->args[1]); //append the user name to the str
	char *user = strdup(filename); //set it to a new str named user

	if(access(user, F_OK) != 0){ //If it doesn't exist
		mkfifo(user, 0666); //Create a new named pipe
	}

	//Inputs that will be needed while reading stdin
	char *inputStr = malloc(sizeof(char)* 256); //Input str
	size_t len = 256; //size of read
	ssize_t	lineSize = 0; //return value;

	pid_t pid1, pid2;
	pid1 = fork();	
	if(pid1 == 0) { 
		while(true) { //Continiously read from stdin
			lineSize = getline(&inputStr, &len, stdin);
			if(lineSize > 1){ //When pressed enter and size > 1
				struct dirent *dir; 
				DIR *dptr;
				dptr = opendir(room); //Open directory	 
				if(dptr){ //If ptr is not null
					while((dir = readdir(dptr)) != NULL){ //Start iterating over files in folder
						pid2 = fork(); //Fork one more time to be able to synchronously write to pipes
						if(pid2 == 0) {
							if(strcmp(".", dir->d_name) == 0 || strcmp("..",dir->d_name) == 0){ //Default subfolders 
								exit(0);
							
							} else if(strcmp(command->args[1], dir->d_name) == 0) { //The user that called the method 
								exit(0);
							
							} else {
								char *temp = malloc(sizeof(char)*256); //Temp str to hold current user
								//Appopriate appendings to the username
								strcat(temp, room); 
								strcat(temp,"/");
								strcat(temp, dir->d_name);
									
								char *msg = malloc(sizeof(char)*512); //The message that will be written to the pipe
								
								//Appopriate appendings to the msg
								strcat(msg, "["); 
								strcat(msg, command->args[0]);
								strcat(msg, "] ");
								strcat(msg, command->args[1]);
								strcat(msg, ": ");
								strcat(msg, inputStr); //Append inputStr to the message at the end


								int fd1 = open(temp, O_WRONLY); //Open the pipe 
								write(fd1, msg, strlen(msg)*sizeof(char)); //Write into it
								close(fd1); //Close the pipe
								//printf("[%s] %s: %s", command->args[0], command->args[1],inputStr);

								fflush(stdout); //fflush to clear the output buffer
								free(msg); //free allocated msg
								free(temp); //free the allocated user str
								exit(0);
							}
						}
					}
				}
				closedir(dptr); //closing directory	
			}
			free(inputStr); //free allocated input str
			inputStr = malloc(sizeof(char)*256); //reallocate
		}
	} else 
	{
		char *str; 
		while(true){ //Continiously
			str = (char *)malloc(sizeof(char)*256); //Malloc str
			int fd0 = open(user,O_RDWR); //open pipe
			read(fd0,str,128); //read from it 

			close(fd0); //close pipe
		
			printf("%s", str); //print the read message
			fflush(stdout); //clear buffer 
			free(str); //free allocated memory
		}
	}
	return SUCCESS;
}

int process_command(struct command_t *command)
{
	char pathname[100] = "/usr/bin/";
	int r;
	if (strcmp(command->name, "")==0) return SUCCESS;

	if (strcmp(command->name, "exit")==0)
		return EXIT;

	if (strcmp(command->name, "cd")==0)
	{
		if (command->arg_count > 0)
		{
			r=chdir(command->args[0]);
			if (r==-1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}
	if(strcmp(command->name, "uniq") == 0){
		myuniq(command);
		return SUCCESS;
	}


	if(strcmp(command->name, "wiseman") == 0){
		wiseman(command);
		return SUCCESS;
	}


	pid_t pid=fork();
	if (pid==0) // child
	{
		/// This shows how to do exec with environ (but is not available on MacOs)
		// extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec


		if(strcmp(command->name, "chatroom") == 0){
			chatroom(command);
			return SUCCESS;
		}
		// increase args size by 2
		command->args=(char **)realloc(
				command->args, sizeof(char *)*(command->arg_count+=2));

		// shift everything forward by 1
		for (int i=command->arg_count-2;i>0;--i)
			command->args[i]=command->args[i-1];

		if(strcmp(command->name, "guessthenumber") == 0) {	
			guessTheNumber(command);
			return SUCCESS;
		}

		if(strcmp(command->name, "rps") == 0){
			rps(command);
			return SUCCESS;
		}
		// set args[0] as a copy of name
		command->args[0]=strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count-1]=NULL;

		if(strcmp(command->name, "fortune") == 0 || strcmp(command->name, "cowsay") == 0){
			strcpy(pathname, "/usr/games/");
			strcat(pathname, command->name);	
			execv(pathname, command->args);
		}	
		io_redirect(command);		
		if(command->next != NULL){
			int fd[2];
			//printf("pathin of pipe is %s\n", pathname);
			pipe_command(command, pathname, fd);
		}
		if(strcmp(pathname, "/usr/bin/") == 0){
			strcat(pathname, command->name);
			execv(pathname, command->args);
		}

		exit(0);
	}
	else {
		int status;
		if(!command->background){
			waitpid(pid, NULL, 0);
		}
		return SUCCESS;

	}
	printf("-%s: %s: command not found\n",sysname, command->name);
	return UNKNOWN;
}	
