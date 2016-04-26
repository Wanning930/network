
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
typedef struct time_node tnode_t;
typedef struct reliable_buffer buffer_t;
// typedef struct reliable_state rel_t;
typedef struct ack_packet ack_t;

#define ACK_LEN 12
#define PKT_HDR 16
#define PKT_LEN 1000
#define CKSUM_LEN (sizeof(uint16_t))

void buffer_enque_c(buffer_t *buffer, char *data, uint16_t len);
void buffer_enque_p(buffer_t *buffer, packet_t *packet);
packet_t *buffer_deque(buffer_t *buffer);
bool buffer_isEmpty(buffer_t *buffer);
bool packet_isAck(size_t n);
void rel_send(rel_t *r);
bool packet_isEof(size_t n);

struct reliable_client {
	seqno_t RWS;
	seqno_t last_recv;
	seqno_t last_legal;
	seqno_t expect;
	packet_t **window; /* ordered and overwritten, size = RWS */
	buffer_t *buffer;
	bool eof;
}

struct packet_node {
	uint16_t len;
	char *content; /* might be pure text or a encapsulated packet */
	pnode_t *next;
};

struct reliable_buffer {
	pnode_t *head;
	pnode_t *tail;
	int size;
}

struct reliable_server { /* send data packet and wait for ack */
	seqno_t SWS;
	seqno_t last_acked;
	seqno_t last_sent;
	pnode_t *packet_window; /* last_acked < x <= last_sent might retransmit later, size = adWindow*/
	tnode_t *time_window;
	buffer_t *buffer;
	bool eof;
};

struct reliable_state {

	conn_t *c;			/* This is the connection object */

	/* Add your own data fields below this */
	client_t *client;
	server_t *server;
	int timeout;

};
rel_t *rel_list;

void buffer_enque_c(buffer_t *buffer, char *data, uint16_t len) {
	assert(len <= PKT_LEN);
	pnode_t *node = malloc(sizeof(pnode_t));
	memset(node, 0, sizeof(pnode_t));
	if (len == 0) {
		node->content = NULL;
	}
	else {
		node->content = malloc(sizeof(char) * len);
		memcpy(node->content, data, sizefo(char) * len);
	}
	node->len = len;
	node->next = NULL;
	buffer->tail->next = node;
	buffer->tail = buffer->tail->next;
	buffer->size++;
}

void buffer_enque_p(buffer_t *buffer, packet_t *packet) {
	pnode_t *node = malloc(sizeof(pnode_t));
	node->content = malloc(packet->len - PKT_HDR);
	memcpy(node->content, packet->data, packet->len - PKT_HDR);
	node->len = packet->len - PKT_HDR;
	node->next = NULL;
	buffer->tail->next = node;
	buffer->tail = buffer->tail->next;
	buffer->size++;
}

packet_t *buffer_deque(buffer_t *buffer) {
	assert(!buffer_isEmpty(buffer));
	pnode_t *pt = buffer->head->next;
	packet_t *newpt = malloc(pt->len + PKT_HDR);
	memset(newpt, 0, pt->len + PKT_HDR);
	memcpy(newpt->data, pt->content, (size_t)(pt->len));
	newpt->len = pt->len + PKT_HDR; /* payload + PKT_HDR */
	free(buffer->head->content);
	free(buffer->head);
	buffer->head = pt;
	buffer->size--;
	return newpt;
}

bool buffer_isEmpty(buffer_t *buffer) {
	return (buffer->head == buffer->tail);
}

bool packet_isAck(size_t n) {
	return (n == ACK_LEN);
}

bool packet_isEof(size_t n) {
	return (n == PKT_HDR);
}

/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *rel_create (conn_t *c, const struct sockaddr_storage *ss,
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
	rel_list = r;

	/* Do any other initialization you need here */

	/* client initialization */
	r->client = malloc(sizeof(client_t));
	memset((char *)r->client, 0, sizeof(client_t));
	r->client->RWS = cc->window;
	r->client->last_recv = 0;
	r->client->last_legal = r->client->RWS;
	r->client->expect = 1;
	r->client->window = malloc((r->client->RWS) * sizeof(packet_t *));
	memset(r->client->window, 0, (r->client->RWS) * sizeof(packet_t *));
	int i = 0;
	for (; i < cc->window; i++) {
		r->client->window[i] = malloc(sizeof(packet_t));
	}
	r->client->buffer = malloc(sizeof(buffer_t));
	r->client->buffer->head = malloc(sizeof(pnode_t));
	memset(r->client->buffer->head, 0, sizeof(pnode_t));
	r->client->buffer->tail = r->client->buffer->head;
	r->client->buffer->size = 0;
	r->client->eof = false;

	/* server initialization */
	r->timeout = cc->timeout;
	r->server = malloc(sizeof(server_t));
	memset(r->server, 0, sizeof(server_t));
	r->server->SWS = cc->window;
	r->server->last_acked = 0;
	r->server->last_sent = 0;
	r->server->packet_window = malloc((r->server->SWS) * sizeof(packet_t *));
	memset (r->server->packet_window, 0, (r->server->SWS) * sizeof(packet_t *));
	for (i = 0; i < cc->window; i++) {
		r->server->packet_window[i] = malloc(sizeof(packet_t));
	}
	r->server->time_window = malloc((r->server->SWS) * sizeof(timespec_t *));
	memset (r->server->time_window, 0, (r->server->SWS) * sizeof(timespec_t *));
	r->server->buffer = malloc(sizeof(buffer_t));
	r->server->buffer->head = malloc(sizeof(pnode_t));
	memset (r->server->buffer->head, 0, sizeof(pnode_t));
	r->server->buffer->tail = r->server->buffer->head;
	r->server->buffer->size = 0;
	r->server->eof = false;

	if (cc->sender_receiver == RECEIVER) {
		// send ACK
		r->server->eof = true;
		ack_t ack;
		ack.len = htons(ACK_LEN);
		ack.ackno = htonl(0);
		ack.cksum = cksum((const void *)(&ack) + CKSUM_LEN, ACK_LEN - CKSUM_LEN);
		conn_sendpkt(r->c, (const packet_t *)(&ack), ACK_LEN);
	}

	return r;
}

void rel_destroy (rel_t *r)
{
	conn_destroy (r->c);

	/* Free any other allocated memory here */
	int i = 0; 
	for (i = 0; i < r->client->RWS; i++) {
		free(r->client->window[i]);
	}
	free(r->client->window);
	assert(buffer_isEmpty(r->client->buffer));
	free(r->client->buffer->head);
	free(r->client->buffer);
	free(r->client);
	for (i = 0; i < r->server->SWS; i++) {
		free(r->server->packet_window[i]);
	}
	free(r->server->packet_window);
	free(r->server->time_window);
	assert(buffer_isEmpty(r->server->buffer));
	free(r->server->buffer->head);
	free(r->server->buffer);
	free(r->server);
}


void rel_demux (const struct config_common *cc, const struct sockaddr_storage *ss, packet_t *pkt, size_t len)
{
 	//leave it blank here!!!

}

void rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
	seqno_t SWS = r->server->SWS;
	seqno_t RWS = r->client->RWS;
	if (pkt->cksum != cksum((void *)pkt + CKSUM_LEN, n - CKSUM_LEN)) {
		/* discard this packet */
		fprintf(stderr, ".................. check sum wrong for seqno %d ........................\n", ntohl(pkt->seqno));
		return;
	}
	pkt->len = ntohs(pkt->len);
	pkt->ackno = ntohl(pkt->ackno);
	seqno_t no = ~0;
	if ((size_t)pkt->len != n) {
		/* discard this packet */
		fprintf(stderr, ".................. check len wrong for seqno %d, pkt->len %zu, n %zu ........................\n", ntohl(pkt->seqno), (size_t)pkt->len, n);
		return;
	}

	if (packet_isAck(n)) {
		if (pkt->ackno == 0) {
			// I'm server
			r->client->eof = true;
		}
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
	else {
		pkt->seqno = ntohl(pkt->seqno);
		no = pkt->seqno;
		if (packet_isEof(n)) {
			fprintf(stderr, "~~~~~~~~~~~~~~~~~~~~~~~~~ client read eof ~~~~~~~~~~~~~~~~~~~~~~\n");
			r->client->eof = true;
		}
		fprintf(stderr, "================================ recv %x, window (%x ~ %x] ======================================\n", no, r->client->last_recv, r->client->last_legal);
		if ( (no > r->client->last_recv) && (no <= r->client->last_legal) ) {
			/* in the window */
			if (r->client->window[(no - 1) % RWS]->len != 0) {
				assert(r->client->window[(no - 1) % RWS]->seqno == no);
			}
			memcpy(r->client->window[(no - 1) % RWS], pkt, sizeof(packet_t));
			if (r->client->expect == no) {
				while (r->client->window[(r->client->expect - 1) % RWS]->len != 0) {
					buffer_enque_p(r->client->buffer, r->client->window[(r->client->expect - 1) % RWS]);
					memset(r->client->window[(r->client->expect - 1) % RWS], 0, sizeof(packet_t));
					r->client->expect++;
				}
				r->client->last_recv = r->client->expect - 1;	
				r->client->last_legal = r->client->last_recv + RWS;			
				fprintf(stderr, "================================ recv buffer size = %d ============================ \n", r->client->buffer->size);
				rel_output(r);
			}
		}
		else {
			/* discard this packet */
		}
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


void rel_read (rel_t *r)
{
	if(r->c->sender_receiver == RECEIVER)
	{
		if (r->client->eof) {
			return;
		}
		else {
			r->server->eof = true;
			ack_t ack;
			ack.len = htons(ACK_LEN);
			ack.ackno = htonl(0);
			ack.cksum = cksum((const void *)(&ack) + CKSUM_LEN, ACK_LEN - CKSUM_LEN);
			conn_sendpkt(r->c, (const packet_t *)(&ack), ACK_LEN);
		}
	}
	else //run in the sender mode
	{
		if (r->server->eof) {
			return;
		}
		char *buf = malloc(sizeof(char) * PKT_LEN);
		memset(buf, 0, sizeof(char) * PKT_LEN);
		int length = 0;
		while ((length = conn_input(r->c, (void *)buf, PKT_LEN)) != 0) {
			if (length == -1) {
				length = 0;
				r->server->eof = true;
				buffer_enque_c(r->server-buffer, buf, 0);
				break;
			}
			buffer_enqueue_c(r->server->buffer, buf, (uint16_t)length);
			memset(buf, 0, sizeof(char) * PKT_LEN);
		}
		free(buf);
		rel_send(s);
	}
}

void rel_output (rel_t *r)
{
	packet_t *tmp = NULL;
	while (!buffer_isEmpty(r->client->buffer)) {
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
				fprintf(stderr, ".................. retransmit send seqno = %d len = %zu........................\n", ntohl(r->server->packet_window[idx]->seqno), (size_t)ntohs(r->server->packet_window[idx]->len));
				conn_sendpkt (r->c, r->server->packet_window[idx], ntohs(r->server->packet_window[idx]->len));
				clock_gettime(CLOCK_REALTIME, r->server->time_window[idx]);
			}
		}
		r = r->next;
	}
}