#define MAX_CMD_LENGTH 100
#define MAX_DIR_LENGTH 1000
#define MAX_USERNAME_LENGTH 100

#define NONE "\033[m"
#define CYAN "\033[0;36m"
#define YELLOW "\033[1;33m"
#define DARK_GRAY "\033[1;30m"
#define LIGHT_PURPLE "\033[1;35m"
#define GREEN "\033[0;32;32m"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

char username[MAX_USERNAME_LENGTH];
char cwd[MAX_DIR_LENGTH];// 当前工作目录
char* cmd[MAX_CMD_LENGTH];

int cmdCount;
int isBg;// is background/contain & or not

pid_t pid[MAX_CMD_LENGTH];
int forkNum;

void prompt();// 打印最开始那一堆
void getCmd();// get命令
void handlerCmd(char *cmd[]);// 处理指令
void cd(char *cmd[]);// cd命令
void exitsh();// exit命令
void myfork(char *cmd[]);// 执行指令
void mutifork(char *cmd[]);// pipeline
void signalHandlering();
void sigint_handler(int sig);//SIGINT信号量的handler
void sigtstp_handler(int sig);//SIGTSTP信号量的handler
void sigchld_handler(int sig);//SIGCHLD信号量的handler 回收zombies process
void fg();
void bg();
void killProcess();

int main(int argc, const char * argv[]) {
    
    setpgid (getpid(), getpid());
    tcsetpgrp (STDIN_FILENO, getpgrp());
    
    printf("Welcome to mysh by 0440062!\n");
    while(1){
        
        //printf("shell gid:%d\n",getpgrp());
        
        signalHandlering();// 信号量
        
        prompt();
        getCmd();
        
        
    }
    return 0;
}

void prompt(){
    getlogin_r(username,MAX_USERNAME_LENGTH);
    getcwd(cwd,MAX_DIR_LENGTH);
    printf(CYAN"%s"NONE" in "YELLOW"%s\n"LIGHT_PURPLE"mysh> "NONE,username,cwd);
}

void getCmd(){
    
    int i;
    for(i=0;i<cmdCount;i++){
        cmd[i]=NULL;
    }
    
    char tmp[1000];
    fgets(tmp,1000,stdin);
    tmp[strlen(tmp)-1]='\0';
    fflush(stdin);
    fflush(stdout);
    
    if(strlen(tmp)>0){
        
        char **p;
        char *q;
        
        p = cmd;
        q = tmp;
        
        // 跳过最开始的空格
        while(*q == ' '){
            q++;
        }
        
        // 分隔命令
        cmdCount = 0;
        while(*q != '\0'){
            
            
            *p++ = q++;
            cmdCount++;
            
            while(*q != ' ' && *q != '\0'){
                q++;
            }
            
            if(*q != '\0'){
                *q++ = '\0';
            }
            
            // 跳过空格
            while(*q==' '){
                q++;
            }
            
        }
        
        // 判断最后一个是不是&，决定是否是background
        if(strcmp(cmd[cmdCount-1], "&")==0){
            isBg=1;
            cmd[cmdCount-1]=NULL;
            cmdCount--;
        } else{
            isBg=0;
        }
        
        // 处理命令
        handlerCmd(cmd);
    }
    
}

void handlerCmd(char *cmd[]){
    // debug
    //printf("command: %s\n",cmd[0]);
    //    printf("%d\n",cmd[1][0]);
    
    // 这步是解决ls有时候的bug
    if(cmd[1]!=NULL&&strlen(cmd[1])==0)
        cmd[1]=NULL;
    
    if(strcmp(cmd[0], "cd")==0||strcmp(cmd[0], "CD")==0){
        cd(cmd);
    } else if(strcmp(cmd[0], "exit")==0){
        exitsh();
    } else if(strcmp(cmd[0], "fg")==0){
        fg();
    } else if(strcmp(cmd[0], "bg")==0){
        bg();
    } else if(strcmp(cmd[0], "kill")==0){
        killProcess();
    } else{
        myfork(cmd);
    }
}

void cd(char *cmd[]){
    
    char tmp[MAX_DIR_LENGTH+50];
    
    if(cmd[1][0]=='/'){
        strcpy(tmp, cmd[1]);
    }else if(cmd[1][0]=='~'){
        // mac os x是这样，其实我不太了解确定其它unix/linux系统是不是这样
        strcpy(tmp, "/Users/");
        strcat(tmp, username);
    }else{
        strcpy(tmp, cwd);
        strcat(tmp, "/");
        strcat(tmp, cmd[1]);
    }
    
    int result=chdir(tmp);
    if(result==-1){
        printf("-mysh: cd %s: No such file or directory\n",cmd[1]);
    }
    
}

void exitsh(){
    printf("goodbye\n");
    exit(0);
}

void myfork(char *cmd[]){
    signal (SIGTTOU, SIG_IGN);
    
    int status;
    forkNum = 1;
    int i=0;
    // debug
    // printf("cmdCount: %d\n",cmdCount);
    for(;i<cmdCount;i++){
        if(strcmp(cmd[i], "|")==0){
            forkNum++;
        }
    }
    
    // 没有pipeline
    if(forkNum < 2){
        if((pid[0] = fork()) < 0){
            perror("fork");
            exit(1);
        }else if(pid[0] == 0){
            // 子进程
            setpgid(0, getpid());
            
            if(isBg==0){
                printf(GREEN"Command executed by pid=%d"NONE"\n",getpid());
                tcsetpgrp (STDIN_FILENO, getpgrp());
            } else{
                printf(GREEN"Command executed by pid=%d in background"NONE"\n",getpid());
            }
    
            if(execvp(cmd[0], cmd) < 0){
                perror("");
                exit(1);
            }
            exit(0);
        }
    }
    
    // 父进程
    if(forkNum < 2){
        if(isBg==0){
            // setpgid后要把子进程组提到前台，这里也做一次
            setpgid(pid[0], pid[0]);
            tcsetpgrp(STDIN_FILENO, getpgid(pid[0]));
            
            waitpid(pid[0], &status, WUNTRACED);
            
            // 还控制权
            tcsetpgrp(STDIN_FILENO, getpid());
        }
        
    }else{
        mutifork(cmd);
    }
}


void mutifork(char *cmd[])
{
    int pipefd[MAX_CMD_LENGTH][2];
    int i, j;
    int count;
    int status = 0;
    char **arg_child[MAX_CMD_LENGTH];
    char **p;
    char ***q;
    
    
    count = 0;
    p = cmd;
    q = arg_child;
    while(*p != NULL && p != NULL){
        *q++ = p;
        count++;
        while(*p != NULL && strcmp(*p, "|") != 0){
            p++;
        }
        
        if(*p != NULL){
            *p++ = NULL;
        }
    }
    *q = NULL;
    
    // fork一大堆子进程
    for(i = 0; i < count; i++){
        if(pipe(pipefd[i]) < 0){
            perror("pipe");
            return;
        }
        
        if((pid[i] = fork()) < 0){
            fprintf(stderr, "%s:%d: fork() failed: %s\n", __FILE__,
                    __LINE__, strerror(errno));
            return ;
        }else if(pid[i] == 0){
            
            setpgid(0, pid[0]);
            
            if(i == 0){
                close(pipefd[i][0]);
                
                if(dup2(pipefd[i][1], STDOUT_FILENO) < 0){
                    perror("dup2 failed");
                    return;
                }
            }else if(i == count - 1){
                for(j = 0; j < i - 1; j++){
                    close(pipefd[j][1]);
                    close(pipefd[j][0]);
                }
                close(pipefd[j][1]);
                close(pipefd[i][0]);
                
                if(dup2(pipefd[j][0], STDIN_FILENO) < 0){
                    perror("dup2 failed");
                    return;
                }
            }else{
                for(j = 0; j < i - 1; j++){
                    close(pipefd[j][1]);
                    close(pipefd[j][0]);
                }
                close(pipefd[j][1]);
                close(pipefd[i][0]);
                
                if(dup2(pipefd[j][0], STDIN_FILENO) < 0){
                    perror("dup2 failed");
                    return;
                }
                if(dup2(pipefd[i][1], STDOUT_FILENO) < 0){
                    perror("dup2 failed");
                    return;
                }
            }
            
            if(isBg==0){
                printf(GREEN"Command executed by pid=%d"NONE"\n",getpid());
                tcsetpgrp (STDIN_FILENO, getpgrp());
            } else{
                printf(GREEN"Command executed by pid=%d in background"NONE"\n",getpid());
            }
            
            if(execvp(arg_child[i][0], arg_child[i]) < 0){
                fprintf(stderr, "%s:%d: fork() failed: %s\n", __FILE__,
                        __LINE__, strerror(errno));
                
                exit(1);
            }
            
            exit(0);
            
        }
    }
    
    // 父进程
    for(i = 0; i < count; i++){
        close(pipefd[i][0]);
        close(pipefd[i][1]);
    }
    
    if(isBg==0){
        // setpgid后要把子进程组提到前台，这里也做一次
        for(i = 0; i < count; i++){
            setpgid(pid[i], pid[0]);
        }
        tcsetpgrp(STDIN_FILENO, getpgid(pid[0]));
        
        for(i = 0; i < count; i++){
            waitpid(pid[i], &status, WUNTRACED);
        }
        
        // 还控制权
        tcsetpgrp(STDIN_FILENO, getpid());
        
    }
    
}


void signalHandlering(){
    // sigint
    struct sigaction sigint_action={
        .sa_handler=&sigint_handler,
        .sa_flags=0
    };
    sigemptyset(&sigint_action.sa_mask);
    sigaction(SIGINT,&sigint_action,NULL);
    
    
    // sigtstp
    struct sigaction sigtstp_action={
        .sa_handler=&sigtstp_handler,
        .sa_flags=0
    };
    sigemptyset(&sigtstp_action.sa_mask);
    sigaction(SIGTSTP,&sigtstp_action,NULL);
    
    
    // sigchld
    struct sigaction sigchld_action={
        .sa_handler=&sigchld_handler,
        .sa_flags=0
    };
    sigemptyset(&sigchld_action.sa_mask);
    sigaction(SIGCHLD,&sigchld_action,NULL);
}

void sigint_handler(int sig){
    int result=kill(-1*pid[0], SIGINT);
    if(result==-1){
        perror( "kill" );
    }
    
    tcsetpgrp(STDIN_FILENO, getpid());
    
}

void sigtstp_handler(int sig){
    int result=kill(-1*pid[0], SIGTSTP);
    if(result==-1){
        perror( "suspend" );
    }
    
    tcsetpgrp(STDIN_FILENO, getpid());
    
}

void sigchld_handler(int sig){
    int status;
    waitpid(-1, &status, WNOHANG);
}

void fg(){
    signal (SIGTTOU, SIG_IGN);
    
    int pidtmp=0;
    int i;
    for(i=0;i<strlen(cmd[1]);i++){
        pidtmp*=10;
        pidtmp+=cmd[1][i]-'0';
    }
    
    // 记录前台进程组id
    pid[0]=getpgid(pidtmp);
    
    int result=kill(-1*getpgid(pidtmp), SIGCONT);
    if(result==-1){
        perror( "kill" );
    }
    tcsetpgrp(STDIN_FILENO, getpgid(pidtmp));
    
    // 前台执行，所以要wait
    int status;
    waitpid(-1*getpgid(pidtmp), &status, WUNTRACED);
    
    // 还控制权
    tcsetpgrp(STDIN_FILENO, getpid());
    
}

void bg(){
    int pidtmp=0;
    int i;
    for(i=0;i<strlen(cmd[1]);i++){
        pidtmp*=10;
        pidtmp+=cmd[1][i]-'0';
    }
    
    int result=kill(-1*getpgid(pidtmp), SIGCONT);
    if(result==-1){
        perror( "kill" );
    }
}

void killProcess(){
    int pid=0;
    int i;
    if(cmd[1][0]=='-')
        i=1;
    else
        i=0;
    
    //printf("length: %lu str: %s\n",strlen(cmd[1]),cmd[1]);
    
    for(i=0;i<strlen(cmd[1]);i++){
        //printf("%c\n",cmd[1][i]);
        pid*=10;
        pid+=cmd[1][i]-'0';
    }
    if(cmd[1][0]=='-')
        pid*=-1;
    
    int result=kill(pid, SIGINT);
    if(result==-1){
        perror( "kill" );
    }
    
    // 处理一个神奇的bug。。
    char tmp[1000];
    fgets(tmp, 1000, stdin);
    
}


