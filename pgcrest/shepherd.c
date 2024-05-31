#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <uv.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>

uv_signal_t child_signal_sigchld;
uv_signal_t child_signal_sighup;
uv_signal_t child_signal_sigterm;


uv_loop_t* parent_loop;
uv_loop_t child_loop;

uv_pid_t* chld;
int w;
char* program = NULL;
char** args;

/* Parse main options */
typedef struct {
	char* string;
	int num_of_workers;
} main_opt;

#ifndef CONFIG_FILE
#define CONFIG_FILE "etc/pgcrest.conf"
#endif

void usage()
{
    printf( "Usage: shepherd [-h] [-f CONFIG] [-w number_of_workers name_of_program]\n"
            "-f CONFIG  load configuration from  CONFIG path instead  of default '%s'\n"
			"-w NUMBER PROGRAM_ADRESS put number of paralel processes\n", CONFIG_FILE);
	exit(1);
    return;
}


pid_t* create_paralell_processes(int w) {
    pid_t pid;
    pid_t* chld_arr = malloc(w * sizeof(pid_t));

	if(!chld_arr) {
		perror("malloc error\n");
		return NULL;
	}

	//printf("%p", chld_arr);
    
    for (int i = 0; i < w ; i++) {  
        switch(pid = fork()) {
            case 0:
                printf("Start %s %s %s \n", args[0], args[1], args[2]);
                execvp(args[0], args);
                perror("execvp\n");
                exit(1);
                break;
            case -1:
                perror("fork error\n");
                break;
            default:
                chld_arr[i] = pid;
                break;
        }
    }

    return chld_arr;
}

void parent_handler(uv_signal_t* handle, int signum) {
    pid_t pid;
	if(!chld){
		perror("Zero pointer chld_pids array\n");
		return;
	}
    
	switch(signum) {
		case SIGCHLD:
            printf("Got sigchild\n");
			for (int child = 0; child < w; child++) {
				if (waitpid(chld[child], NULL, WNOHANG) > 0) {
					if((pid = fork()) == 0) {
                        //printf("%s %s\n", program, args);
						execvp(args[0], args);
						perror("execvp");
                        exit(1);
					} else {
						printf("%d - was forked\n", pid);
						chld[child] = pid;
					}
                }
			}
			break;
		case SIGHUP:
			printf("Parent process - %d got SIGHUP\n", getpid());

			/*kill old child processes*/
			for(int child = 0; child < 1; child++)
				uv_kill(chld[child], SIGTERM);

			/*Wait till all processes will finish*/
			// for(int child = 0; child < w; child++)
			// 	waitpid(chld[child], NULL, WNOHANG);
            printf("All child processes terminated\n");
            printf("New %d child processes were created\n", w);
			break;

		case SIGTERM:
			printf("Parent process - %d got SIGTERM\n", getpid());

			/*Send signal SIGTERM for all child processess*/
			for(int child = 0; child < w; child++)
				uv_kill(chld[child], SIGTERM);

			/*Wait till all processes will finish*/
			printf("All child processes terminated\n");
			printf("Parent process terminated\n");
			exit(0);
			break;
		default:
			break;
	}
}


int main(int argc, char *argv[]) {
    char opt;
    char *file_name = NULL;

    //i dont know why i parse all args if i only need number of workers
    while ((opt = getopt(argc, argv, "w:")) != -1) {
        switch (opt) {
            case 'w':
                w = atoi(optarg);
                break;
            case 'f':
                file_name = strdup(optarg);
                break;
            default:
                printf("Wrong flag\nUse -h for help\n");
                break;
        }
    }
    printf("Shiftint to %d: %s %s %s\n", optind, argv[optind], argv[optind+1], argv[optind+2]);
    args = argv + optind;
    //argv[argc] = 0;

    //printf("w = %d program = %s file_name = %s\n", w, program, file_name);

	parent_loop = uv_default_loop();

	chld = create_paralell_processes(w);

	uv_signal_init(parent_loop, &child_signal_sigchld);
    uv_signal_init(parent_loop, &child_signal_sighup);
    uv_signal_init(parent_loop, &child_signal_sigterm);

    uv_signal_start(&child_signal_sigchld, parent_handler, SIGCHLD);
	uv_signal_start(&child_signal_sigterm, parent_handler, SIGTERM);
	uv_signal_start(&child_signal_sighup, parent_handler, SIGHUP);

	uv_timer_t periodic;

	uv_timer_init(parent_loop, &periodic);

	//uv_timer_start(&periodic, periodic_cb, 1000, 1000*iniparser_getint(_config, "pgpool:pingrate", 10));
    //uv_kill(uv_os_getpid(), SIGHUP);

	int r = uv_run(parent_loop, UV_RUN_DEFAULT);

    return 0;
}