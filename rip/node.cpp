#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <queue>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "node.h"
#include "router.h"
#include "protocal.h"
using namespace std;


void *recvRoutine(void *nn) {
	Node *node = (Node *)nn;
	char buffer[MAX_IP_LEN];
	while (1) {
		pthread_testcancel();
		if (!node->recvp(buffer)) {
			break;
		}
	}
	return NULL;
}

Node::~Node() {
	killRecv();
	delFace();
	close(sfd);
	free(saddr);
}

void Node::delFace() {
	int i = 0; 
	for (; i < numFace; i++) {
		close(it[i]->cfd);
		free(it[i]->saddr);
		delete it[i];
	}
}

bool Node::startLink() {
	int status = createConn();
	if (!isGoodConn(status)) {
		return false;
	}
	return startRecv();
}

int Node::createConn() {
	Nface *nf = NULL;
	int i = 0;
	for (; i < numFace; i++) {
		// client
		nf = it[i];
		nf->cfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (nf->cfd < 0) {
			return 1;
		}

		nf->saddr = (Sockaddr_in *)malloc(sizeof(Sockaddr_in));
		memset((char *)nf->saddr, 0, sizeof(Sockaddr_in));
		nf->saddr->sin_addr.s_addr = inet_addr(LOCAL_HOST);
		nf->saddr->sin_family = AF_INET;
		nf->saddr->sin_port = htons(nf->port);

		cout<<"connect "<<port<<" "<<nf->port<<endl;
	}


	// server
	sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sfd < 0) {
		return 3;
	}

	saddr = (Sockaddr_in *)malloc(sizeof(Sockaddr_in));

	memset((char *)saddr, 0, sizeof(Sockaddr_in));
	saddr->sin_addr.s_addr = INADDR_ANY;
	saddr->sin_family = AF_INET;
	saddr->sin_port = htons(port);

	if (bind(sfd, (Sockaddr *)saddr, sizeof(Sockaddr)) < 0) {
		return 4;
	}
	return 0;
}

bool Node::isGoodConn(int result) {
	switch(result) {
	case 1:
		perror("client create socket error");
		return false;
	case 3:	
		perror("server create socket error");
		return false;
	case 4:
		perror("server bind error");
		return false;
	default:
		break;
	}
	return true;
}

bool Node::sendp(int id, char msg[], bool flag, in_addr_t destIP) {
	// wrap the msg < MAX_MSG_LEN to a packet
	// flag == true: message
	// flag == false: rip
	Nface *nf = it[id];
	socklen_t addrlen = ADDR_LEN;
	ssize_t len, result;
	len = sizeof(ip_t) + strlen(msg);
	char *packet = (char *)malloc(len);
	memcpy((void *)packet + sizeof(ip_t), (void *)msg, strlen(msg));
	ip_t *ip = (ip_t *)packet;
	ip->hl = sizeof(ip_t);
	ip->version = 4;
	ip->tos = 0;
	ip->length = htons(sizeof(ip_t) + strlen(msg));
	ip->iden = htons(0);
	ip->offset = htons(0);
	ip->ttl = 255;
	ip->protocal = (flag)? 0:200;
	ip->sum = 0;
	ip->src = htonl(nf->localIP);
	ip->dest = htonl(destIP);
	ip->sum = ip_sum(packet, len);

	result = sendto(nf->cfd, packet, len, 0, (Sockaddr *)nf->saddr, addrlen);
	if (result == -1) {
		perror("socket send error");
		return false;
	}
	assert(result == len);
	return true;
}

bool Node::sendp(int id, string longMsg, bool flag, in_addr_t destIP) {
	// works when message length > MAX_MSG_LEN
	queue<string> que;
	string msg;
	if (longMsg.size() > MAX_MSG_LEN) {
		do {
			msg = longMsg.substr(0, MAX_MSG_LEN);
			que.push(msg);
			longMsg = longMsg.substr(MAX_MSG_LEN);
		} while (longMsg.size() > MAX_MSG_LEN);
	}
	else {
		que.push(longMsg);
	}

	while (!que.empty()) {
		msg = que.front();
		que.pop();
		if (!sendp(id, msg.c_str(), bool flag, in_addr_t destIP)) {
			return false;
		}
	}
	return true;
}

bool Node::recvp(char buffer[]) {
	socklen_t addrlen = 0;
	Sockaddr_in sin;	
	memset((void *)buffer, 0, MAX_IP_LEN);
	ssize_t len = 0;
	len = recvfrom(sfd, buffer, MAX_IP_LEN, 0, (Sockaddr *)&sin, &addrlen);
	if (len == -1) {
		perror("socket receive error");
		return false;
	}

	ip_t *ip = (ip_t *)buffer;
	in_addr_t destIP = ip->dest;
	Nface *nf = NULL;
	int i = 0;
	for (; i < it.size(); i++) {
		if (it[i]->localIP == destIP) {
			nf = it[i];
			break;
		}
	}

	if (nf == NULL) {
		ip->ttl -= 1;
		if (ip->ttl == 0) {
			printf("ttl = 0, discard packet\n");
		}
		else {
			void *arg = malloc(3 * sizeof(void *));
			*arg = &router;
			*(arg + 1) = &destIP;
			*(arg + 2) = &(buffer + sizeof(ip_t));

		}
	}
	else {
		if (ip->protocal == 200) {

		}
		else if (ip->protocal == 0) {
			printf("receive message: %s\n", buffer + sizeof(ip_t));
		}
	}
	return true;
}

bool Node::startRecv() {
	int r = pthread_create(&tidRecv, NULL, &recvRoutine, (void *)this);
	if (r != 0) {
		printf("thread create error\n");
		return false;
	}
	recving = true;
	return true;
}

void Node::killRecv() {
	if (recving) {
		pthread_cancel(tidRecv);
		pthread_join(tidRecv, NULL);
		recving = false;
	}
}

void Node::setRipHandler(bool (*func)(void *arg)) {
	ripHandler = func;
}

void Node::setMsgHandler(bool (*func)(void *arg)) {
	msgHandler = func;
}





