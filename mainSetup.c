#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

 
#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */
 
/* The setup function below will not return any value, but it will just: read
in the next command line; separate it into distinct arguments (using blanks as
delimiters), and set the args array entries to point to the beginning of what
will become null-terminated, C-style strings. */

void setup(char inputBuffer[], char *args[],int *background)
{
    int length, /* # of characters in the command line */
        i,      /* loop index for accessing inputBuffer array */
        start,  /* index where beginning of next command parameter is */
        ct;     /* index of where to place the next parameter into args[] */
    
    ct = 0;
        
    /* read what the user enters on the command line */
    length = read(STDIN_FILENO,inputBuffer,MAX_LINE);  

    /* 0 is the system predefined file descriptor for stdin (standard input),
       which is the user's screen in this case. inputBuffer by itself is the
       same as &inputBuffer[0], i.e. the starting address of where to store
       the command that is read, and length holds the number of characters
       read in. inputBuffer is not a null terminated C-string. */

    start = -1;
    if (length == 0)
        exit(0);            /* ^d was entered, end of user command stream */

/* the signal interrupted the read system call */
/* if the process is in the read() system call, read returns -1
  However, if this occurs, errno is set to EINTR. We can check this  value
  and disregard the -1 value */
    if ( (length < 0) && (errno != EINTR) ) {
        perror("error reading the command");
	exit(-1);           /* terminate with error code of -1 */
    }

	printf(">>%s<<",inputBuffer);
    for (i=0;i<length;i++){ /* examine every character in the inputBuffer */

        switch (inputBuffer[i]){
	    case ' ':
	    case '\t' :               /* argument separators */
		if(start != -1){
                    args[ct] = &inputBuffer[start];    /* set up pointer */
		    ct++;
		}
                inputBuffer[i] = '\0'; /* add a null char; make a C string */
		start = -1;
		break;

            case '\n':                 /* should be the final char examined */
		if (start != -1){
                    args[ct] = &inputBuffer[start];     
		    ct++;
		}
                inputBuffer[i] = '\0';
                args[ct] = NULL; /* no more arguments to this command */
		break;

	    default :             /* some other character */
		if (start == -1)
		    start = i;
                if (inputBuffer[i] == '&'){
		    *background  = 1;
                    inputBuffer[i-1] = '\0';
		}
	} /* end of switch */
     }    /* end of for */
     args[ct] = NULL; /* just in case the input line was > 80 */

	for (i = 0; i <= ct; i++)
		printf("args %d = %s\n",i,args[i]);
} /* end of setup routine */
void history(char inputBuffer[], char historyBuffer[10][MAX_LINE]) {
    int i;

    // Kuyruktaki tüm elemanları bir ileri kaydır
    for (i = 9; i > 0; i--) {
        strncpy(historyBuffer[i], historyBuffer[i - 1], MAX_LINE);
    }

    // Yeni komutu en başa ekle
    strncpy(historyBuffer[0], inputBuffer, MAX_LINE);
}

void printHistory(char historyBuffer[10][MAX_LINE]){
    
    for(int i =0; i < 10 ; i++ ){
        for(int j = 0 ; j < MAX_LINE ; j++){
            printf("%d. %s", i, historyBuffer[i][j]);
        }
    }
}
int main(void)
{
            char inputBuffer[MAX_LINE]; /*buffer to hold command entered */
            char historyBuffer[10][MAX_LINE];
            int background; /* equals 1 if a command is followed by '&' */
            char *args[MAX_LINE/2 + 1]; /*command line arguments */
            pid_t pid;
            int historyOrder;
            while (1){
                        background = 0;
                        
                        printf("myshell: ");
                        /*setup() calls exit() when Control-D is entered */
                        
                        setup(inputBuffer, args, &background);
                        history(inputBuffer, historyBuffer);

                        if (strcmp(args[0], "history") == 0) {
                            if (args[1] == NULL) {
                                // Kullanıcı sadece "history" komutunu girdiğinde
                                printHistory(historyBuffer);
                            } else {
                                // Kullanıcı "history [index]" şeklinde bir komut girdiğinde
                                int index = atoi(args[1]); // Argümanı integer'a çevir

                                if (index >= 0 && index < 10 && historyBuffer[index][0] != '\0') {
                                    // Geçerli bir index girilmişse
                                    char *commandArgs[MAX_LINE / 2 + 1]; // Komutun argümanlarını tutacak
                                    char command[MAX_LINE];
                                    strncpy(command, historyBuffer[index], MAX_LINE);

                                    // Komutu parçalayarak args dizisine yerleştir
                                    int background = 0; // Arka plan işlemi için
                                    setup(command, commandArgs, &background);
                                    history(command,historyBuffer);
                                    // execv ile komutu çalıştır
                                    if (fork() == 0) {
                                        execv(commandArgs[0], commandArgs);
                                        perror("execv failed");
                                        exit(1);
                                    }
                                    if (background == 0) {
                                        wait(NULL); // Ön planda çalışıyorsa bekle
                                    }
                                } else {
                                    printf("Invalid history index!\n");
                                }
                            }
                        }
                        
                        /**  the steps are:
                        (1) fork a child process using fork()*/
                         /*
                        (2) the child process will invoke execv()*/
                        pid = fork();
                        if(pid == 0 ) {
                            execv(args[0], args);
                        }
                        /*(3) if background == 0, the parent will wait,
                        otherwise it will invoke the setup() function again. */
                        else if (pid > 0){
                         if (background == 0)
                            wait(NULL);
                        }
                        else 
                            setup(inputBuffer, args, &background);
                        


						
            }
}
