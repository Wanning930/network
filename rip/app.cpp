/* app.cpp */
#include <iostream>
#include <fstream>
#include <string>
#include <stdio.h>
#include <unistd.h>
#include "node.h"
#include "router.h"
using namespace std;

bool isBadHost(string host) {
	bool r = (host != "localhost");
	if (r) {
		fprintf(stderr, "all ports open on host\n");
	}
	return r;
}

bool isBadPort(int port) {
	bool r = (port < 1024 || port > 65535);
	if (r) {
		fprintf(stderr, "port number %d unusable\n", port);
	}
	return r;
}

bool init(char argv[], Router *router) {
	ifstream input;
	input.open(argv);
	string line;
	string host;
	int port = 0;
	
	input>>line;
	if (isBadHost(host = line.substr(0, 9)) || 
		isBadPort(port = stoi(line.substr(10)))) {
		return false;
	}

	router = new Router(port);

	string ip1, ip2;
	Nface *nf = NULL;
	Rface *rf = NULL;
	int num = 0;
	while(input>>line) {
		if (isBadHost(host = line.substr(0, 9)) || 
			isBadPort(port = stoi(line.substr(10)))) {
			delete router;
			return false;
		}
		input>>ip1>>ip2;
		nf = new Nface(port);
		rf = new Rface(ip1, ip2);
		router->node->it.push_back(nf);
		router->it.push_back(rf);
		num++;
	}
	router->node->numFace = num;

	if (!router->node->startLink()) {
		delete router;
		return false;
	}
	return true;
}

int main(int argc, char *argv[]) {
	
	// create network topology
	Router *router = NULL;
	if (!init(argv[1], router)) {
		return 0;
	}

	// listen to command line
	string cmd;
	while (cin>>cmd) {
		if (cmd == "exit") {
			break;
		}
		else {
			cout<<"cmd: "<<cmd<<endl;
		}
	}

	delete router;
	return 0;
}