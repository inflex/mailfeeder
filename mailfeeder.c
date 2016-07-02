/* mailfeeder
 *
 * This is a wrapper which accepts Xamime output (ie, from stdin) and
 * redelivers it in a suitable fasion for the postfix 10025 port
 * delivery method.
 *
 * Started : 02/02/02
 * Finished (initial) 09/05/02
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "logger.h"
#include "mta-connect.h"

#define MF_VERSION "Mailfeeder 0.2.7 - January 10, 2005 - PLDaniels Software - http://www.pldaniels.com/mailfeeder"

#define MF_HELP "\
mailfeeder usage:\n\
	\n\
	mailfeeder -i <mailpack> -s <sender address> -r <receiver address> [-d <local domain>] [-S <SMTP server>] [-p <SMTP port>] [ -c <connection attempts>] [-v] [--version][--debug][--help]\n\
	\n\
	-i <mailpack>: Mailpack file required to be sent (use '-' for STDIN)\n\
	-s <sender address>: Email address of the sender of the email\n\
	-r <receiver address>: Email address of the receiver of the email\n\
	-d <local domain>: Domain to use for the SMTP HELO\n\
	-S <SMTP server>: Address of the SMTP server to send mailpack to\n\
	-p <SMTP port>: Port on SMTP server to connect to\n\
	-c <connection attempts>: Number of times to attempt to connect to the server\n\
	-v : Be verbose ( in errors too )\n\
	-x : Show the MTA/MUA conversation\n\
	--version: Displays Mailfeeder version\n\
	--debug: Activates debugging information during execution\n\
	--help: Displays this message\n\
	\n\
	By default, mailfeeder will attempt to send to the localhost server on port 25\n\
	with 5 connection attempts using 'localhost' for the HELO introduction\n\
	"
	struct MF_globals {
		char *server;
		char *sender;
		char *receiver;
		char *domain;
		char *mailpackpath;
		int port;
		int socket;
		int debug;
		int verbose;
		int show_conversation;
		int connection_attempts;
	};


char MF_default_helo_domain[]="localhost";
char MF_default_SMTP_server[]="localhost";
int MF_default_SMTP_port=25;
int MF_default_connect_attempts=5;


/*-----------------------------------------------------------------\
 Function Name	: MF_done
 Returns Type	: int
 	----Parameter List
	1. void, 
 	------------------
 Exit Codes	: 
 Side Effects	: 
--------------------------------------------------------------------
 Comments:
 
--------------------------------------------------------------------
 Changes:
 
\------------------------------------------------------------------*/
int MF_done(struct MF_globals *glb )
{
		if (glb->sender) free(glb->sender);
		if (glb->receiver) free(glb->receiver);
		if (glb->mailpackpath) free(glb->mailpackpath);

		return 0;
}

/*-----------------------------------------------------------------\
  Function Name	: MF_parse_parameters
  Returns Type	: int
  ----Parameter List
  1. struct MF_globals *glb, 
  2.  int argc, 
  3.  char **argv , 
  ------------------
  Exit Codes	: 
  Side Effects	: 
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int MF_parse_parameters( struct MF_globals *glb, int argc, char **argv )
{
	int i;

	if (argc < 2)
	{
		LOGGER_log("Insufficient paramters\n\%s\n%s", MF_VERSION, MF_HELP );
		exit(1);
	}

	for (i = 1; i<argc; i++) 
	{
		if ( argv[i][0] == '-' )
		{
			if (argv[i][1] == '-' )
			{
				char *p = &(argv[i][2]);

				if (strncmp(p,"version",strlen(p))==0) 
				{ 
					LOGGER_log("%s", MF_VERSION );
				} 
				else if (strncmp(p,"help", strlen(p))==0) 
				{
					LOGGER_log("%s", MF_HELP );
				}		 
				else if (strncmp(p,"debug", strlen(p))==0)
				{
					glb->debug = 1;
					MTACON_set_debug(1);
				} 
				else 
				{
					LOGGER_log("%s:%d:MF_parse_parameters:WARNING: Unknown parameter '%s'",FL,argv[i]);
				}
			} // End if there were two --'s
			else
			{
				// If there's only ONE hypen, then lets look at our double options

				switch (argv[i][1]) {
					case 'd':
						i++;
						glb->domain = strdup(argv[i]);
						break;

					case 's':
						i++;
						glb->sender = strdup(argv[i]);
						break;

					case 'r':
						i++;
						glb->receiver = strdup(argv[i]);
						break;

					case 'i':
						i++;
						glb->mailpackpath = strdup(argv[i]);
						break;

					case 'S':
						i++;
						glb->server = strdup(argv[i]);
						break;

					case 'p':
						i++;
						glb->port = atoi(argv[i]);
						if ((glb->port < 1)||(glb->port > 65300))
						{
							LOGGER_log("%s:%d:MF_parse_paramters:ERROR: Specified SMTP port '%s' is out of range [ 1 - 65300 ]", FL, argv[i]);
							exit(1);
						}
						break;

					case 'c':
						i++;
						glb->connection_attempts = atoi(argv[i]);
						if (glb->connection_attempts < 1)
						{
							LOGGER_log("%s:%d:MF_parse_parameters:WARNING: Connection attempts less than 1, setting to %d by default", FL, MF_default_connect_attempts);
							glb->connection_attempts = MF_default_connect_attempts;
						}

					case 'v':
						glb->verbose = 1;
						break;

					case 'x':
						glb->show_conversation = 1;
						MTACON_set_conversation(1);
						LOGGER_log("Displaying SMTP conversation:");
						break;

					default:
						LOGGER_log("%s:%d:MF_parse_parameters:WARNING: Unknown flag '%s'", FL, argv[i]);
						break;
				}
			} // One-hypen test
		}	

	}

	return 0;

}




/*-----------------------------------------------------------------\
  Function Name	: MF_init
  Returns Type	: int
  ----Parameter List
  1. struct MF_globals *glb , 
  ------------------
  Exit Codes	: 
  Side Effects	: 
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int MF_init( struct MF_globals *glb )
{

	LOGGER_set_output_mode( _LOGGER_STDOUT );
	glb->domain = MF_default_helo_domain;
	glb->sender = NULL;
	glb->receiver = NULL;
	glb->mailpackpath = NULL;
	glb->server = MF_default_SMTP_server;
	glb->port = MF_default_SMTP_port;
	glb->socket = -1;
	glb->verbose = 0;
	glb->show_conversation = 0;
	glb->connection_attempts = MF_default_connect_attempts;

	return 0;
}


/*------------------------------------------------------------------------
Procedure:     main ID:1
Purpose:       Accepts parameters passed from the command
line, sets up the sockets()
connection to the local SMTP
server, and pushes through the data which is
obtained
either via STDIN or a file.
Input:
Output:
Errors:
------------------------------------------------------------------------*/
int main( int argc, char **argv )
{
	int result = 0;

	struct MF_globals glb;

	MF_init( &glb );

	MF_parse_parameters( &glb, argc, argv );


	// Connect to the server.

	do {
		glb.socket = MTACON_make_call(glb.server, glb.port);
		if (glb.socket < 0)
		{
			sleep(1);
			glb.connection_attempts--;
		}
	} while ((glb.connection_attempts > 0)&&(glb.socket < 0));

	if (glb.socket < 0)
	{
		if (glb.verbose > 0) fprintf(stdout,"Unable to connect to server to make delivery\n");
		return EX_TEMPFAIL;
	}

	// Send the mailpack

	result = MTACON_send_mailpack( glb.mailpackpath, glb.sender, glb.receiver, glb.domain, glb.socket );

	// Close the connection.

	MTACON_close(glb.socket);

	if (result < 0)
		return EX_TEMPFAIL;
	else return result;
}


//--END.

