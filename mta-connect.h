#ifndef EX_TEMPFAIL
#define EX_TEMPFAIL 75
#endif

#define MTACON_DEBUG_VERBOSE 1
#define MTACON_DEBUG_NORMAL 2
#define MTACON_DEBUG_PEDANTIC 4
#define MTACON_TIMEOUT_SECONDS 60

int MTACON_make_call(char *hostname, unsigned short portnum);
int MTACON_set_debug( int level );
int MTACON_set_verbose( int level );
int MTACON_set_conversation( int level );
int MTACON_send_buffer( char *buffer, char *sender, char *receiver, char *domain, int sk );
int MTACON_send_mailpack( char *mailpackname, char *sender, char *receiver, char *domain, int sk );
int MTACON_close( int pfsocket );

