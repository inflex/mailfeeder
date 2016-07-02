/* mta-connect:
 *
 * MTA-connect is a generic SMTP conversation library, primarly
 * focused at /sending/ mailpacks to existing server. Currently it
 * is used by Xamime and inflex [via Mailfeeder]
 *
 * Platform Porting Table:
 *----------------------------------------------------
 * Linux: OK
 * FreeBSD: OK ( requires sys/time.h for various calls )
 * Solaris: OK ( requires -lnsl -lsocket and netdb.h header )
 *
 * Started : 14/01/2002
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#if OSTYPE==FreeBSD
#include <sys/time.h>
#endif
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include "time.h"

#include "pldstr.h"
#include "logger.h"
#include "mta-connect.h"


#ifndef FL
#define FL __FILE__,__LINE__
#endif

#define MTACON_DVERBOSE ((MTACON_debug & MTACON_DEBUG_VERBOSE) == MTACON_DEBUG_VERBOSE)
#define MTACON_DNORMAL ((MTACON_debug & MTACON_DEBUG_NORMAL) == MTACON_DEBUG_NORMAL)
#define MTACON_DPEDANTIC ((MTACON_debug & MTACON_DEBUG_PEDANTIC) == MTACON_DEBUG_PEDANTIC)
#define DMTAC if (MTACON_debug == 1)

static int MTACON_debug = 0;
//static int MTACON_verbose = 0;
static int MTACON_conversation = 0;

// BEGIN SMP MOD
int lastline_eol = 1;
// END SMP MOD

/*------------------------------------------------------------------------
Procedure:     MTACON_set_debug ID:1
Purpose:       Turns on/off the debugging messages within MTACON.
Input:
Output:
Errors:
------------------------------------------------------------------------*/
int MTACON_set_debug( int level )
{
	MTACON_debug = level;

	return 0;
}


/*-----------------------------------------------------------------\
  Function Name	: MTACON_set_conversation
  Returns Type	: int
  ----Parameter List
  1. int level , 
  ------------------
  Exit Codes	: 
  Side Effects	: 
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int MTACON_set_conversation( int level )
{
	MTACON_conversation = level;

	return 0;
}

/*-----------------------------------------------------------------\
  Function Name	: MTACON_send_data
  Returns Type	: int
  ----Parameter List
  1. int pfsocket, 
  2.  char *data, 
  3.  size_t len , 
  ------------------
  Exit Codes	: 
  Side Effects	: 
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int MTACON_send_data( int pfsocket, char *data, int len )
{
	int total = 0;
	int bytesleft = len;
	int n = 0;

	while (total < len)
	{
		if (MTACON_conversation) LOGGER_log("   -=>>> %s",data +total);
		n = send(pfsocket, data +total, bytesleft, 0);
		if (n == -1) {
			LOGGER_log("%s:%d:MTACON_send_data:ERROR: While trying to send %d bytes in string '%s' (%s)"
					,FL
					,bytesleft
					,data +total
					,strerror(errno)
					);
			break;
		}
		total += n;
		bytesleft -= n;
	}

	if (n == -1) return -1; else return total;

}
/*------------------------------------------------------------------------
Procedure:     MTACON_get_response ID:1
Purpose:       Used to absorb/accept data being sent from the SMTP server.
Note, this data is ignored, as we are simply following SMTP
protocol and assume that if email has gotten to here that it must
have been accepted by postfix and Xamime already.
Input:
Output:
Errors:
------------------------------------------------------------------------*/
int MTACON_get_response( int pfsocket )
{
	char buf[1024];
	int result;
	struct timeval tv;
	time_t baseTime, nowTime;
	int timeout=30;
	fd_set recv_fdset;

	// Clear our set of selectors
	FD_ZERO(&recv_fdset);

	// Add in our pfsocket fd.
	FD_SET(pfsocket, &recv_fdset);

	// Set our timeout
	tv.tv_sec = 30;
	tv.tv_usec = 0;

	// Infinately loop until either we time out or we get the
	// data we were after.

	while (1)
	{
		// Set up the select to wait for activity on our socket

		result=select(pfsocket+1,&recv_fdset, NULL, NULL, &tv);

		// If we got a non-zero result, that's good!
		if (result == -1)
		{
			LOGGER_log("%s:%d:MTACON_get_response:ERROR: Select on socket returned -1 (%s)",FL,strerror(errno));
			return -1;
		} else if (result == 0) { 
			LOGGER_log("%s:%d:MTACON_get_response:WARNING: No response after %d seconds, aborting attempt", FL, timeout );
			return -1;
		} else {
			int read_size;

			// Read that data from the socket.
			read_size=read(pfsocket, buf, sizeof(buf));

			// Get time from last time we received something.

			if (read_size > 0) {
				baseTime = time(NULL);
			}
			else {
				double hangSeconds;
				nowTime = time(NULL);
				hangSeconds = difftime(nowTime, baseTime);
				if (hangSeconds >= MTACON_TIMEOUT_SECONDS) {
					if (MTACON_debug) LOGGER_log("%s:%d:MTACON_get_response:DEBUG: Timeout waiting for information, elapsed %f seconds",FL,hangSeconds);
					return -1;
				}
			}


			// Put a string termination on
			buf[read_size] = '\0';

			if (MTACON_conversation) LOGGER_log("<<<=- %s", buf);

			// Send out what we received, for debugging
			if (MTACON_debug) LOGGER_log("%s:%d:MTACON_get_response:DEBUG: Read: %s",FL,buf);

			// If the last char is a \n or \r, break, as we've gotten
			// what we came for.
			if ((buf[read_size-1] == '\n')||(buf[read_size-1] == '\r')) break;
		}
	}
	return result;
}

/*------------------------------------------------------------------------
Procedure:     MTACON_make_call ID:1
Purpose:       Makes a connection to port required for
sending the data to.
Input:
Output:
Errors:
------------------------------------------------------------------------*/
int MTACON_make_call(char *hostname, unsigned short portnum)
{
	struct sockaddr_in sa;
	struct hostent     *hp;
	int s;
	int result;

	DMTAC LOGGER_log("%s:%d:MTACON_make_call: Getting host ID", FL);
	hp = gethostbyname(hostname);
	DMTAC LOGGER_log("%s:%d:MTACON_make_call: hp = %p", FL, hp);

	if (hp == NULL)
	{
#if USE_HSTRERROR == 0
		LOGGER_log("%s:%d:MTACON_make_call: Failure getting host address from '%s'",FL,hostname);
#else
		LOGGER_log("%s:%d:MTACON_make_call: Failure getting host address from '%s' (%s)",FL,hostname,hstrerror(h_errno));
#endif
		errno= ECONNREFUSED;
		return -1;
	}

	DMTAC LOGGER_log("%s:%d:MTACON_make_call:DEBUG: Clearing the Socket-Address struct ", FL);
	// Clear the struct
	memset(&sa,0,sizeof(sa));

	// Set address.
	DMTAC LOGGER_log("%s:%d:MTACON_make_call:DEBUG: setting connect structure details", FL);
	//sa.sin_family= hp->h_addrtype;
	sa.sin_family=  AF_INET;
	sa.sin_port= htons((u_short)portnum);
	sa.sin_addr = *((struct in_addr *)hp->h_addr);
	//	memcpy((char *)&sa.sin_addr,hp->h_addr,hp->h_length);
	memset(&(sa.sin_zero),'\0',8);

	// Attempt to make the socket
	DMTAC LOGGER_log("%s:%d:MTACON_make_call:DEBUG: Making socket",FL);
	//s = socket(hp->h_addrtype,SOCK_STREAM,0);
	s = socket(AF_INET,SOCK_STREAM,0);
	if (s < 0)
	{
		LOGGER_log("%s:%d:MTACON_make_call:ERROR: Unable to get socket to communicate (%s)",FL,strerror(errno));
		return -1;
	}

	// Attempt to connect
	DMTAC LOGGER_log("%s:%d:MTACON_make_call:DEBUG: Attempting connect",FL);
	if (MTACON_conversation) LOGGER_log("Connecting...");

	result = connect(s,(struct sockaddr *)&sa,sizeof(struct sockaddr));
	if (result < 0)
	{
		close(s); 
		// We only put this log output on the 'verbose' level because we don't want connection
		//	retries spilling out needless reports to stdout or where ever - instead, we'll
		// let the return code indicate that there's a connection problem to the calling parent
		// and let them decide what reports they want to give out
		if (MTACON_DVERBOSE) LOGGER_log("%s:%d:MTACON_make_call:ERROR: Unable to connect (%s)",FL,strerror(errno));
		return -1;
	}

	// If all went well, we return the socket identifier.
	return s;
}


/*-----------------------------------------------------------------\
  Function Name	: MTACON_file_to_socket
  Returns Type	: int
  ----Parameter List
  1. int sk, 
  2.  FILE *fin, 
  3.  int must_close_fin , 
  ------------------
  Exit Codes	: 
  Side Effects	: 
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int MTACON_file_to_socket( int sk, FILE *fin, int must_close_fin )
{
	/** Copies the contents of the file *fin to the network socket netsocket **/
	char buf[1024];
	int lastline_eol=0;
	int status = 0;

	while ( fgets(buf, 1022, fin))
	{
		// Only use 1023 characters in case we need to prepend
		// a period...
		//
		// RFC 2821, section 4.5.2, requires the sending agent to 
		// examine the first character of a line, and if it is a
		// period, insert an additional period in front of it.
		// This is done even if there is additional text on the
		// line. The SMTP receiver is required to similarly delete
		// the first character of any line that starts with a period.
		//
		// the fgets call is not guaranteed to really end on a line,
		// so I use a flag... On any given read, the flag will be
		// from the last read, so we'll know if the last time through
		// had a end-of-line delimiter. If so, then we can consider
		// this a case needing to be fixed, otherwise, just continue
		// to read as usual...
		//
		if (lastline_eol) {
			if (buf[0] == '.') {
				char workbuf[1024];

				// save our current line
				snprintf(workbuf,sizeof(workbuf),"%s",buf);

				// put a period in the outbound line, 
				// then add the original source line.
				snprintf(buf,sizeof(buf),".%s",workbuf);
			}
		}

		// Now, after we've evaluated the line, set the flag as to
		// whether or not this line had a CR or LF for next pass...
		//
		//	We ASSUME here that the line will end either with a \r or \n
		//		because we used fgets() to read the file.  The only time
		//		the line won't have a \r or \n is if the line has NO
		//		\r\n's in it or our buffer was too small to hold the 
		//		entire line, because fgets() will read upto and including
		//		the \n.
		//
		//	Just REMEMBER this ASSUMPTION if you ever go change from
		//		using fgets() to something else (like read()).

		if (strchr(buf,'\r') || strchr(buf,'\n')) {
			lastline_eol = 1;
		} else {
			lastline_eol = 0;
		}


		// continue as normal...
		// END SMP MOD
		if (MTACON_send_data(sk, buf, strlen(buf)) == -1)
		{
			LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While attempting to send mailpack data '%s'",FL,buf);
			return -1;
		}
	}

	return status;
}

/*-----------------------------------------------------------------\
  Function Name	: MTACON_data_to_socket
  Returns Type	: int
  ----Parameter List
  1. int sk, 
  2.  FILE *fin, 
  3.  int must_close_fin , 
  ------------------
  Exit Codes	: 
  Side Effects	: 
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int MTACON_data_to_socket( int sk, char *data )
{

	int status = 0;
	char buf[1024];
	int bufrem=1024;
	char lastchar='\0';
	char *pin,*pout;

	/** Initialize our variables **/
	pin = data;
	pout = buf;
	bufrem = sizeof(buf) -2;

	/** Perform a while loop, building our output string
	 ** and making any required modifications, like 
	 ** double-leading-dots in the SMTP data.  Do this 
	 ** until we run out of data or have an error in the 
	 ** input 
	 **/
	while (1)
	{
		/** See if we need to send our data **/
		if ((bufrem < 1)||(*pin == '\0')) {
			*pout = '\0';
			if (MTACON_send_data(sk, buf, strlen(buf)) == -1)
			{
				LOGGER_log("%s:%d:MTACON_send_data:ERROR: While attempting to send mailpack data '%s'",FL,buf);
				return -1;
			} else {
				bufrem = sizeof buf;
				pout = buf;
			}

			if (*pin == '\0') break;
		}

		/** Handle special character cases **/
		switch (*pin) {
			case '.':
				/** If the current character is a period/., and the 
				 ** previous character was a line break, then add
				 ** another period to the output string.  This is
				 ** required for SMTP DATA transfer **/
				if ((lastchar == '\n')||(lastchar == '\r')) {
					*pout = '.';
					pout++;
					bufrem--;
				}
				break;
		}

		/** Increment to the next character in the data stream **/
		lastchar = *pin;
		*pout = *pin;
		pout++;
		pin++;
		bufrem--;

	}

	return status;
}

/*------------------------------------------------------------------------
Procedure:     MTACON_send_mailpack ID:1
Purpose:       Sends a given text file/mailpack through the
supplied socket file-descriptor.
Input:
Output:
Errors:
------------------------------------------------------------------------*/
int MTACON_send_mailpack_generic( char *mailpackname, char *rfc822buffer, char *sender, char *receiver, char *domain, int sk )
{
	struct PLD_strtok st;
	char buf[2048];
	int mta_result;
	int must_close_fin=0;
	int send_result = 0;
	FILE *fin;

	// BEGIN SMP MOD
	char *p;
	char delims[]=", ";
	// END SMP MOD

	if ((mailpackname != NULL)&&(rfc822buffer != NULL)) {
		/** Error, we cannot have BOTH items non-NULL  **/
		LOGGER_log("%s:%d:MTACON_send_mailpack_generic:ERROR: Filename and buffer are non-NULL, we can only one OR the other",FL);
		return -1;
	}

	if (sk < 0) {
		/** Socket is not valid **/
		LOGGER_log("%s:%d:MTACON_send_mailpack_generic:ERROR: Socket supplied is invalid (%s)",FL,sk);
		return -1;
	}

	// Open the mailpack we're trying to send to.

	if (mailpackname != NULL) {
		/** If we have a mailpack/file to open and send, then open it here **/
		if ((*mailpackname == '-')&&(*(mailpackname +1) == '\0'))
		{
			/** If the mailpack name is just '-', it means read from STDIN **/
			fin = stdin;
		} else {
			/** If the mailpack name isn't '-', open it as a normal file as read only **/
			fin = fopen(mailpackname,"r");
			if (!fin)
			{
				/** If we couldn't open the file, report in why **/
				LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: while opening mailpack '%s' for reading. (%s)",FL, mailpackname, strerror(errno));
				return -1;
			}

			/** As we don't normally 'close' stdin, we just flag here if we have to explicitly close a file **/
			must_close_fin = 1;
		}
	}

	// Send the preliminary headers to get things going

	// Get the SMTP introduction message
	mta_result = MTACON_get_response(sk);
	if (mta_result == -1)
	{
		if (must_close_fin == 1) fclose(fin);
		LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While waiting on SMTP intro",FL);
		return -1;
	}

	// Introduce ourselves
	snprintf(buf,sizeof(buf),"HELO %s\r\n", domain );
	if (MTACON_send_data(sk, buf, strlen(buf)) == -1)
	{
		if (must_close_fin == 1) fclose(fin);
		LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While attempting to send HELO string '%s'",FL,buf);
		return -1;
	}
	write(sk, buf, strlen(buf));
	mta_result = MTACON_get_response(sk);
	if (mta_result == -1)
	{
		if (must_close_fin == 1) fclose(fin);
		LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While on HELO response",FL);
		return -1;
	}

	// Say who we're sending email from...
	if (strchr(sender,'<') != NULL) {
		snprintf(buf,sizeof(buf),"mail from: %s\r\n", sender);
	} else {
		snprintf(buf,sizeof(buf),"mail from: <%s>\r\n", sender);
	}
	//snprintf(buf,sizeof(buf),"mail from: %s\r\n", sender);
	if (MTACON_send_data(sk, buf, strlen(buf)) == -1)
	{
		if (must_close_fin == 1) fclose(fin);
		LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While attempting to send MAIL FROM string '%s'",FL,buf);
		return -1;
	}
	mta_result = MTACON_get_response(sk);
	if (mta_result == -1)
	{
		if (must_close_fin == 1) fclose(fin);
		LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While waiting on MAIL FROM response",FL);
		return -1;
	}

	// Say who we're sending it to...
	//
	// BEGIN SMP MOD
	// Modified for multiple recipients
	// 
	if (MTACON_debug) LOGGER_log("%s:%d:MTACON_send_mailpack:DEBG: receivers = '%s'",FL,receiver);
	p = PLD_strtok( &st, receiver, delims );
	while( p != NULL ) 
	{
		if (strchr(p,'<') != NULL) {
			snprintf(buf,sizeof(buf),"rcpt to: %s\r\n", p);
		} else {
			snprintf(buf,sizeof(buf),"rcpt to: <%s>\r\n", p);
		}

		if (MTACON_debug) LOGGER_log("%s:%d:MTACON_send_mailpack:DEBG: receiver = '%s'",FL,p);
		if (MTACON_send_data(sk, buf, strlen(buf)) == -1)
		{
			if (must_close_fin == 1) fclose(fin);
			LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While attempting to send RCPT TO string '%s'",FL,buf);
			return -1;
		}

		mta_result = MTACON_get_response(sk);
		if (mta_result == -1)
		{
			if (must_close_fin == 1) fclose(fin);
			LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While waiting on RCPT reponse",FL);
			return -1;
		}
		p = PLD_strtok( &st, NULL, delims );
		if (MTACON_debug) LOGGER_log("%s:%d:MTACON_send_mailpack:DEBUG: next receiver = '%s'",FL,p);
	}
	// END SMP MOD

	// Tell SMTP server that we're about to send the data
	snprintf(buf,sizeof(buf),"data\r\n");
	if (MTACON_send_data(sk, buf, strlen(buf)) == -1)
	{
		if (must_close_fin == 1) fclose(fin);
		LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While attempting to send DATA string '%s'",FL,buf);
		return -1;
	}
	mta_result = MTACON_get_response(sk);
	if (mta_result == -1)
	{
		if (must_close_fin == 1) fclose(fin);
		LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While waiting on DATA response",FL);
		return -1;
	}

	// Send the data...
	//	while ((result = fread(buf, 1, 1024, fin)))
	//	BEGIN SMP MOD
	if (mailpackname != NULL) {
		int r = 0;

		r = MTACON_file_to_socket(sk, fin, must_close_fin);
		if ((r == -1)&&(must_close_fin == 1)) { fclose(fin); return -1; }

	} else {
		MTACON_data_to_socket(sk, rfc822buffer);
	}

	// Send the trailing dot and the exit "quit"
	snprintf(buf,sizeof(buf),"\r\n.\nquit\r\n");
	if (MTACON_send_data(sk, buf, strlen(buf)) == -1)
	{
		if (must_close_fin == 1) fclose(fin);
		LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While attempting to send QUIT '%s'",FL,buf);
		return -1;
	}

	mta_result = MTACON_get_response(sk);
	if (mta_result == -1)
	{
		if (must_close_fin == 1) fclose(fin);
		LOGGER_log("%s:%d:MTACON_send_mailpack:ERROR: While waiting on QUIT response",FL);
		return -1;
	}

	/*
	// Close the mailpack file if required.
	// OMG, did no one notice this!? It's trying to close STDIN, I'm such an idiot!
	//	now fixed (I hope).
	//if (*mailpackname == '-') Damn that was a bad bug !
	if (*mailpackname != '-')
	{
	fclose(fin);
	}
	 */

	if (must_close_fin == 1) fclose(fin);

	//	2004-05-22:PLD
	// This was returning 'result' which was meaning it would pick up random
	//		values from the above processes which used result as a buffer length
	//		holder *sigh*

	return send_result;
}


/*-----------------------------------------------------------------\
  Date Code:	: 20111207-160749
  Function Name	: MTACON_send_buffer
  Returns Type	: int
  	----Parameter List
	1. char *buffer, 
	2.  char *sender, 
	3.  char *receiver, 
	4.  char *domain, 
	5.  int sk , 
	------------------
Exit Codes	: 
Side Effects	: 
--------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int MTACON_send_buffer( char *buffer, char *sender, char *receiver, char *domain, int sk )
{
	return MTACON_send_mailpack_generic( NULL, buffer, sender, receiver, domain, sk );
}

/*-----------------------------------------------------------------\
  Date Code:	: 20111207-160748
  Function Name	: MTACON_send_mailpack
  Returns Type	: int
  	----Parameter List
	1. char *mailpackname, 
	2.  char *sender, 
	3.  char *receiver, 
	4.  char *domain, 
	5.  int sk , 
	------------------
Exit Codes	: 
Side Effects	: 
--------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int MTACON_send_mailpack( char *mailpackname, char *sender, char *receiver, char *domain, int sk )
{
	return MTACON_send_mailpack_generic( mailpackname, NULL, sender, receiver, domain, sk );
}

/*------------------------------------------------------------------------
Procedure:     MTACON_close ID:1
Purpose:       Close the socket handle handed in the
params. NOTE - this currently is a fairly
trivial call (redundant), but we use it anyhow
in case we do have to do some more complex
things later.
Input:
Output:
Errors:
------------------------------------------------------------------------*/
int MTACON_close( int pfsocket )
{
	if (MTACON_conversation) LOGGER_log("Closing connection.");
	close(pfsocket);

	return 0;
}



//--END.

