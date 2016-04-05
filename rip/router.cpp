/* router.cpp */
#include <queue>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "router.h"
using namespace std;

#define CLOCKID CLOCK_REALTIME  
#define EXPIRE (11)
#define INFINITY (16)

void *routerRecv(void *arg);

void timer_thread(union sigval v) {  
	Router *router = (Router *)v.sival_ptr;
    // printf("jwn timer! %lu\n", router->it.size());  
	router->checkTimer();
}

string ntoa(struct in_addr in) {
	static pthread_mutex_t ntoaLock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_lock(&ntoaLock);
	char *ret = inet_ntoa(in);
	string r(ret);
	pthread_mutex_unlock(&ntoaLock);
	return r;
}

Router::Router(unsigned short p) {
	node = new Node(p);
	node->router = this;
	node->setHandler(routerRecv);
	pthread_mutex_init(&rtlock, NULL);
	timeStamp = 0;
}

Router::~Router() {
	delete node;
	delRt();
	delIt();
	timer_delete(timerid);
}

bool Router::checkTimer() {
	timeStamp++;
	if (timeStamp == 5) {
		timeStamp = 0;
		sendRip(true);
	}
	pthread_mutex_lock(&rtlock);
	bool flag = false;
	list<Entry *>::iterator pt = rt.begin();
	for (; pt != rt.end(); pt++) {
		if ((*pt)->timeStamp > EXPIRE) {
			(*pt)->cost = INFINITY;
			flag = true;
		}
		else if ((*pt)->cost != 0){
			(*pt)->timeStamp++;
		}
	}
	pthread_mutex_unlock(&rtlock);

	if (flag) {
		sendRip(true);
	}
}

bool Router::startTimer() {
	
	struct sigevent evp;
	memset(&evp, 0, sizeof(struct sigevent));

	evp.sigev_notify = SIGEV_THREAD;
	evp.sigev_notify_function = timer_thread; 
	evp.sigev_value.sival_ptr = this;
    if (timer_create(CLOCKID, &evp, &timerid) == -1) {  
        perror("fail to timer_create");  
        return false;  
    }  

	struct itimerspec it;  
    it.it_interval.tv_sec = 1;  
    it.it_interval.tv_nsec = 0;  
    it.it_value.tv_sec = 0;  
    it.it_value.tv_nsec = 500;  
	if (timer_settime(timerid, 0, &it, NULL) == -1) {
		perror("fail to timer_settime"); 
		return false;
	}
	return true;
}

void Router::delRt() {
	pthread_mutex_lock(&rtlock);
	list<Entry *>::iterator pt = rt.begin();
	for (; pt != rt.end(); pt++) {
		delete *pt;
		*pt = NULL;
	}
	pthread_mutex_unlock(&rtlock);
}

void Router::delIt() {
	int num = it.size();
	int i = 0;
	for (; i < num; i++) {
		delete it[i];
		it[i] = NULL;
	}
}

void Router::setActive(int id, bool flag) {
	cout<<"in setActive()"<<endl;

	pthread_mutex_lock(&rtlock);
	cout<<"in rtlock in setActive()"<<endl;
	list<Entry *>::iterator pt = rt.begin();

	int tmp = -1;
	if (flag) {
		for (; pt != rt.end(); pt++) {
			if ((*pt)->destIP == it[id]->localIP) {
				(*pt)->cost = 0;
				break;
			}			
		}
	}
	else {
		for (; pt != rt.end(); pt++) {
			if (findIt((*pt)->nextIP, false) == id) {
				(*pt)->cost = INFINITY;
			}
			else if ((*pt)->destIP == it[id]->localIP) {
				(*pt)->cost = INFINITY;
			}
		}
	}
	pthread_mutex_unlock(&rtlock);

	printTable();
	sendRip(true);
	it[id]->active = flag;
}

bool Router::rtUpdate(const in_addr_t dest, const in_addr_t src, int cost) {
	// dest and src is 1 hop (in rip entry)
	pthread_mutex_lock(&rtlock);
	int i = 0;
	bool result = false;

	cout<<"rtUpdate "<<ntoa({dest})<<" "<<ntoa({src})<<" "<<cost<<endl;

	list<Entry *>::iterator pt = rt.begin();
	for (; pt != rt.end(); pt++) {
		if ((*pt)->destIP == dest) {
			if ((*pt)->nextIP == src) {
				if (cost == INFINITY) {
					(*pt)->cost = INFINITY;
					(*pt)->timeStamp = 0;
					result = true;
				}
				else if ((*pt)->cost != cost + 1) {
					(*pt)->cost = cost + 1;
					result = true;
				}
				(*pt)->timeStamp = 0;
			}
			else if ((*pt)->cost > cost + 1) {
				(*pt)->cost = cost + 1;
				(*pt)->nextIP = src;
				(*pt)->timeStamp = 0;
				result = true;
			}
			break; // destination is unique
		}
		else if (src == dest && cost == INFINITY && (*pt)->nextIP == src) {
			cout<<"hit!!!!!!!!!"<<endl;
			(*pt)->cost = INFINITY;
			(*pt)->timeStamp = 0;
			result = true;
		}
	}

	if (pt == rt.end()) {
		result = true;
		in_addr_t newdest = (isDest(dest))? src : dest;
		Entry *entry = new Entry(newdest, cost + 1, src);
		entry->timeStamp = 0;
		rt.push_back(entry);
	}
	pthread_mutex_unlock(&rtlock);
	return result;
}

bool Router::sendRip(bool flag) {
	pthread_mutex_lock(&rtlock);

	int num = rt.size();
	size_t pktlen = sizeof(ip_t) + sizeof(rip_t) + num * sizeof(rip_entry_t);
	char *buf = (char *)malloc(pktlen * sizeof(char));
	memset(buf, 0, pktlen);
	ip_t *iph = (ip_t *)buf;
	rip_t *riph = (rip_t *)(buf + sizeof(ip_t));
	rip_entry_t *ent = (rip_entry_t *)(buf + sizeof(ip_t) + sizeof(rip_t));

	list<Entry *>::iterator pt = rt.begin();

	riph->cmd = (flag) ? REPLY : REQUEST;
	riph->num = rt.size();

	int itnum = it.size();
	int i = 0;
	iph->protocal = 200;
	for (; i < itnum; i++) {
		iph->src = it[i]->localIP;
		iph->dest = it[i]->nextIP;

		ent = (rip_entry_t *)(buf + sizeof(ip_t) + sizeof(rip_t));
		for (pt = rt.begin(); pt != rt.end(); pt++) {
			if ((*pt)->nextIP == it[i]->nextIP) {
				ent->cost = INFINITY;
			}
			else {
				ent->cost = (*pt)->cost;
			}
			ent->addr = (*pt)->destIP;
			ent += 1;
		}

		node->sendp(i, buf, pktlen);
	}

	free(buf);
	pthread_mutex_unlock(&rtlock);

	return true;
}

bool Router::recvRip(char *buf) {
	ip_t *iph = (ip_t *)buf;
	rip_t *riph = (rip_t *)(buf + sizeof(ip_t));
	rip_entry_t *entry = (rip_entry_t *)((char *)(riph) + sizeof(rip_t));
	int num = riph->num;
	printf("receive %d rip entries\n", num);
	int i = 0;
	in_addr_t src = iph->src;
	bool result = true;
	bool flag = (riph->cmd == REQUEST)? true : false;
	if (!flag) {
		for (; i < num; i++) {
			if (rtUpdate(entry->addr, src, entry->cost)) {
				flag = true;
			}
			entry += 1;
		}
	}

	printTable();

	if (flag) {
		if (!sendRip(true)) {
			result = false; 
		}
	}
	return result;
}

void Router::printTable() {
	pthread_mutex_lock(&rtlock);
	list<Entry *>::iterator pt = rt.begin();
	for(; pt != rt.end(); pt++) {
		printf("%s, %d, %s\n", ntoa({(*pt)->destIP}).c_str(), (*pt)->cost, ntoa({(*pt)->nextIP}).c_str());
	}
	printf("\n");
	pthread_mutex_unlock(&rtlock);
}

void *routerRecv(void *a) {
	// printf("handler function\n");
	char *arg = (char *)a;

	Router *router = NULL;
	memcpy(&router, arg, sizeof(void *));
	
	Sockaddr_in sina;
	socklen_t addrlen = sizeof(Sockaddr_in);
	memcpy(&sina, arg + sizeof(void *), addrlen);

	char *buf = (char *)malloc(MAX_IP_LEN);
	memcpy(buf, arg + sizeof(void *) + addrlen, MAX_IP_LEN);
	
	free(arg);

	ip_t *iph = (ip_t *)buf;
	// char *ax = ntoa(sina.sin_addr).c_str();
	// in_addr_t ay = inet_addr(ax);
	// if (router->findIt(ay) == -1) {
	// 	printf("receive packet from a down interface, %s\n", ax);
	// }
	// else 
	if (router->isDest(iph->dest)) {
		if (iph->protocal == 0) {
			printf("recv: %s\n", buf + sizeof(ip_t));
		}
		else {
			if (!router->recvRip(buf)) {
				printf("cannot rip\n");
			}
		}
	}
	else {
		assert(iph->protocal == 0);
		int id = router->findIt(iph->dest);
		if (id >= 0) {
			if (!router->node->sendp(id, buf, MAX_IP_LEN)) {
				printf("cannot send: discard packet");
			}
		}
		else {
			printf("cannot routing: discard packet\n");
		}
	}
	free(buf);
	return NULL;
}

bool Router::wrapSend(in_addr_t destIP, const char *msg, bool flag) {
	int id = findIt(destIP);
	if (id < 0) {
		printf("cannot routing\n");
		return true;
	}
	Rface *rf = it[id];
	in_addr_t src = it[id]->localIP;
	size_t len = strlen(msg) + sizeof(ip_t);
	char *packet = (char *)malloc(len);
	ip_t *ip = (ip_t *)packet;
	memset(packet, 0, sizeof(ip_t));
	memcpy(packet + sizeof(ip_t), msg, strlen(msg));
	ip->protocal = (flag) ? 0 : 200;
	ip->src = rf->localIP;
	ip->dest = destIP;

	bool result = node->sendp(id, packet, len);
	free(packet);
	return result;
}

bool Router::isDest(const in_addr_t dest) {
	int i = 0;
	int num = it.size();
	for (; i < num; i++) {
		if (it[i]->localIP == dest) {
			return true;
		}
	}
	return false;
}

int Router::findIt(in_addr_t dest, bool flag) {
	if (flag) {
		pthread_mutex_lock(&rtlock);
	}
	list<Entry *>::iterator pt = rt.begin();
	in_addr_t next = 0;
	for(; pt != rt.end(); pt++) {
		if ((*pt)->destIP == dest) {
			next = (*pt)->nextIP;
			break;
		}
	}

	if (pt == rt.end() || (*pt)->cost >= INFINITY) {
		printf("no such destination in routing table\n");
		if (flag) {
			pthread_mutex_unlock(&rtlock);
		}
		return -1;
	}


	if (flag) {
		pthread_mutex_unlock(&rtlock);
	}
	int inum = it.size();
	int i = 0;
	for (; i < inum; i++) {
		if (it[i]->nextIP == next) {
			break;
		}
	}
	if (i == inum) {
		printf("rt and it not match\n");
		return -1;
	}
	if (!it[i]->active) {
		printf("the interface is down\n");
		return -1;
	}
	return i;

}

int Router::findIt(in_addr_t dest) {
	return findIt(dest, true);	
}

bool Router::send(in_addr_t dest, string longMsg) {

	// printf("send '%s' to %u\n", longMsg.c_str(), dest);

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
		if (!wrapSend(dest, msg.c_str(), true)) {
			return false;
		}
	}
	return true;
}

bool Router::send(string d, string longMsg) {
	in_addr_t dest = inet_addr(d.c_str());
	return send(dest, longMsg);
}

void Router::printInterface() {
	int n = it.size();
	int i = 0;
	for (; i < n; i++) {
		printf("%d ", i + 1);
		cout<<ntoa({it[i]->localIP});
		if (it[i]->active) {
			cout<<" up"<<endl;
		}
		else {
			cout<<" down"<<endl;
		}
	}
}

void Router::printRoute() {

	pthread_mutex_lock(&rtlock);
	list<Entry *>::iterator pt = rt.begin();
	string tmp;
	int id = 0;
	for (; pt != rt.end(); pt++) {
		if ((*pt)->cost == 0 || (*pt)->cost >= INFINITY) {
			continue;
		}
		tmp = ntoa({(*pt)->destIP});
		cout<<tmp<<" ";
		id = findIt((*pt)->nextIP, false) + 1;
		cout<<id<<" ";
		cout<<(*pt)->cost<<endl;
	}
	pthread_mutex_unlock(&rtlock);
}









