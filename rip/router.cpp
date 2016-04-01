/* router.cpp */
#include "router.h"
using namespace std;

Router::Router(unsigned short p) {
	node = new Node(p);
}

Router::~Router() {
	delete node;
	delRt();
	delIt();
}

void Router::delRt() {
	list<Entry *>::iterator pt = rt.begin();
	for (; pt != rt.end(); pt++) {
		delete *pt;
		*pt = NULL;
	}
}

void Router::delIt() {
	int num = it.size();
	int i = 0;
	for (; i < num; i++) {
		delete it[i];
		it[i] = NULL;
	}
}