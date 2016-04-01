#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "node.h"
using namespace std;

#define ADDR_LEN (sizeof(struct sockaddr_in))
#define MAX_MSG_LEN (1400)

void *recvRoutine(void *nn) {
	Node *node = (Node *)nn;
	char buffer[MAX_MSG_LEN];
	while (1) {
		pthread_testcancel();
		node->recvp(buffer);
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

void Node::sendp(int id, string msg) {
	Nface *nf = it[id];
	socklen_t addrlen = ADDR_LEN;
	sendto(nf->cfd, msg.c_str(), msg.size(), 0, (Sockaddr *)nf->saddr, addrlen);
}

void Node::recvp(char buffer[]) {
	socklen_t addrlen = 0;
	Sockaddr_in sin;	
	memset((void *)buffer, 0, MAX_MSG_LEN);
	recvfrom(sfd, buffer, MAX_MSG_LEN, 0, (Sockaddr *)&sin, &addrlen);
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

void Node::register(void (*func)(int)) {
	handler = func;
}






