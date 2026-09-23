#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "mmake_parser.h"

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

    delete_mmake_rules(rules);
    return 0;
}
