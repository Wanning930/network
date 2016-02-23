
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
#include <stdint.h>

#include "rlib.h"

typedef enum{false, true} bool;
typedef struct reliable_client client_t;
typedef struct reliable_server server_t;
typedef uint32_t seqno_t;
typedef struct timespec timespec_t;
typedef struct packet_node pnode_t;
typedef struct reliable_buffer buffer_t;
// typedef struct reliable_state rel_t;
typedef struct ack_packet ack_t;

#define ACK_LEN 8
// #define PKT_LEN (sizeof(packet_t))
#define CKSUM_LEN (sizeof(uint16_t))

void buffer_enque_c(buffer_t *buffer, char *data, uint16_t len);
void buffer_enque_p(buffer_t *buffer, packet_t *packet);
packet_t *buffer_deque(buffer_t *buffer);
bool buffer_isEmpty(buffer_t *buffer);
bool packet_isAck(size_t n);
void rel_send(rel_t *r);
bool packet_isEof(size_t n);

struct reliable_client { /* receive data packet and send ack */
	seqno_t RWS;
	seqno_t last_recv;
	seqno_t last_legal;
	seqno_t expect;
	packet_t **window; /* ordered and overwirted, size = RWS */
	buffer_t *buffer;
	bool eof;
};

struct packet_node {
	uint16_t len;
	char *content;
	pnode_t *next;
};


struct reliable_buffer {
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
	bool eof;
};


struct reliable_state {
	rel_t *next;          /* Linked list for traversing all connections */
	rel_t **prev;

	conn_t *c;            /* This is the connection object */

	/* Add your own data fields below this */
	client_t *client;
	server_t *server;
	int timeout;
};
rel_t *rel_list;

void buffer_enque_c(buffer_t *buffer, char *data, uint16_t len) {
	assert(len <= 500);
	pnode_t *node = malloc(sizeof(pnode_t));
	memset(node, 0, sizeof(pnode_t));
	if (len == 0) {
		node->content = NULL;
	}
	else {
		node->content = malloc(sizeof(char) * len);
		memcpy(node->content, data, sizeof(char) * len);
	}
	node->len = len;
	node->next = NULL;
	buffer->tail->next = node;
	buffer->tail = buffer->tail->next;
}

void buffer_enque_p(buffer_t *buffer, packet_t *packet) {
	pnode_t *node = malloc(sizeof(pnode_t));
	node->content = malloc(packet->len - 12);
	memcpy(node->content, packet->data, packet->len - 12);
	node->len = packet->len - 12; /* payload */
	node->next = NULL;
	buffer->tail->next = node;
	buffer->tail = buffer->tail->next;

}

packet_t *buffer_deque(buffer_t *buffer) {
	assert(!buffer_isEmpty(buffer));
	pnode_t *pt = buffer->head->next;
	packet_t *newpt = malloc(pt->len + 12);
	memset(newpt, 0, pt->len + 12);
	memcpy(newpt->data, pt->content, (size_t)(pt->len));
	newpt->len = pt->len + 12; /* payload + 12 */
	free(buffer->head->content);
	free(buffer->head);
	buffer->head = pt;
	return newpt;
}


bool buffer_isEmpty(buffer_t *buffer) {
	return (buffer->head == buffer->tail);
}

bool packet_isAck(size_t n) {
	return (n == sizeof(ack_t));
}

bool packet_isEof(size_t n) {
	return n == 12;
}



/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t * rel_create (conn_t *c, const struct sockaddr_storage *ss, const struct config_common *cc)
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

	/* Do client initialization */
	r->client = malloc(sizeof(client_t));
	memset (r->client, 0, sizeof (client_t));
	r->client->RWS = cc->window;
	r->client->last_recv = 0;
	r->client->last_legal = r->client->RWS;
	r->client->expect = 1;
	r->client->window = malloc((r->client->RWS) * sizeof(packet_t *));
	memset (r->client->window, 0, (r->client->RWS) * sizeof(packet_t *));
	r->client->buffer = malloc(sizeof(buffer_t));
	r->client->buffer->head = malloc(sizeof(pnode_t));
	memset(r->client->buffer->head, 0, sizeof(pnode_t));
	r->client->buffer->tail = r->client->buffer->head;
	r->client->eof = false;

	/* Do server initialization */
	r->timeout = cc->timeout;
	r->server = malloc(sizeof(server_t));
	memset (r->server, 0, sizeof (server_t));
	r->server->SWS = cc->window;
	r->server->last_acked = 0;
	r->server->last_sent = 0;
	r->server->packet_window = malloc((r->server->SWS) * sizeof(packet_t *));
	memset (r->server->packet_window, 0, (r->server->SWS) * sizeof(packet_t *));
	r->server->time_window = malloc((r->server->SWS) * sizeof(timespec_t *));
	memset (r->server->time_window, 0, (r->server->SWS) * sizeof(timespec_t *));
	r->server->buffer = malloc(sizeof(buffer_t));
	r->server->buffer->head = malloc(sizeof(pnode_t));
	memset (r->server->buffer->head, 0, sizeof(pnode_t));
	r->server->buffer->tail = r->server->buffer->head;
	r->server->eof = false;
	return r;
}

void rel_destroy (rel_t *r)
{
	if (r->next) {
		r->next->prev = r->prev;
	}
	*r->prev = r->next;
	conn_destroy (r->c);
	/* Free any other allocated memory here */
	free(r->client->window);
	assert(buffer_isEmpty(r->client->buffer));
	free(r->client->buffer->head);
	free(r->client->buffer);
	free(r->client);
	free(r->server->packet_window);
	free(r->server->time_window);
	assert(buffer_isEmpty(r->server->buffer));
	free(r->server->buffer->head);
	free(r->server->buffer);
	free(r->server);
}

void rel_demux (const struct config_common *cc,
	   const struct sockaddr_storage *ss,
	   packet_t *pkt, size_t len)
{
}

void rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
	seqno_t SWS = r->server->SWS;
	seqno_t RWS = r->client->RWS;
	if (pkt->cksum != cksum((void *)pkt + CKSUM_LEN, n - CKSUM_LEN)) {
		/* discard this packet */
		return;
	}
	pkt->seqno = ntohl(pkt->seqno);
	pkt->ackno = ntohl(pkt->ackno);
	seqno_t no = pkt->seqno;
	pkt->len = ntohs(pkt->len);
	if (pkt->len != n) {
		/* discard this packet */
		return;
	}

	if (packet_isAck(n)) { /* server */
		while (pkt->ackno - r->server->last_acked > 1) {
			r->server->last_acked++;
			if (packet_isEof(r->server->packet_window[(r->server->last_acked - 1) % SWS]->len)) {
				assert(r->server->eof == true);
			}
			free(r->server->time_window[(r->server->last_acked - 1) % SWS]);
			r->server->time_window[(r->server->last_acked - 1) % SWS] = NULL;
			free(r->server->packet_window[(r->server->last_acked - 1) % SWS]);
			r->server->packet_window[(r->server->last_acked - 1) % SWS] = NULL;
		}
		rel_send(r);
	}
	else { /* client */
		if (packet_isEof(n)) {
			r->client->eof = true;
		}
		fprintf(stderr, "================================ recv %d, window (%d ~ %d] ======================================\n", no, r->client->last_recv, r->client->last_legal);
		if ( (no > r->client->last_recv) && (no <= r->client->last_legal) ) {
			/* in the window */
			if (r->client->window[(no - 1) % RWS] != NULL) {
				assert(r->client->window[(no - 1) % RWS]->seqno == no);
			}
			r->client->window[(no - 1) % RWS] = pkt;
			if (r->client->expect == no) {
				while (r->client->window[(r->client->expect - 1) % RWS] != NULL) {
					buffer_enque_p(r->client->buffer, r->client->window[(r->client->expect - 1) % RWS]);
					r->client->window[(r->client->expect - 1) % RWS] = NULL;
					r->client->expect++;
				}
				r->client->last_recv = r->client->expect - 1;	
				r->client->last_legal = r->client->last_recv + RWS;			
				rel_output(r);
			}
		}
		else {
			/* discard this packet */
		}
		/* send acknowledgment back to server */
		ack_t ack;
		ack.len = htons(ACK_LEN);
		ack.ackno = htonl(r->client->expect);
		ack.cksum = cksum ((const void *)(&ack) + CKSUM_LEN, ACK_LEN - CKSUM_LEN); 
		conn_sendpkt (r->c, (const packet_t *)&ack, ACK_LEN);
	}	
	if (r->server->eof && r->client->eof && r->server->last_sent == r->server->last_acked) {
		rel_output(r);
		rel_destroy(r);
	}
}

void rel_send(rel_t *r) {
	seqno_t SWS = r->server->SWS;
	while ((r->server->last_sent - r->server->last_acked < SWS) && (!buffer_isEmpty(r->server->buffer))) {
		r->server->last_sent++;
		r->server->packet_window[(r->server->last_sent - 1) % SWS] = buffer_deque(r->server->buffer);
		packet_t *tmp = r->server->packet_window[(r->server->last_sent - 1) % SWS];
		if (packet_isEof(tmp->len)) {
			r->server->eof = true;
		}
		tmp->ackno = htonl(r->client->expect);
		tmp->seqno = htonl(r->server->last_sent);
		tmp->len = htons(tmp->len);
		tmp->cksum = cksum ((const void *)(tmp) + CKSUM_LEN, ntohs(tmp->len) - CKSUM_LEN);
		conn_sendpkt (r->c, tmp, ntohs(tmp->len));
		timespec_t *ti = malloc(sizeof(timespec_t));
		clock_gettime (CLOCK_REALTIME, ti);
		r->server->time_window[(r->server->last_sent - 1) % SWS] = ti;
	}
}


void rel_read (rel_t *s)
{
	/* need to transmit a packet for the first transmission */
	if (s->server->eof) {
		return;
	}
	char *buf = malloc(sizeof(char) * 500);
	memset(buf, 0, sizeof(char) * 500);
	int length = 0;
	while ((length = conn_input(s->c, (void *)buf, 500)) != 0) {
		if (length == -1) { /* end of file */
			length = 0;
			s->server->eof = true;
			buffer_enque_c(s->server->buffer, buf, 0); 
			break;
		}
		buffer_enque_c(s->server->buffer, buf, (uint16_t)length); 
		memset(buf, 0, sizeof(char) * 500);
	}
	free(buf);
	rel_send(s);
}

void rel_output (rel_t *r)
{
	packet_t *tmp = NULL;
	while(!buffer_isEmpty(r->client->buffer)) {
		if (conn_bufspace(r->c) >= r->client->buffer->head->next->len) {
			tmp = buffer_deque(r->client->buffer);
			conn_output(r->c, (void *)tmp->data, (size_t)tmp->len - 12);
			free(tmp);
			tmp = NULL;
		}
		else {
			break;
		}
	}
}

void rel_timer ()
{
	/* Retransmit any packets that need to be retransmitted */
	timespec_t now;
	clock_gettime(CLOCK_REALTIME, &now);
	seqno_t i = 0;
	rel_t *r = rel_list;
	long interval;
	int idx = 0;
	while (r != NULL) {
		for (i = r->server->last_acked + 1; i <= r->server->last_sent; i++) {
			idx = (i - 1) % r->server->SWS;
			interval = (now.tv_sec * 1000000 + now.tv_nsec);
			interval -= r->server->time_window[idx]->tv_nsec * 1000000 + r->server->time_window[idx]->tv_nsec;
			if (interval > (long)r->timeout * 1000000) {
				conn_sendpkt (r->c, r->server->packet_window[idx], ntohs(r->server->packet_window[idx]->len));
				clock_gettime(CLOCK_REALTIME, r->server->time_window[idx]);
			}
		}
		r = r->next;
	}
}
