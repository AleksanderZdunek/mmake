#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include "mmake_parser.h"

#define DEBUG_EXPR(expr) fprintf(stderr, "%s:%d:%s(): %s: 0x%llX\n", __FILE__, __LINE__, __func__, #expr, (unsigned long long)(expr))
#define DEBUG_STR(str) fprintf(stderr, "%s:%d:%s(): %s: %s\n", __FILE__, __LINE__, __func__, #str, (char*)(str))

bool make_target(mmake_rules* rules, const char* target);
pid_t exec_command(char** argv);
bool exec_and_wait(char* argv[]);

void debug_print_rule(mmake_rules* rules, const char* target);
void debug_print_rule(mmake_rules* rules, const char* target)
{
    DEBUG_STR(target);

    rule* rule = get_target_rule(rules, target);
    if(!rule)
    {
        DEBUG_STR("Rule not found");
        return;
    }

    const char** dependencies = get_rule_prereq(rule);
    for(; *dependencies; ++dependencies)
    {
        DEBUG_STR(*dependencies);
    }

    char** cmd = get_rule_cmd(rule);
    for(; *cmd; ++cmd)
    {
        DEBUG_STR(*cmd);
    }
}

int main(int argc, char* argv[])
{
    const char* filename = "mmakefile";
    int opt;
    while((opt = getopt(argc, argv, "f:")) != -1)
    {
        switch(opt)
        {
            case 'f':
                filename = optarg;
                break;
            default:
                //TODO: Document more options
                fprintf(stderr, "Usage: mmake [-f MMAKEFILE]\n");
                exit(EXIT_FAILURE);
                break;
        }

    }

    FILE* file = fopen(filename, "r");
    if(!file)
    {
        perror(filename);
        exit(EXIT_FAILURE);
    }

    mmake_rules* rules = parse_mmakefile(file);
    if(!rules)
    {
        fprintf(stderr, "mmake: No rule found in %s\n", filename);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    fclose(file);

    //TODO: the rest of the program
    const char* default_target = get_default_target(rules);
    // debug_print_rule(rules, default_target);
    if(make_target(rules, default_target))
    {
        DEBUG_STR("Target made successfully");
        DEBUG_STR(default_target);
    } else
    {
        DEBUG_STR("Error making target");
        DEBUG_STR(default_target);
    }

    delete_mmake_rules(rules);
    return 0;
}

/*
    TODO: document
*/
bool make_target(mmake_rules* rules, const char* target)
{
    rule* rule = get_target_rule(rules, target);
    if(!rule)
    {
        DEBUG_STR("Rule not found");
        fprintf(stderr, "mmake: *** No rule to make target '%s'.  Stop.\n", target);
        return false;
    }

    //TODO: deal with prerequisites

    char** cmd = get_rule_cmd(rule);
    if(!cmd)
    {
        printf("make: Nothing to be done for '%s'.\n", target);
        return true;
    } else
    {
        //TODO: echo command
        return exec_and_wait(cmd);
    }
}

/**
    Fork and exec command

    @param argv Pointer to array of string pointers. The first string pointer
        argv[0] is the command to execute. The rest are arguments to the
        command. The last string pointer shall be NULL.

    @return Process ID of the child process, to the parent process
            -1 on error
            Does not return in child process on successful exec
*/
pid_t exec_command(char* argv[])
{
    const pid_t pid = fork();
    if(pid == -1) //Error
    {
        perror("mmake: fork() error");
    }
    else if(pid == 0) //Child
    {
        execvp(argv[0], argv);
        perror(argv[0]);
        return -1;
    }
    //Parent
    return pid;
}

/**
    Fork and exec command, and wait for the child process to terminate.

    @param argv Pointer to array of string pointers. The first string pointer
    argv[0] is the command to execute. The rest are arguments to the
    command. The last string pointer shall be NULL.

    @return true if command process exited with status code 0
            false on error or if command process exited with non-zero status code
*/
bool exec_and_wait(char* argv[])
{
    const pid_t pid = exec_command(argv);
    if(pid == -1) return false;
    int wstatus;
    if(waitpid(pid, &wstatus, 0) == -1)
    {
        perror("mmake: waitpid() error");
        return false;
    }
    //Check child exit status
    if(WIFEXITED(wstatus))
    {
        if(WEXITSTATUS(wstatus) == 0)
        {
            return true;
        }
        fprintf(stderr, "mmake: command exited with code %d\n", WEXITSTATUS(wstatus));
    }
    else if(WIFSIGNALED(wstatus))
    {
        fprintf(stderr, "mmake: command terminate terminated by signal %d\n", WTERMSIG(wstatus));
    }
    return false;
}
