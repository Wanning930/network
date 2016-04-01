/* router.h */
#include <map>
#include <list>
#include "node.h"


struct Rface {
	in_addr_t localIP;
	in_addr_t nextIP;
	Rface(in_addr_t a, in_addr_t b): localIP(a), nextIP(b) {};
	Rface(string a, string b): localIP(inet_addr(a.c_str())), nextIP(inet_addr(b.c_str())) {};
};

struct Entry {
	in_addr_t destIP;
	int cost;
	in_addr_t nextIP;
	Entry(string a, int b, string c): destIP(inet_addr(a.c_str())), cost(b), nextIP(inet_addr(c.c_str())) {};
	Entry(in_addr_t a, int b, in_addr_t c): destIP(a), cost(b), nextIP(c) {};
};

class Router {
public:
	list<Entry *> rt;
	pthread_mutex_t rtlock;
	vector<Rface *> it; // [(local ip, next hop ip)]
	Node *node;

	Router(in_port_t p);
	~Router();

	void delRt();
	void delIt();

private:
};