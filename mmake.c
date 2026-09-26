/*
    (C) Aleksander Zdunek <redacted>@cs.umu.se
*/
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

struct cfg
{
    const char* filename; //Path to mmakefile
    bool silent; //Suppress echoing of build commands
    bool force_rebuild; //Always rebuild all targets
    size_t nrof_targets; //Number of [TARGET ...] arguments passed on the command line
    const char *const * targets; //Optional list of build targets
};

enum make_status
{
    MAKE_OK,            //Build command executed successfully
    UP_TO_DATE,         //Rule exists but target does not need rebuilding
    NOTHING_TO_BE_DONE, //Target exists but has no rule
    MAKE_ERROR,         //Something went wrong
    MAKE_STOP = MAKE_ERROR //Target missing but there's no rule. Handled like other errors.
};

enum make_status make_target(mmake_rules* rules, const char* target, bool force_rebuild, bool silent);
pid_t exec_command(char *const argv[]);
bool exec_and_wait(char *const argv[]);
void echo_cmd(const char *const argv[]);
int64_t file_mod_time(const char* path);
#define FILE_MOD_TIME_FILE_NOT_FOUND INT64_MIN
#define FILE_MOD_TIME_ERROR (INT64_MIN + 1)
struct cfg options(int argc, char* argv[]);

int main(int argc, char* argv[])
{
    struct cfg cfg = options(argc, argv);

    FILE* file = fopen(cfg.filename, "r");
    if(!file)
    {
        perror(cfg.filename);
        exit(EXIT_FAILURE);
    }

    mmake_rules* rules = parse_mmakefile(file);
    if(!rules)
    {
        fprintf(stderr, "mmake: error parsing %s (syntax malformed?)\n", cfg.filename);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    fclose(file);

    const char* default_target;
    if(cfg.nrof_targets == 0)
    {
        default_target = get_default_target(rules);
        cfg.targets = &default_target;
        cfg.nrof_targets = 1;
    }

    //Build targets
    for(size_t i = 0; i < cfg.nrof_targets; ++i)
    {
        const char *const target = cfg.targets[i];
        switch(make_target(rules, target, cfg.force_rebuild, cfg.silent))
        {
            case NOTHING_TO_BE_DONE:
                if(!cfg.silent) printf("mmake: Nothing to be done for '%s'.\n", target);
                break;
            case UP_TO_DATE:
                if(!cfg.silent) printf("mmake: '%s' is up to date.\n", target);
                break;
            case MAKE_OK:
                break;
            default:
                assert(false); //If NDEBUG this falls through to error
            case MAKE_ERROR:
                delete_mmake_rules(rules);
                exit(EXIT_FAILURE);
        }
    }

    delete_mmake_rules(rules);
    return EXIT_SUCCESS;
}

/*
    TODO: document
*/
enum make_status make_target(mmake_rules* rules, const char* target, bool force_rebuild, bool silent)
{
    bool rebuild_needed = false;
    const int64_t target_timestamp = file_mod_time(target);
    if(FILE_MOD_TIME_ERROR == target_timestamp) return MAKE_ERROR;
    if(FILE_MOD_TIME_FILE_NOT_FOUND == target_timestamp) rebuild_needed = true;

    rule* rule = get_target_rule(rules, target);
    if(!rule)
    {
        if(rebuild_needed)
        {
            fprintf(stderr, "mmake: *** No rule to make target '%s'.  Stop.\n", target);
            return MAKE_STOP;
        }
        return NOTHING_TO_BE_DONE;
    }

    //Check dependencies
    for(const char *const* deps = get_rule_prereq(rule); *deps; ++deps)
    {
        const char *const dep = *deps;

        //Recursively traverse the prerequisites tree.
        //NOTE: No safety against prerequisite loops!
        //  A dependency loop will crash with infinite recursion.
        if(make_target(rules, dep, force_rebuild, silent) == MAKE_ERROR) return MAKE_ERROR;

        const int64_t dep_timestamp = file_mod_time(dep);
        if(FILE_MOD_TIME_ERROR == dep_timestamp) return MAKE_ERROR;
        if(target_timestamp < dep_timestamp) rebuild_needed = true;
    }

    if(rebuild_needed || force_rebuild)
    {
        char *const *cmd = get_rule_cmd(rule);
        //If we have a rule we should have a command line.
        //Otherwise parse_mmakefile() would have failed earlier.
        assert(cmd);
        assert(*cmd);
        if(!silent) echo_cmd((const char *const *)cmd);
        return exec_and_wait(cmd) ? MAKE_OK : MAKE_ERROR;
    } else
    {
        return UP_TO_DATE;
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
        fprintf(stderr, "mmake: *** [%s] Error %d\n", argv[0], WEXITSTATUS(wstatus));
    }
    else if(WIFSIGNALED(wstatus))
    {
        fprintf(stderr, "mmake: *** [%s] Error Signal %d\n", argv[0], WTERMSIG(wstatus));
    }
    else fprintf(stderr, "mmake: exec_and_wait(): unknown error\n");
    return false;
}

/*
    TODO: document
*/
void echo_cmd(const char *const argv[])
{
    size_t buf_size = 0;
    for(const char *const *p = argv; *p; ++p)
    {
        buf_size += strlen(*p) + 1;
    }
    char buf[buf_size];
    char* bp = buf;
    for(const char *const *p = argv; *p; ++p)
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

/**
    Parse command line options.
    May mutate the order arguments in argv pointer array

    @param argc Number of arguments
    @param argv Argument pointer array

    @return struct cfg holding options configuration.
        Does not return but exits with EXIT_FAILURE if incorrect options are passed.
*/
struct cfg options(int argc, char* argv[])
{
    struct cfg cfg = { .filename = "mmakefile" };
    int opt;
    while((opt = getopt(argc, argv, "Bf:s")) != -1)
    {
        switch(opt)
        {
            case 'B':
                cfg.force_rebuild = true;
                break;
            case 'f':
                cfg.filename = optarg;
                break;
            case 's':
                cfg.silent = true;
                break;
            default:
                fprintf(stderr, "Usage: mmake [-f MMAKEFILE] [-B] [-s] [TARGET ...]\n");
                exit(EXIT_FAILURE);
                break;
        }
    }
    cfg.nrof_targets = argc - optind; //Number of [TARGET ...] arguments passed on the command line
    cfg.targets = (const char *const *)&argv[optind];
    return cfg;
}
