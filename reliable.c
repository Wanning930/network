
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "rlib.h"

typedef enum{false, true} bool;
typedef struct reliable_client client_t;
typedef struct reliable_server server_t;
typedef uint32_t seqno_t;
typedef struct timespec timespec_t;
typedef struct packet_node pnode_t;
typedef struct server_buffer buffer_t;
// typedef struct reliable_state rel_t;
typedef struct ack_packet ack_t;

#define ACK_LEN 8
#define CKSUM_LEN (sizeof(uint16_t))

void buffer_enque(buffer_t *buffer, packet_t *packet);
packet_t *buffer_deque(buffer_t *buffer);
bool buffer_isEmpty(buffer_t *buffer);
bool packet_isAck(size_t n);


struct reliable_client { /* receive data packet and send ack */
	seqno_t RWS;
	seqno_t last_recv;
	seqno_t last_legal;
	seqno_t expect;
	packet_t **window; /* ordered and overwirted, size = RWS */
};

struct packet_node {
	packet_t *content;
	pnode_t *next;
	// pnode_t *prev;
};


struct server_buffer {
	pnode_t *head; /* head is a dummy head */
	pnode_t *tail;
};

struct reliable_server { /* send data packet and wait for ack */
	seqno_t SWS;
	seqno_t last_acked;
	seqno_t last_sent;
	packet_t **packet_window; /* last_acked < x <= last_sent might retransmit latter, size = SWS*/
	timespec_t **time_window;
	buffer_t *buffer; /* from conn_read but not in sliding window, no seq assigned, arbitrary size */
};


struct reliable_state {
	rel_t *next;          /* Linked list for traversing all connections */
	rel_t **prev;

	conn_t *c;            /* This is the connection object */

	/* Add your own data fields below this */
	// seqno_t max_seqno;		/* comes from config: window size */
	client_t *client;
	server_t *server;

};
rel_t *rel_list;

void buffer_enque(buffer_t *buffer, packet_t *packet) {
	pnode_t *node = malloc(sizeof(pnode_t));
	node->content = packet;
	node->next = NULL;
	buffer->tail->next = node;
	buffer->tail = buffer->tail->next;
}

packet_t *buffer_deque(buffer_t *buffer) {
	assert(!buffer_isEmpty(buffer));
	pnode_t *pt = buffer->head;
	packet_t *pc = pt->next->content;
	buffer->head = buffer->head->next;
	free(pt);
	return pc;
}

bool buffer_isEmpty(buffer_t *buffer) {
	return (buffer->head == buffer->tail);
}

bool packet_isAck(size_t n) {
	return (n == sizeof(ack_t));
}



/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
		const struct config_common *cc)
{
	rel_t *r;

	r = xmalloc (sizeof (*r));
	memset (r, 0, sizeof (*r));

	if (!c) {
		c = conn_create (r, ss);
		if (!c) {
			free (r);
			return NULL;
		}
	}

	r->c = c;
	r->next = rel_list;
	r->prev = &rel_list;
	if (rel_list) {
		rel_list->prev = &r->next;
	}
	rel_list = r;
	/* Do any other initialization you need here */
	// r->max_seqno = 2 * cc->window;
	r->client = malloc(sizeof(client_t));
	memset (r->client, 0, sizeof (client_t));
	r->client->RWS = cc->window;
	r->client->last_recv = -1;
	r->client->last_legal = r->client->RWS - 1;
	r->client->expect = 0;
	r->client->window = malloc((r->client->RWS) * sizeof(packet_t *));
	memset (r->client->window, 0, (r->client->RWS) * sizeof(packet_t *));
	r->server = malloc(sizeof(server_t));
	memset (r->server, 0, sizeof (server_t));
	r->server->SWS = cc->window;
	r->server->last_acked = -1;
	r->server->last_sent = -1;
	r->server->packet_window = malloc((r->server->SWS) * sizeof(packet_t *));
	memset (r->server->packet_window, 0, (r->server->SWS) * sizeof(packet_t *));
	r->server->time_window = malloc((r->server->SWS) * sizeof(timespec_t *));
	memset (r->server->time_window, 0, (r->server->SWS) * sizeof(timespec_t *));
	/* might change this to pure string buffer */
	r->server->buffer = malloc(sizeof(buffer_t));
	r->server->buffer->head = malloc(sizeof(pnode_t *));
	memset (r->server->buffer->head, 0, sizeof(pnode_t *));
	r->server->buffer->tail = r->server->buffer->head;
	return r;
}

void
rel_destroy (rel_t *r)
{
	if (r->next) {
		r->next->prev = r->prev;
	}
	*r->prev = r->next;
	conn_destroy (r->c);
	/* Free any other allocated memory here */
	free(r->client->window);
	free(r->client);
	free(r->server->packet_window);
	free(r->server->time_window);
	assert(buffer_isEmpty(r->server->buffer));
	free(r->server->buffer->head);
	free(r->server->buffer);
	free(r->server);
}


/* This function only gets called when the process is running as a
 * server and must handle connections from multiple clients.  You have
 * to look up the rel_t structure based on the address in the
 * sockaddr_storage passed in.  If this is a new connection (sequence
 * number 1), you will need to allocate a new conn_t using rel_create
 * ().  (Pass rel_create NULL for the conn_t, so it will know to
 * allocate a new connection.)
 */
void
rel_demux (const struct config_common *cc,
	   const struct sockaddr_storage *ss,
	   packet_t *pkt, size_t len)
{
}

void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
	seqno_t SWS = r->server->SWS;
	seqno_t RWS = r->client->RWS;
	seqno_t no = pkt->seqno;
	if (packet_isAck(n)) { /* server */
		if (pkt->cksum != cksum((void *)pkt + CKSUM_LEN, n - CKSUM_LEN)) {
			/* discard this packet */
		}
		else {
			while (pkt->ackno - r->server->last_acked > 1) {
				// r->server->last_acked++;
				r->server->time_window[(r->server->last_acked - 1) % SWS] = NULL;
				r->server->packet_window[(r->server->last_acked - 1) % SWS] = NULL;
				r->server->last_acked++;
			}
		}
	}
	else {
		if (pkt->cksum != cksum((void *)pkt + CKSUM_LEN, n - CKSUM_LEN)) { 
			/* discard this packet */
		}
		else { /* client */
			if ( (no > r->client->last_recv) && (no <= r->client->last_legal) ) {
				/* in the window */
				r->client->window[(no - 1) % RWS] = pkt;
				if (r->client->expect == no) {
					while (r->client->window[(r->client->expect - 1) % RWS] != NULL) {
						r->client->window[(r->client->expect - 1) % RWS] = NULL;
						r->client->expect++;
					}
					r->client->last_recv = r->client->expect - 1;
					/* send acknowledgment back to server */
					ack_t ack;
					ack.len = ACK_LEN;
					ack.ackno = r->client->expect;
					ack.cksum = cksum ((const void *)(&ack) + CKSUM_LEN, ACK_LEN - CKSUM_LEN); 
					conn_sendpkt (r->c, (const packet_t *)&ack, ACK_LEN);
				}
			}
			else {
				/* discard this packet */
			}
		}
		ack_t ack;
		ack.len = ACK_LEN;
		ack.ackno = r->client->expect;
		conn_sendpkt (r->c, (const packet_t *)&ack, ACK_LEN);
	}	
	
}


void
rel_read (rel_t *s)
{
}

void
rel_output (rel_t *r)
{
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */

}
