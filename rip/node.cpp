#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "node.h"
using namespace std;


Node *createNode(char argv[]) {
	ifstream input;
	input.open(argv);
	string line;
	string host, port;
	int myPort = 0;

	
	input>>line;
	host = line.substr(0, 9);
	if (host != "localhost") {
		fprintf(stderr, "input format error 1\n");
		return NULL;
	}
	port = line.substr(10);
	myPort = stoi(port);
	if (myPort < 1024 || myPort > 65535) {
		fprintf(stderr, "port number unusable\n");
		return NULL;
	}
	Node *node = new Node(myPort);

	string ip1, ip2;
	Iface *itf = new Iface();
	node->iface = itf;
	int num = 0;
	while(input>>line) {
		host = line.substr(0, 9);
		if (host != "localhost") {
			fprintf(stderr, "input format error 2\n");
			delete node;
			return NULL;
		}
		port = line.substr(10);
		input>>ip1;
		input>>ip2;
		num++;
		itf->next = new Iface(stoi(port), ip1, ip2, num);
		itf = itf->next;
	}
	itf = node->iface;
	node->iface = node->iface->next;
	delete itf;
	node->numIface = num;
	return node;
}

int createConn(Node *node) {
	Iface *itf = node->iface;
	while (itf) {
		// client
		itf->cfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (itf->cfd < 0) {
			return 1;
		}

		itf->saddr = (Sockaddr_in *)malloc(sizeof(Sockaddr_in));
		memset((char *)itf->saddr, 0, sizeof(Sockaddr_in));
		itf->saddr->sin_addr.s_addr = inet_addr(LOCAL_HOST);
		itf->saddr->sin_family = AF_INET;
		itf->saddr->sin_port = htons(itf->port);

		cout<<"connect "<<node->port<<" "<<itf->port<<endl;

		itf = itf->next;
	}


	// server
	node->sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (node->sfd < 0) {
		return 3;
	}

	node->saddr = (Sockaddr_in *)malloc(sizeof(Sockaddr_in));

	memset((char *)node->saddr, 0, sizeof(Sockaddr_in));
	node->saddr->sin_addr.s_addr = INADDR_ANY;
	node->saddr->sin_family = AF_INET;
	node->saddr->sin_port = htons(node->port);

	if (bind(node->sfd, (Sockaddr *)node->saddr, sizeof(Sockaddr)) < 0) {
		return 4;
	}
	return 0;
}

int main(int argc, char *argv[]) {
	
	Node *node = createNode(argv[1]);
	int result = createConn(node);
	switch(result) {
	case 1:
		perror("client create socket error");
		delete(node);
		return 0;
	case 3:	
		perror("server create socket error");
		delete(node);
		return 0;
	case 4:
		perror("server bind error");
		delete(node);
		return 0;
	default:
		break;
	}
	

	cout<<"enter jwn"<<endl;
	string jwn;
	cin>>jwn;
	Iface *itf = node->iface;
	while(itf) {
		close(itf->cfd);
		itf = itf->next;
	}
	close(node->sfd);


	delete(node);
	return 0;
}

