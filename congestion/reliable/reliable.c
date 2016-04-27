
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
typedef struct reliable_buffer buffer_t;
// typedef struct reliable_state rel_t;
typedef struct ack_packet ack_t;

#define ACK_LEN 12
#define PKT_HDR 16
#define PKT_LEN 1000
#define CKSUM_LEN (sizeof(uint16_t))
#define ALPHA (0.125)
#define BETA (0.25)

void buffer_enque_c(buffer_t *buffer, char *data, uint16_t len);
void buffer_enque_p(buffer_t *buffer, packet_t *packet);
packet_t *buffer_deque(buffer_t *buffer);
bool buffer_isEmpty(buffer_t *buffer);
bool packet_isAck(size_t n);
void rel_send(rel_t *r);
bool packet_isEof(size_t n);
void throughput(rel_t *r, timespec_t *t1, timespec_t *t2);
void update_rtt(rel_t *r, seqno_t idx);
bool safe_destroy(rel_t *r);
double time_diff(timespec_t large, timespec_t small);
double time_ms(timespec_t ti);
void fast_retran(rel_t *r, packet_t *pkt);

struct reliable_client {
	seqno_t RWS;
	seqno_t last_recv;
	seqno_t last_legal;
	seqno_t expect;
	packet_t **window; /* ordered and overwritten, size = RWS */
	buffer_t *buffer;
	seqno_t tpt;
	bool eof;
};

struct packet_node {
	uint16_t len;
	char *content; /* might be pure text or a encapsulated packet */
	pnode_t *next;
};

struct reliable_buffer {
	pnode_t *head;
	pnode_t *tail;
	int size;
};

struct reliable_server { /* send data packet and wait for ack */
	seqno_t SWS;
	seqno_t last_acked;
	seqno_t last_sent;
	packet_t **packet_window; /* last_acked < x <= last_sent might retransmit later, size = ad_window*/
	timespec_t **time_window;
	seqno_t dup;
	bool dup_flag; // false and dup == ackno, fast retransmit
	buffer_t *buffer;
	bool eof;
	seqno_t cwnd;
	seqno_t cwnd_cnt;
	bool ff;
	double est_rtt;
	double dev_rtt;
	seqno_t effect_window;
};

struct reliable_state {

	conn_t *c;			/* This is the connection object */

	/* Add your own data fields below this */
	int flag;
	client_t *client;
	server_t *server;
	timespec_t *record;
	int timeout; //millisecond

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
		memcpy(node->content, data, sizeof(char) * len);
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
	r->record = (timespec_t *)malloc(sizeof(timespec_t));
	clock_gettime(CLOCK_REALTIME, r->record);

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
	r->client->tpt = 0;

	/* server initialization */
	// r->timeout = cc->timeout;
	r->timeout = 200;
	r->server = malloc(sizeof(server_t));
	memset(r->server, 0, sizeof(server_t));
	r->server->SWS = 200; // r->server->SWS = cc->window;
	r->server->last_acked = 0;
	r->server->last_sent = 0;
	r->server->packet_window = malloc((r->server->SWS) * sizeof(packet_t *));
	memset (r->server->packet_window, 0, (r->server->SWS) * sizeof(packet_t *));
	for (i = 0; i < cc->window; i++) {
		r->server->packet_window[i] = (packet_t *)malloc(sizeof(packet_t));
	}
	r->server->time_window = malloc((r->server->SWS) * sizeof(timespec_t *));
	memset (r->server->time_window, 0, (r->server->SWS) * sizeof(timespec_t *));
	r->server->dup = 0;
	r->server->dup_flag = true;
	r->server->buffer = malloc(sizeof(buffer_t));
	r->server->buffer->head = malloc(sizeof(pnode_t));
	memset (r->server->buffer->head, 0, sizeof(pnode_t));
	r->server->buffer->tail = r->server->buffer->head;
	r->server->buffer->size = 0;
	r->server->eof = false;
	r->server->cwnd = 1;
	r->server->cwnd_cnt = 0;
	r->server->ff = false; // true: fast retransmit; false: slow start
	r->server->est_rtt = 0.0;
	r->server->dev_rtt = 0.0;
	r->server->effect_window = 1;

	if (cc->sender_receiver == RECEIVER) {
		r->flag = RECEIVER;
		// send ACK
		r->server->eof = true;
		ack_t ack;
		ack.len = htons(ACK_LEN);
		ack.ackno = htonl(0);
		ack.cksum = cksum((const void *)(&ack) + CKSUM_LEN, ACK_LEN - CKSUM_LEN);
		conn_sendpkt(r->c, (const packet_t *)(&ack), ACK_LEN);
		// fprintf(stderr, "=================== send ack ==================%d \n", ack.ackno);
	}
	else {
		r->flag = SENDER;
		r->client->eof = true;
	}

	return r;
}

void rel_destroy (rel_t *r)
{
	conn_destroy (r->c);

	if (r->flag == SENDER) {
		/* Print time */
		timespec_t now;
		clock_gettime(CLOCK_REALTIME, &now);
		double total = time_diff(now, *(r->record));
		fprintf(stderr, "Total time: %f ms\n", total);
		long amount = r->server->last_sent * (PKT_HDR + PKT_LEN) * 8;
		if (total == 0) {
			fprintf(stderr, "aaaaaaaaabbbbbbbbbccccccccc\n");
		}
		double rate = (double)amount * 1000.0 / total;
		fprintf(stderr, "file size = %ld\n", amount);
		fprintf(stderr, "Average throughput: %f\n", rate);
	}


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
	// fprintf(stderr, "get a pkt\n");
	seqno_t SWS = r->server->SWS;
	seqno_t RWS = r->client->RWS;
	if (pkt->cksum != cksum((void *)pkt + CKSUM_LEN, n - CKSUM_LEN)) {
		/* discard this packet */
		// fprintf(stderr, ".................. check sum wrong for seqno %d ........................\n", ntohl(pkt->seqno));
		return;
	}
	pkt->len = ntohs(pkt->len);
	pkt->ackno = ntohl(pkt->ackno);
	pkt->rwnd = ntohl(pkt->rwnd);
	seqno_t no = ~0;
	if ((size_t)pkt->len != n) {
		/* discard this packet */
		// fprintf(stderr, ".................. check len wrong for seqno %d, pkt->len %zu, n %zu ........................\n", ntohl(pkt->seqno), (size_t)pkt->len, n);
		return;
	}

	if (packet_isAck(n)) {
		// fprintf(stderr, "~~~~~~~~~~~~~~~~~~~~ receive ack no = %d ~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n", pkt->ackno);
		if (pkt->ackno <= 1) {
			// I'm server, close client
			r->client->eof = true;
		}
		else {
			// effective window
			r->server->effect_window = pkt->rwnd - (r->server->last_sent - r->server->last_acked + 1);
			// printf("effective window %d\n", r->server->effect_window);

			fast_retran(r, pkt);

			// effective ack
			if (pkt->ackno - r->server->last_acked > 1) {
				if (r->server->ff) { // fast retransmit
					r->server->cwnd_cnt += (pkt->ackno - r->server->last_acked - 1);
					if (r->server->cwnd_cnt >= r->server->cwnd) {
						r->server->cwnd++;
						fprintf(stderr, "cwnd = %d\n", r->server->cwnd);
						r->server->cwnd_cnt = 0;
					}
				}
				else { // slow start
					r->server->cwnd++;
					fprintf(stderr, "cwnd = %d\n", r->server->cwnd);
				}				
				while (pkt->ackno - r->server->last_acked > 1) {
					r->server->last_acked++;
					// update_rtt(r, r->server->last_acked);
					free(r->server->time_window[(r->server->last_acked - 1) % SWS]);
					r->server->time_window[(r->server->last_acked - 1) % SWS] = NULL;
					free(r->server->packet_window[(r->server->last_acked - 1) % SWS]);
					r->server->packet_window[(r->server->last_acked - 1) % SWS] = NULL;
				}
			}
			rel_send(r);
		}
	}
	else if (!r->client->eof) {
		pkt->seqno = ntohl(pkt->seqno);
		no = pkt->seqno;
		fprintf(stderr, "================================ recv %d, window (%d ~ %d] ======================================\n", no, r->client->last_recv, r->client->last_legal);
		if ((!r->client->eof) && (no > r->client->last_recv) && (no <= r->client->last_legal) ) {
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
				// fprintf(stderr, "================================ recv buffer size = %d ============================ \n", r->client->buffer->size);
				rel_output(r);
			}
		}
		else {
			/* discard this packet */
		}
		// fprintf(stderr, "`````````````````````````send ack no %d``````````````````````````\n", r->client->expect);
		ack_t ack;
		ack.len = htons(ACK_LEN);
		ack.ackno = htonl(r->client->expect);
		// int ad_window = RWS - (r->client->expect - r->client->last_read);
		int ad_window = RWS - r->client->buffer->size;
		ack.rwnd = htonl(ad_window);
		ack.cksum = cksum ((const void *)(&ack) + CKSUM_LEN, ACK_LEN - CKSUM_LEN); 
		conn_sendpkt (r->c, (const packet_t *)&ack, ACK_LEN);
	}
	if (safe_destroy(r)) {
		rel_output(r);
		rel_destroy(r);
	}
}

bool safe_destroy(rel_t *r) {
	bool f1 = r->server->eof;
	bool f2 = r->client->eof;
	bool f3 = (r->server->last_sent == r->server->last_acked);
	bool f4 = (r->client->last_recv == r->client->expect - 1);
	if (r->client->last_recv == 1001) {
		// fprintf(stderr, "recv 1000, r->client->expect = %d\n", r->client->expect);
	}
	return f1&&f2&&f3&&f4;
}

void rel_send(rel_t *r) {
	seqno_t SWS = r->server->SWS;
	seqno_t window_size = (r->server->cwnd > r->server->effect_window)? (r->server->effect_window):(r->server->cwnd);
	// fprintf(stderr, "window size %d\n", window_size);
	while ((r->server->last_sent - r->server->last_acked < window_size) && (!buffer_isEmpty(r->server->buffer))) {
		// fprintf(stderr, "r->server->last_sent %d, r->server->last_acked %d\n", r->server->last_sent, r->server->last_acked);
		// fprintf(stderr, "window size %d\n", window_size);
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
		// fprintf(stderr, ".................. send seqno = %d len = %zu........................\n", ntohl(tmp->seqno), (size_t)ntohs(tmp->len));
		conn_sendpkt (r->c, tmp, ntohs(tmp->len));
		timespec_t *ti = malloc(sizeof(timespec_t));
		clock_gettime (CLOCK_REALTIME, ti);
		r->server->time_window[(r->server->last_sent - 1) % SWS] = ti;
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
		bool kill = false;
		while (!kill && (length = conn_input(r->c, (void *)buf, PKT_LEN)) != 0) {
			switch(length) {
				case -1:
					r->server->eof = true;
					buffer_enque_c(r->server->buffer, buf, 0);
					kill = true;
					break;
				case PKT_LEN:
					buffer_enque_c(r->server->buffer, buf, (uint16_t)length);
					memset(buf, 0, sizeof(char) * PKT_LEN);
					break;
				default:
					r->server->eof = true;
					buffer_enque_c(r->server->buffer, buf, (uint16_t)length);
					memset(buf, 0, sizeof(char) * PKT_LEN);
					buf[0] = EOF;
					buffer_enque_c(r->server->buffer, buf, 0);
					kill = true;
					break;
			}
		}
		free(buf);
		rel_send(r);
	}
}

void rel_output (rel_t *r)
{
	packet_t *tmp = NULL;
	while (!buffer_isEmpty(r->client->buffer)) {
		if (conn_bufspace(r->c) >= r->client->buffer->head->next->len) {
			tmp = buffer_deque(r->client->buffer);
			if (packet_isEof(tmp->len)) {
				r->client->eof = true;
			}
			else {
				if (tmp->data[tmp->len - PKT_HDR - 1] == EOF) {
					r->client->eof = true;
				}
				conn_output(r->c, (void *)tmp->data, (size_t)tmp->len - PKT_HDR);
			}
			r->client->tpt++;
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
	if (r != NULL && r->flag == SENDER) {
		int re_cnt = r->server->effect_window;
		for (i = r->server->last_acked + 1; i <= r->server->last_sent && re_cnt > 0; i++) {
			idx = (i - 1) % r->server->SWS;
			interval = (long)time_diff(now, *(r->server->time_window[idx]));
			if (interval > (long)r->timeout) {
				// fprintf(stderr, "interval = %ld, last_acked = %d, last_send = %d, i = %d\n", interval, r->server->last_acked, r->server->last_sent, i);
				// fprintf(stderr, ".................. retransmit send seqno = %d len = %zu........................\n", ntohl(r->server->packet_window[idx]->seqno), (size_t)ntohs(r->server->packet_window[idx]->len));
				conn_sendpkt (r->c, r->server->packet_window[idx], ntohs(r->server->packet_window[idx]->len));
				clock_gettime(CLOCK_REALTIME, r->server->time_window[idx]);
				r->server->cwnd /= 2;
				r->server->cwnd_cnt = 0;
				fprintf(stderr, "cwnd = %d\n", r->server->cwnd);
				r->server->ff = true;
				re_cnt--;

				break;
			}
		}
	}
	else if (r != NULL && r->flag == RECEIVER) {
		throughput(r, &now, r->record);
		clock_gettime(CLOCK_REALTIME, r->record);
	}

}

void throughput(rel_t *r, timespec_t *t1, timespec_t *t2) {	
	long sec = (long)time_diff(*t1, *t2);
	if (sec != 0 && sec % 10 == 0) { //0.01s
		int amount = r->client->tpt * (PKT_HDR + PKT_LEN);
		double result = (8.0 * (double)amount * 1000.0)/(double)sec;
		// fprintf(stderr, "throughput sample %f\n", result);
		r->client->tpt = 0;
	}
	
}

void update_rtt(rel_t *r, seqno_t idx) {
	timespec_t now;
	clock_gettime(CLOCK_REALTIME, &now);
	double sample = time_diff(now, *(r->server->time_window[(idx - 1) % r->server->SWS]) );
	// fprintf(stderr, "sample = %f\n", sample);
	r->server->est_rtt = (1 - ALPHA) * r->server->est_rtt + ALPHA * sample;
	double diff = (sample > r->server->est_rtt)? (sample - r->server->est_rtt):(r->server->est_rtt - sample); 
	// fprintf(stderr, "diff = %f\n", diff);
	r->server->dev_rtt = (1 - BETA) * r->server->dev_rtt + BETA * diff;
	r->timeout = (int)(r->server->est_rtt + 4 * r->server->dev_rtt);
	fprintf(stderr, "update rtt to %d\n", r->timeout);
}


double time_diff(timespec_t large, timespec_t small) {
	double ms = large.tv_sec - small.tv_sec;
	ms *= 1000000000;
	ms += large.tv_nsec - small.tv_nsec;
	ms /= 1000000;
	return ms;
}

double time_ms(timespec_t ti) {
	double ms = ti.tv_sec;
	ms *= 1000000000;
	ms += ti.tv_nsec;
	ms /= 1000000;
	return ms;
}


void fast_retran(rel_t *r, packet_t *pkt) {
	if (pkt->ackno == r->server->dup) {
		if (r->server->dup_flag) {
			r->server->dup_flag = false; 
		}
		else {
			// fast retransmit
			r->server->ff = true;
			// r->server->cwnd = r->server->cwnd / 2;
			r->server->cwnd_cnt = 0;
			fprintf(stderr, "pkt->ackno = %d, fast retransmit\n", pkt->ackno);
			int idx = (pkt->ackno - 1) % r->server->SWS;
			conn_sendpkt (r->c, r->server->packet_window[idx], ntohs(r->server->packet_window[idx]->len));
			clock_gettime(CLOCK_REALTIME, r->server->time_window[idx]);	
			r->server->dup = 0; // clear duplication count, different from anyone
			r->server->dup_flag = true;
		}
	}
	else {
		r->server->dup = pkt->ackno;
		r->server->dup_flag = true;
	}
}