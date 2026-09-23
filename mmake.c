#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "mmake_parser.h"

#define DEBUG_EXPR(expr) fprintf(stderr, "%s:%d:%s(): %s: 0x%llX\n", __FILE__, __LINE__, __func__, #expr, (unsigned long long)(expr))
#define DEBUG_STR(str) fprintf(stderr, "%s:%d:%s(): %s: %s\n", __FILE__, __LINE__, __func__, #str, (char*)(str))

static void debug_print_rule(mmake_rules* rules, const char* target)
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
    debug_print_rule(rules, default_target);

    delete_mmake_rules(rules);
    return 0;
}
