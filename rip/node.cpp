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
		itf->clientFD = socket(AF_INET, SOCK_STREAM, 0);
		if (itf->clientFD < 0) {
			return 1;
		}

		struct sockaddr_in serAddr;
		memset((char *)&serAddr, 0, sizeof(struct sockaddr_in));
		serAddr.sin_addr.s_addr = inet_addr(LOCAL_HOST);
		serAddr.sin_family = AF_INET;
		serAddr.sin_port = htons(itf->port);

		if (connect(itf->clientFD, (struct sockaddr*)&serAddr, sizeof(struct sockaddr)) < 0) {
			return 2;
		}

		// server
		int sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0) {
			return 3;
		}

		struct sockaddr_in cliAddr;
		memset((char *)&cliAddr, 0, sizeof(struct sockaddr_in));
		cliAddr.sin_addr.s_addr = INADDR_ANY;
		cliAddr.sin_family = AF_INET;
		cliAddr.sin_port = htons(node->port);

		if (bind(sock, (struct sockaddr *)&cliAddr, sizeof(struct sockaddr)) < 0) {
			return 4;
		}

		listen(sock, MAX_BACK_LOG);
		socklen_t sin_size = sizeof(struct sockaddr_in);
		itf->serverFD = accept(sock, (struct sockaddr *)&cliAddr, &sin_size);
		if (itf->serverFD < 0) {
	        printf ("Error no is : %d\n", errno);
	        printf("Error description : %s\n",strerror(errno));
			return 5;
		}

		cout<<"connect "<<node->port<<" "<<itf->port<<endl;

		itf = itf->next;
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
	case 2:
		perror("client connect error");
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
	case 5:
		perror("accept error");
		delete(node);
		return 0;
	default:
		break;
	}
	

	string jwn;
	cin>>jwn;
	Iface *itf = node->iface;
	while(itf) {
		close(itf->clientFD);
		close(itf->serverFD);
	}


	delete(node);
	return 0;
}

