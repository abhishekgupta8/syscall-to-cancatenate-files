#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>

#define __NR_xconcat	349	/* our private syscall number */

int main(int argc, char *argv[])
{
	 /* structure for packing arguments */	
	typedef struct args
	{
		char *outfile;
		char **infiles;
		unsigned int infile_count;
		int oflags;
		mode_t mode;
		unsigned int flags;
	}args;                

	int option, rc, i, argslen, help=0;
	args params;
	
	/* setting default values for parameters*/
	params.oflags = 0;
	params.mode = 0550;
	params.flags = 0x00;

	/* getting all arguments */
	while( ( option = getopt( argc, argv, "actem:ANPh" ) ) != -1 )
	{	
 		switch(option)
		{	
			/* checking for oflags and appending */
			case 'a' : params.oflags = params.oflags | O_APPEND;
			break;
			
			case 'c' : params.oflags = params.oflags | O_CREAT;
			break;
			
			case 't' : params.oflags = params.oflags | O_TRUNC;
			break;
			
			case 'e' : params.oflags = params.oflags | O_EXCL;
			break;
			
			/* checking for mode and validating it */	
			case 'm' : params.mode = strtol(optarg, NULL, 8);
			
			/* validation for 4 digit number */
			if(strlen(optarg) > 4){
				errno=EINVAL;
				perror(
			"Error: The arument to mode is invalid.\nError");
				exit(errno);
			}
			
			/* validation for octal digit */
			for(i=0;i<strlen(optarg);i++){
			       if(!((isdigit(*(optarg+i))) && (*(optarg+i))<56))
				{
					errno=EINVAL;
					perror(
			"Error: The argument to mode is invalid.\nError");
					exit(errno);
				}
			}

			break;
			
			/* checking for flags and validating them
			only one flag can be selected */
			case 'A' :
			params.flags += 0x04;
			break;

			case 'N' : if( (params.flags >> 1 == 1) || 
					((params.flags) == 0x06)){
				errno=EINVAL;
				perror(
				"Error: --N cannot be used with --P\nError");
				exit(errno);
			}
			params.flags += 0x01;
			break;

			case 'P' : if( (params.flags == 0x01) ||
					(params.flags == 0x05 )){
				errno=EINVAL;
				perror(
				"Error: --P cannot be used with --N\nError");
				exit(errno);
			}
			params.flags += 0x02;
			break;
			
			/* checking for help message */	
			case 'h' : help =1;
			break;

			case '?' :
			errno=EINVAL;
			perror("Error: Wrong flag(s) entered\nError");
			exit(errno);

		}

	}

	/* printing help message */
	if (help ==1){
		printf(
"xhw1 is used to concatenate any number of input files into one out file\n");
		printf("Syntax: ./xhw1 [flags] outfile infile1 infile2...\n");
		printf("Flags:\n");
		printf("-a: Append Mode\n");
		printf("-c: Create Mode\n");
		printf("-t: Truncate Mode\n");
		printf("-A: Atonmic Cancat Mode\n");
		printf("-N: Return number of files instead of bytes written\n");
		printf("-P: Return percentage of data written out\n");
		printf("-m ARG: Set default mode to ARG\n");
		exit(0);
	}
	
	/* cheacking for missing arguments */
	else if( argc < 3 )
	{
		errno=EINVAL;
		perror(
	"Correct Syntax:./xhw1 [flags] outfile infile1 infile2...\nError");
		exit(errno);
	}
	
	/* checking for missing files */
	else if ( argc - optind < 2 )
	{
		errno=EINVAL;
		perror(
		"Error: Output or input filenames missing\nError");
		exit(errno);
	}
	
	/* defaulting oflags if not set yet */
	if ( params.oflags == 0 )
		params.oflags = O_CREAT;
	
	/* calculating nmuber of input files */
	params.infile_count = argc-optind-1;
	
	/* restricting the number of infiles to 10 */
	if(params.infile_count > 10){
		errno = EPERM;
		perror("Error: Maximum 10 infiles can be specified\nError");
		exit(errno);
	}

	/* setting outfile */
	params.outfile=argv[optind];

	/* setting infiles */
	params.infiles=malloc(sizeof(char*)*params.infile_count);
	
	for(i=0;i<params.infile_count;i++)
	{
		params.infiles[i]=argv[optind+1+i];
	}
		
	void *dummy = (void *)(&params);
	argslen = sizeof(params);

	/* system call */
  	rc = syscall(__NR_xconcat, dummy, argslen);
	
	/* printing appropriate message */
	if (rc >= 0){
		printf("%d\n", rc);
	}
	else{
		printf("Error Number: %d\n", errno);
		perror("Error");
	}
	
	/* deallocating memory */
	free(params.infiles);

	exit(rc);

}
