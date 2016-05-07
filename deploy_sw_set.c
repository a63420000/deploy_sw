#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<ctype.h>

#include<sys/socket.h>
#include<sys/time.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#include<errno.h>
#include<netdb.h>

#define SWNUM 20
#define CTRLNUM 3
#define QCLI 20
#define MAXLINE 10000

char sw_ID[SWNUM][50], ctrl[CTRLNUM][50];
int tcp(char *port);
char *rcv_cli_msg( int cli_sockfd, char *line );
int readline( int fd, char *ptr, int maxlen );
char *rm_ln_from_line( char *line );
void exec_tcpdump(int sw_ctrl_port[][SWNUM]);
int reply_sw_pkt_num( int fd, int sw_ctrl_port[][SWNUM] );
void print_s_c_set(int sw_ctrl_port[][SWNUM]);
void print_sport_to_c(int sw_ctrl_port[][SWNUM]);
void implement_sw(int i, char *sw_ID_clone, char *ctrl_clone,  char *l2_ctrl_clone, int sw_ctrl_port[][SWNUM], int layer );
int main(int argc, char *argv[] )
{
	char input[1000];
	char *sw_ctrl, *temp = malloc(sizeof(char)*1000), *line, *rmnline;	
	int sw_ctrl_port[4][SWNUM];

	FILE *rctrl_fp = fopen("ctrl_set.txt", "r"), *rsw_fp = fopen("sw_set.txt", "r");
	// *rsetfp = fopen("set.txt", "r"),
	int i=0, j=0;

	//** Import ctrl info	
	i=0;
	while( EOF !=  fscanf(rctrl_fp, "%s", input ) )
	{
		strcpy( ctrl[i], input );	printf("%s ", ctrl[i]);
		bzero(input,999);
		i++;
	}					printf("\n");
	//** END **

	//** Import sw info	
	i=0;
	while( EOF !=  fscanf(rsw_fp, "%s", input ) )
	{
		strcpy( sw_ID[i], input );	printf("%s ", sw_ID[i]);
		bzero(input,999);
		i++;
	}					printf("\n");
	//** END **

	struct sockaddr_in fsin;
	int msock;
	fd_set wfds, rfds, afds;
	int alen;
	int fd, nfds, rc, a, layer;
	/* flag */
	int hello, sw_set_fin, can_disconnect;

	msock = tcp(argv[1]);
	nfds = getdtablesize();
	FD_ZERO( &afds );
	FD_SET( msock, &afds );
	while(1)
	{
		memcpy(&rfds, &afds, sizeof(rfds));
		memcpy(&wfds, &afds, sizeof(wfds));
		if(select(nfds, &rfds, &wfds, (fd_set *)0, (struct timeval*)0) < 0)
		{
			fprintf(stderr, "select: %s.\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		if(FD_ISSET(msock, &rfds))
		{
			int ssock;			
			alen = sizeof(fsin);
			ssock = accept(msock, (struct sockaddr *)&fsin, &alen);
			if(ssock < 0)
			{
				fprintf(stderr,"accept: %s.\n", strerror(errno));
				exit(EXIT_FAILURE);
			}	
			fprintf(stdout, "\nGA gonna send set, fd=%d.\n",ssock);
			FD_SET(ssock, &afds);	
			hello = 0;
			layer = 0;
			sw_set_fin = 0;
			can_disconnect = 0;
			continue;
		}
		
		for(fd = 0; fd < nfds; ++fd)
		{
			//** Read msgs from fd
			if(fd != msock && FD_ISSET(fd, &rfds)) 
			{		
				line = rcv_cli_msg( fd, temp );
				printf("%s", line);
				rmnline = rm_ln_from_line( line );

				if( strstr( line, "Hello server!") != NULL )
				{	hello = 1; continue;	}
				if( hello == 1 && layer < 4)
				{	
					printf("layer=%d, %s\n", layer, rmnline);
					i=0;
					sw_ctrl = strtok( rmnline, "," );
					while( sw_ctrl != NULL )
					{
						sw_ctrl_port[layer][i] = atoi( sw_ctrl );
						printf("(%s,%d) ", sw_ctrl, sw_ctrl_port[layer][i] );
						i++;
						sw_ctrl = strtok( NULL, "," );
					}
					bzero(line,999);
					layer+=2;
					
					if( layer == 4 )
					{
						
						hello = 0;
						//** Implement switch set **
						char sw_ID_clone[100], l1_ctrl_clone[100], l2_ctrl_clone[100];
						for( i = 0 ; i < SWNUM ; i++ )
						{	
							//** Set switch's 1st controller **
								// sw_ctrl mapping is 1,2,3 but array index is 0,1,2
							strcpy( l1_ctrl_clone , ctrl[ sw_ctrl_port[0][i] - 1 ] ); 
							strcpy( sw_ID_clone, sw_ID[i] );
							implement_sw( i, sw_ID_clone, l1_ctrl_clone, "\0"  ,sw_ctrl_port, 1  );
							//** End of set sw[i]'s 1st controller **
						}
						for( i = 0 ; i < SWNUM ; i++ )
						{
							//** Set switch's 2nd controller ** 
								// sw_ctrl mapping is 1,2,3 but array index is 0,1,2
							strcpy( l2_ctrl_clone , ctrl[ sw_ctrl_port[2][i] - 1 ] ); 
							strcpy( l1_ctrl_clone , ctrl[ sw_ctrl_port[0][i] - 1 ] ); 
							strcpy( sw_ID_clone, sw_ID[i] );
							implement_sw( i, sw_ID_clone, l1_ctrl_clone, l2_ctrl_clone, sw_ctrl_port, 3 );
							//** End of set sw[i]'s 2nd controller **		
						}
						//** END of set sws' controller **
	
						print_sport_to_c(sw_ctrl_port);

						//** Monitor sw port  
						exec_tcpdump(sw_ctrl_port);
						//** END of monitor sw port
					
						//** Set flag to make server write pkt info back
						sw_set_fin = 1;
					}
					else
						continue;
				}
				

				if(strstr( line, "disconnect") != NULL)
				{
					printf("%s\n", rmnline);
					//** Disconnect client
					(void) close(fd);
					FD_CLR(fd, &afds);
					bzero(line, 999);
					//** END of disconnect client
					
				}
				
					
			}
			//** End of read msg from fd

			//** Write msg to fd
			if(fd != msock && FD_ISSET(fd, &wfds))
			{
				if( sw_set_fin == 1 )
				{
					can_disconnect = reply_sw_pkt_num( fd, sw_ctrl_port );
				}
				if( can_disconnect == 2 )
				{
					char *replyMsg = "Disconnect\n";
					write(fd, replyMsg , strlen(replyMsg));		
				}
			}
			//** END of Write msg to fd
		}
	}
	
	return 0;
}

void exec_tcpdump( int sw_ctrl_port[][SWNUM] )
{
	int i;
	char argv[SWNUM][100];
	
	sprintf(argv[0], "sudo");
	sprintf(argv[1], "./tcpdump.sh");
	for( i = 0 ; i < SWNUM+2 ; i++ )
	{
		 sprintf(argv[i+2], "%d", sw_ctrl_port[1][i]); 
	}

	/** 
	i=0;
	while( i < SWNUM+2 ) // argv[0] is file name
	{
		printf("%d %s\n", i, argv[i] );
		i++;
	}
	printf("\n");
	**/

	pid_t td_pid;
	td_pid = fork();
	if( td_pid < 0 )
	{
		fprintf(stderr, "Fork tcpdump error\n");
	}
	else if( td_pid == 0 )
	{
		fprintf(stderr, "tcpdump ports.\n");
		char * argv2[] = {argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]
				, argv[6], argv[7], argv[8], argv[9], argv[10], argv[11]
				, argv[12], argv[13], argv[14], argv[15], argv[16], argv[17]
				, argv[18], argv[19], argv[20], argv[21], NULL};
		execve("/usr/bin/sudo", argv2, NULL );
	}
	else
	{
		wait(NULL);
		fprintf(stderr, "End of tcpdump\n");
	}
 
}

void implement_sw(int i, char *sw_ID_clone, char *ctrl_clone,  char *l2_ctrl_clone, int sw_ctrl_port[][SWNUM], int layer )
{
	int j; 
	int port;
	pid_t set_c1_pid, netstat_pid;

	set_c1_pid = fork();
	if ( set_c1_pid < 0 )
	{
		fprintf(stderr,"Error in set-controller\n");
	}
	else if ( set_c1_pid == 0 ) 
	{
		fprintf(stderr,"set-controller %d\n", i);
		if( layer == 1 )
		{
			char * argv2[] = {"ovs-vsctl", "set-controller", sw_ID_clone, ctrl_clone, 0 };	//fprintf(stderr ,"%s, %s\n", ctrl_clone, sw_ID_clone ); 
			execvp(argv2[0], argv2);
		}
		else if( layer == 3 )
		{
			char l1ctrl[100], l1sw_ID[100];
			char * argv2[] = {"ovs-vsctl", "set-controller", sw_ID_clone, ctrl_clone, l2_ctrl_clone, 0 };	//fprintf(stderr ,"%s, %s\n", ctrl_clone, sw_ID_clone ); 
			execvp(argv2[0], argv2);
		}
	}      
	else
	{
		wait(NULL);
		//fprintf(stderr,"End of set-controller %d\n", i);

		netstat_pid = fork();
		if ( netstat_pid < 0 )
		{
			fprintf(stderr,"Error in netstat\n");
		}
		else if ( netstat_pid == 0 ) 
		{
			//fprintf(stderr,"Netstat\n");
			char * argv2[] = {"python", "netstat.py", 0 };
			execvp(argv2[0], argv2);
		} 
		else
		{
			wait(NULL);
			//fprintf(stderr,"Finish grep netstat\n");
			//sleep(1);
			char input[1000];
			char *DTIP = "192.168.1.30", *port_inc_str, *port_str;
			char *pa_bool ;
			int there_is_DTIP = 0;

			FILE *net_fp;
			net_fp = fopen("netstat.txt", "r");
			if( net_fp == NULL )
			{
				fprintf(stderr, "Open netstat.txt fail\n"); 
				exit(0);
			}
			while( fgets( input, sizeof(input) , net_fp ) )
			{									//printf("input = %s", input );
				
				//** Parse port from doc **
				port_inc_str = strtok( input, " " );
				while( port_inc_str != NULL )
				{
					if( strstr( port_inc_str, DTIP) != NULL )
					{			
						there_is_DTIP = 1;				//printf("%s find!\n", port_inc_str );
						break;
					}
					else
						port_inc_str = strtok( NULL, " " );
				}								// printf("%s\n", port_inc_str );
				if( there_is_DTIP == 1 )
				{
					there_is_DTIP = 0;
					port_str = strtok( port_inc_str, ":" );
					port_str = strtok( NULL, ":" );				// printf("port = %s\n", port_str );
					port = atoi( port_str );
					//** END of parsing **
				
					//** Compare result with table to see if recorded or not. If not, record. **
					int has_same = 0;
					for( j = 0 ; j < i ; j++ )
					{
						if( port == sw_ctrl_port[layer][j] )
						{
							has_same = 1;
							break;
						}
					}
					if( layer == 3 )
					{
						// additonal check layer 1
						for( j = 0 ; j < SWNUM ; j++ )
						{
							if( port == sw_ctrl_port[layer-2][j] )
							{
								has_same = 1;
								break;
							}
						}
					}
					if( has_same == 0 )
					{
						sw_ctrl_port[layer][i] = port;
						break;
					}
				}
				
			}
			fclose(net_fp);
			//** End of record sw's 1st connection port **
		}
	}
}

int reply_sw_pkt_num( int fd, int sw_ctrl_port[][SWNUM] )
{
	int data_replied = 2;
	printf("BlaBlaBla\n");
	return data_replied;
}

int tcp(char *port)
{
	int sock, type = SOCK_STREAM;
	u_short portbase = 0;
	struct servent *pse;
	struct protoent *ppe;
	struct sockaddr_in sin;
	char *protocol = "tcp\0";

	bzero((char *)&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
       
	if( pse = getservbyname( port, protocol ) )
		sin.sin_port = htons( ntohs( (u_short)pse->s_port ) + portbase );
	else if( (sin.sin_port = htons( (u_short)atoi(port) ) ) == 0 )
	{
		fprintf( stderr, "can't get %s service.\n", port );
		exit(EXIT_FAILURE);
	}

	if( ( ppe = getprotobyname(protocol) ) == 0 )
	{
		fprintf(stderr, "can't get %s protocol.\n", protocol);
		exit(EXIT_FAILURE);
	}

	sock = socket( PF_INET, type, ppe->p_proto );
	if( sock < 0 )
	{
		fprintf(stderr, "can't create socket:%s.\n", strerror(errno));
		exit(EXIT_FAILURE);
	} 
	
	if( bind( sock, (struct sockaddr *)&sin, sizeof(sin) ) < 0 )
	{
		fprintf(stderr, "can't bind to %s port:_%s.\n", port, strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	if( listen(sock, QCLI) < 0 )
	{
		fprintf(stderr, "can't listen on %s port:%s.\n", port, strerror(errno));
		exit(EXIT_FAILURE);
	}
        
	return sock;
}

char *rcv_cli_msg( int cli_sockfd, char *line )
{
	int n;
	n = readline( cli_sockfd, line, MAXLINE );
	if( n == 0 )
	{
		return '\0';
	}
	else if( n < 0 )
	{
		fprintf( stderr, "controller %d message error\n", cli_sockfd );
		exit(0);
	} 
	
	return line;
}

int readline( int fd, char *ptr, int maxlen )
{
	int n, rc;
	char c;
	for( n = 1; n < maxlen; n++ )
	{
		if( rc = read(fd, &c, 1) == 1 )
		{
			*ptr++ = c;
			
			if( c == '\n' )
				break;
		}
		else if( rc == 0 )
		{
			if( n == 1 )
				return 0; // EOF, no data read
			else
				break;	// EOF, some data was read
		}
		else
		{
			return -1;
		}
	}
	*ptr = 0;
	return n;
}

char *rm_ln_from_line( char *line )
{
	int i;
	for( i = 0 ; i < strlen(line) ; i++ )
	{
		if( line[i] == '\n' )
		{
			line[i] = '\0';
			break;
		}
	}
	return line;
}

void print_s_c_set(int sw_ctrl_port[][SWNUM])
{
	int i;
	printf("\n** Debuging **\n");
	//** print set **
	for( i = 0 ; i < SWNUM ; i++ )
		printf("%d ", sw_ctrl_port[0][i] );	
	printf("\n");
	for( i = 0 ; i < SWNUM ; i++ )
		printf("%d ", sw_ctrl_port[2][i] );	
	printf("\n");
	printf("** End of Debug **\n");
	//** END **
}

void print_sport_to_c(int sw_ctrl_port[][SWNUM])
{
	int i;
	for( i = 0 ; i < SWNUM ; i++ )
		printf("%d\t", sw_ctrl_port[0][i] );
	printf("\n");
	for( i = 0 ; i < SWNUM ; i++ )
		printf("%d\t", sw_ctrl_port[1][i] );
	printf("\n");
	for( i = 0 ; i < SWNUM ; i++ )
		printf("%d\t", sw_ctrl_port[2][i] );
	printf("\n");
	for( i = 0 ; i < SWNUM ; i++ )
		printf("%d\t", sw_ctrl_port[3][i] );
	printf("\n");
}

//** Import sw-ctrl sets  **
/*while( EOF !=  fscanf(rsetfp, "%s", input ) )
{
	sw_ctrl = strtok( input, "," );
	while( sw_ctrl != NULL )
	{
		sw_ctrl_port[line][i] = atoi( sw_ctrl );
		i++;
		sw_ctrl = strtok( NULL, "," );
	}
	bzero(input,999);
	i=0;
	line+=2;
}*/
//** END **





