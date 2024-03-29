#include <glib.h>
#include <axsdk/axevent.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */

void subscription_callback(guint subscription, AXEvent *ax_event, guint *token);
void *send_request();
guint set_up_ax_event_subscription(AXEventHandler *event_handler, guint token);


/* Callback trigger when change in motion state */
void subscription_callback(guint subscription, AXEvent *ax_event, guint *token)
{
	const AXEventKeyValueSet *k_v_set;
	gboolean state;
	(void)subscription;

	// extract key-value set from event
	k_v_set = ax_event_get_key_value_set(ax_event);

	// state of manual trigger port
	ax_event_key_value_set_get_boolean(k_v_set, "state", NULL, &state, NULL);
    
	char *msg =  state ? "Triggered high" : "Triggered low";
	syslog(LOG_INFO,"Motion: %s \n", msg);

	// create the thread and prepare to send the composed time&date
    	pthread_t client_thread; 
    	pthread_create(&client_thread, NULL, send_request, NULL); 
}



/* Setup motion-detection-event and bind to a subscription */
guint set_up_ax_event_subscription(AXEventHandler *event_handler, guint token)
{
	AXEventKeyValueSet *k_v_set;
	guint subscription;

	k_v_set = ax_event_key_value_set_new();

	// need to set the key values before subscribing to the motion detection events 
	ax_event_key_value_set_add_key_values(k_v_set, NULL, 
		"topic0", "tns1", "RuleEngine", AX_VALUE_TYPE_STRING, 
		"topic1", "tnsaxis", "VMD3", AX_VALUE_TYPE_STRING, 
		"active", NULL, NULL, AX_VALUE_TYPE_BOOL, NULL);

	// subscribe to motion event and assign the callback to be called
	ax_event_handler_subscribe(event_handler, k_v_set, &subscription, (AXSubscriptionCallback)subscription_callback, token, NULL);

	// free resources for key value set
	ax_event_key_value_set_free(k_v_set);

	return subscription;
}



int main(void)
{
	syslog(LOG_INFO, "Starting MotionDetectorLogger...");

	AXEventHandler *event_handler;
	event_handler = ax_event_handler_new();

	guint subscription;
	guint token = 1111;
	subscription = set_up_ax_event_subscription(event_handler, &token);

	// The axevent library is designed to be used with the GLib library. 
	// An application using the axevent library must have a running GMainLoop.
	GMainLoop *loop;
	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	
 	ax_event_handler_unsubscribe(event_handler, subscription, NULL);

	ax_event_handler_free(event_handler);

	g_main_loop_unref(loop);

	return 0;
}

/*
 * Send request to web server.
 * Runs on a thread to send async and when finished, the thread closes
 */
void *send_request()
{
    int port = 8888;
    char *host = "192.168.20.231";
    
    struct hostent *server;
    struct sockaddr_in serv_addr;
    int sockfd;
    char message[128],response[2048];
    time_t now;

    time(&now);                                     // get current time
    int h,m,s,d,mon,y;                              // variables holds date and time
    struct tm *local = localtime(&now);             // struct points at current date and time

    h = local->tm_hour; 	// get hour
    m = local->tm_min;  	// get minute
    s = local->tm_sec; 		// get seconds
    d = local->tm_mday; 	// get day
    mon = local->tm_mon+1; 	// get month
    y = local->tm_year+1900; 	// get year
    
    // fill http-parameter for request 
    sprintf(message, "%d-%d-%d&%d-%d-%d", y, mon, d, h, m, s);
    syslog(LOG_INFO, "Request:%s\n", message);
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) syslog(LOG_INFO,"Error to init socket!");
   
    // check host ip name 
    server = gethostbyname(host);
    if (server == NULL) syslog(LOG_INFO, "NO SUCH HOST!");
    
    // prepare struct for network 
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    
    // connect to socket 
    if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
        syslog(LOG_INFO,"FAILED TO CONNECT!");
    
    // compose the http request and send a post request to the webserver to save the message
    char request[200];
    sprintf(request, "POST %s/set_data\r\nHTTP/1.1\r\nHOST:%s\r\n", message,"192.168.20.231");
    send(sockfd, request, sizeof(request), 0);
            
    printf("request sent!\n");
    printf("waiting for response...\n");
    recv(sockfd, &response, sizeof(request), 0);
    
    syslog(LOG_INFO, "Received following: %s", response);

    close(sockfd);
}

