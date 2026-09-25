#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include "mmake_parser.h"

#define DEBUG_EXPR(expr) fprintf(stderr, "%s:%d:%s(): %s: 0x%llX\n", __FILE__, __LINE__, __func__, #expr, (unsigned long long)(expr))
#define DEBUG_STR(str) fprintf(stderr, "%s:%d:%s(): %s: %s\n", __FILE__, __LINE__, __func__, #str, (char*)(str))

bool make_target(mmake_rules* rules, const char* target);
pid_t exec_command(char *const argv[]);
bool exec_and_wait(char *const argv[]);
void echo_cmd(char *const argv[]);
int64_t file_mod_time(const char* path);
#define FILE_MOD_TIME_FILE_NOT_FOUND INT64_MIN
#define FILE_MOD_TIME_ERROR (INT64_MIN + 1)

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
                fprintf(stderr, "Usage: mmake [-f MMAKEFILE] [TARGET ...]\n");
                exit(EXIT_FAILURE);
                break;
        }
    }
    size_t nrof_targets = argc - optind; //Number of [TARGET ...] arguments passed on the command line
    const char *const *targets = (const char *const *)&argv[optind];

    FILE* file = fopen(filename, "r");
    if(!file)
    {
        perror(filename);
        exit(EXIT_FAILURE);
    }

    mmake_rules* rules = parse_mmakefile(file);
    if(!rules)
    {
        fprintf(stderr, "mmake: error parsing %s (syntax malformed?)\n", filename);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    fclose(file);

    const char* default_target;
    if(nrof_targets == 0)
    {
        default_target = get_default_target(rules);
        targets = &default_target;
        nrof_targets = 1;
    }

    for(size_t i = 0; i < nrof_targets; ++i)
    {
        if(!make_target(rules, targets[i]))
        {
            delete_mmake_rules(rules);
            exit(EXIT_FAILURE);
        }
    }

    delete_mmake_rules(rules);
    return 0;
}

/*
    TODO: document
*/
bool make_target(mmake_rules* rules, const char* target)
{
    bool rebuild_needed = false;

    const int64_t target_timestamp = file_mod_time(target);
    if(FILE_MOD_TIME_ERROR == target_timestamp) return false;
    if(FILE_MOD_TIME_FILE_NOT_FOUND == target_timestamp) rebuild_needed = true;

    rule* rule = get_target_rule(rules, target);
    if(!rule)
    {
        if(rebuild_needed)
        {
            fprintf(stderr, "mmake: *** No rule to make target '%s'.  Stop.\n", target);
            return false;
        }
        return true;
    }

    //Check dependencies
    for(const char *const* deps = get_rule_prereq(rule); *deps; ++deps)
    {
        const char *const dep = *deps;
        if(!make_target(rules, dep)) return false; //Something went wrong down the line
        const int64_t dep_timestamp = file_mod_time(dep);
        if(FILE_MOD_TIME_ERROR == dep_timestamp) return false;
        if(target_timestamp <= dep_timestamp) rebuild_needed = true; //Rebuild on equal timestamps to account for only second resolution
    }

    //TODO: optionally force rebuilds
    if(rebuild_needed)
    {
        char *const *cmd = get_rule_cmd(rule);
        //If we have a rule we should have a command line.
        //Otherwise parse_mmakefile() would have failed earlier.
        assert(cmd);
        assert(*cmd);
        //TODO: optionally silence command echoing
        echo_cmd(cmd);
        return exec_and_wait(cmd);
    } else
    {
        printf("mmake: Nothing to be done for '%s'.\n", target);
        return true;

        //TODO: Figure out when and how to print the appropriate user feedback message.
        //Apparently 'is up to date' should be printed whene there is a rule,
        //and 'Nothing to be done' when there isn't a rule. But only on top of
        //the recursion stack?
        // printf("mmake: '%s' is up to date.\n", target);
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
pid_t exec_command(char *const argv[])
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
bool exec_and_wait(char *const argv[])
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
    else fprintf(stderr, "mmake: exec_and_wait(): unknown error\n");
    return false;
}

/*
    TODO: document
*/
void echo_cmd(char *const argv[])
{
    size_t buf_size = 0;
    for(char *const *p = argv; *p; ++p)
    {
        buf_size += strlen(*p) + 1;
    }
    char buf[buf_size];
    char* bp = buf;
    for(char *const *p = argv; *p; ++p)
    {
        size_t len = strlen(*p);
        memcpy(bp, *p, len);
        bp += len;
        *(bp++) = ' ';
    }
    assert(bp == buf + sizeof(buf));
    *(bp - 1) = '\0';
    puts(buf);
}

/**
    Return the modification time of a file
    in UNIX time seconds.

    @param path Path to file
    @return UNIX timestamp seconds when file was last modified
        Special values FILE_MOD_TIME_FILE_NOT_FOUND if the file was not found
        or FILE_MOD_TIME_ERROR if some other error occurred.
*/
int64_t file_mod_time(const char* path)
{
    struct stat statbuf;
    if(stat(path, &statbuf) == -1)
    {
        if(ENOENT == errno)
        {
            return FILE_MOD_TIME_FILE_NOT_FOUND;
        } else
        {
            perror(path);
            return FILE_MOD_TIME_ERROR;
        }
    }
    //Lab spec only requires second precision
    return statbuf.st_mtim.tv_sec;
}
