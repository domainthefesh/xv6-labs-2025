#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

#define MAX_UDP_QUEUE 16
#define MAX_BOUND_PORTS 32

struct udp_packet {
  char *buf;          // e1000 收到的整张 packet page
  int len;            // 整个 ethernet frame 长度

  uint32 src;         // host byte order
  uint16 sport;       // host byte order

  int payload_off;
  int payload_len;
};

struct port_queue {
  int used;
  uint16 port;

  struct udp_packet packets[MAX_UDP_QUEUE];

  int head;
  int tail;
  int count;
};

static struct port_queue ports[MAX_BOUND_PORTS];

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

void
netinit(void)
{
  initlock(&netlock, "netlock");
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
 int port;

  argint(0, &port);

  if(port < 0 || port > 65535)
    return -1;

  acquire(&netlock);

  for(int i = 0; i < MAX_BOUND_PORTS; i++){
    if(ports[i].used && ports[i].port == port){
      release(&netlock);
      return -1;
    }
  }

  for(int i = 0; i < MAX_BOUND_PORTS; i++){
    if(!ports[i].used){
      ports[i].used = 1;
      ports[i].port = port;
      ports[i].head = 0;
      ports[i].tail = 0;
      ports[i].count = 0;

      release(&netlock);
      return 0;
    }
  }

  release(&netlock);
  return -1;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  int port;

  argint(0, &port);

  if(port < 0 || port > 65535)
    return -1;

  acquire(&netlock);

  struct port_queue *q = 0;

  for(int i = 0; i < MAX_BOUND_PORTS; i++){
    if(ports[i].used && ports[i].port == port){
      q = &ports[i];
      break;
    }
  }

  if(q == 0){
    release(&netlock);
    return -1;
  }

  /*
   * 先标记为 unbound。
   * 正在 sys_recv() 里 sleep 的进程醒来后会看到 used == 0。
   */
  q->used = 0;

  // 把队列中还没人 recv 的 packet 全部释放
  while(q->count > 0){
    struct udp_packet *pkt = &q->packets[q->head];

    if(pkt->buf != 0){
      kfree(pkt->buf);
      pkt->buf = 0;
    }

    q->head = (q->head + 1) % MAX_UDP_QUEUE;
    q->count--;
  }

  q->head = 0;
  q->tail = 0;
  q->port = 0;

  // recv() 可能正在睡
  wakeup(q);

  release(&netlock);

  return 0;

}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
   //
  // Your code here.
  //
  int dport;
  uint64 srcaddr;
  uint64 sportaddr;
  uint64 bufaddr;
  int maxlen;

  argint(0, &dport);
  argaddr(1, &srcaddr);
  argaddr(2, &sportaddr);
  argaddr(3, &bufaddr);
  argint(4, &maxlen);

  if(dport < 0 || dport > 65535 || maxlen < 0)
    return -1;

  acquire(&netlock);

  struct port_queue *q = 0;

  for(int i = 0; i < MAX_BOUND_PORTS; i++){
    if(ports[i].used && ports[i].port == dport){
      q = &ports[i];
      break;
    }
  }

  // recv() 要求这个 port 之前已经 bind
  if(q == 0){
    release(&netlock);
    return -1;
  }

  /*
   * 没有 packet 就睡眠。
   *
   * 必须使用 while，而不是 if，因为醒来之后
   * 必须重新确认 queue 中真的有 packet。
   */
  while(q->count == 0 && q->used){
    sleep(q, &netlock);
  }

  // 可能是在 unbind() 后被叫醒的
  if(!q->used){
    release(&netlock);
    return -1;
  }

  // FIFO：从 head 取最早到达的 packet
  struct udp_packet pkt = q->packets[q->head];

  q->head = (q->head + 1) % MAX_UDP_QUEUE;
  q->count--;

  release(&netlock);

  int n = pkt.payload_len;
  if(n > maxlen)
    n = maxlen;

  struct proc *p = myproc();

  // 返回 source IP
  if(copyout(p->pagetable,
             srcaddr,
             (char *)&pkt.src,
             sizeof(pkt.src)) < 0){
    kfree(pkt.buf);
    return -1;
  }

  // 返回 source UDP port
  if(copyout(p->pagetable,
             sportaddr,
             (char *)&pkt.sport,
             sizeof(pkt.sport)) < 0){
    kfree(pkt.buf);
    return -1;
  }

  // 返回 UDP payload
  if(copyout(p->pagetable,
             bufaddr,
             pkt.buf + pkt.payload_off,
             n) < 0){
    kfree(pkt.buf);
    return -1;
  }

  kfree(pkt.buf);

  return n;
}


// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  if(e1000_transmit(buf, total) < 0){
  kfree(buf);
  return -1;
}

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  //Your code here.
  //
  if(len < sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp)){
    kfree(buf);
    return;
  }

  struct eth *eth = (struct eth *)buf;
  struct ip *ip = (struct ip *)(eth + 1);

  // Part 2 只接收 UDP
  if(ip->ip_p != IPPROTO_UDP){
    kfree(buf);
    return;
  }

  struct udp *udp = (struct udp *)(ip + 1);

  uint16 dport = ntohs(udp->dport);
  uint16 sport = ntohs(udp->sport);
  uint32 src = ntohl(ip->ip_src);

  int ulen = ntohs(udp->ulen);

  // UDP length 包括 UDP header 自己
  if(ulen < sizeof(struct udp) ||
     sizeof(struct eth) + sizeof(struct ip) + ulen > len){
    kfree(buf);
    return;
  }

  int payload_len = ulen - sizeof(struct udp);
  int payload_off = (char *)(udp + 1) - buf;

  acquire(&netlock);

  struct port_queue *q = 0;

  // 找 destination port 对应的 queue
  for(int i = 0; i < MAX_BOUND_PORTS; i++){
    if(ports[i].used && ports[i].port == dport){
      q = &ports[i];
      break;
    }
  }

  // 没有人 bind 这个 port
  if(q == 0){
    release(&netlock);
    kfree(buf);
    return;
  }

  // 这个 port 已经缓存了 16 个 packet
  if(q->count == MAX_UDP_QUEUE){
    release(&netlock);
    kfree(buf);
    return;
  }

  // enqueue 到 tail
  struct udp_packet *pkt = &q->packets[q->tail];

  pkt->buf = buf;
  pkt->len = len;
  pkt->src = src;
  pkt->sport = sport;
  pkt->payload_off = payload_off;
  pkt->payload_len = payload_len;

  q->tail = (q->tail + 1) % MAX_UDP_QUEUE;
  q->count++;

  // 如果 recv() 正睡在这个 queue 上，把它叫醒
  wakeup(q);

  release(&netlock);
}


//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
