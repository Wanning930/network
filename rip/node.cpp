#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "node.h"
#include "router.h"
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

bool Node::sendp(int id, char *pkt, size_t len) {
	Nface *nf = it[id];
	ssize_t result = 0;
	size_t addrlen = sizeof(Sockaddr_in);
	result = sendto(nf->cfd, pkt, len, 0, (Sockaddr *)nf->saddr, addrlen);
	if (result < len) {
		return false;
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

	pthread_t tid = 0;
	char *arg = (char *)malloc(sizeof(char *) + MAX_IP_LEN);
	memcpy(arg, &router, sizeof(void *));
	memcpy(arg + sizeof(void *), buffer, MAX_IP_LEN);
	
	pthread_create(&tid, NULL, handler, (void *)arg);

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
	else {
		printf("false kill receive\n");
	}
}

void Node::setHandler(void * (*func)(void *arg)) {
	handler = func;
}





