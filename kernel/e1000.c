#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"
#include <stddef.h>

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  
  acquire(&e1000_lock);
  uint tx_index=regs[E1000_TDT];
  struct tx_desc * desc=&tx_ring[tx_index];
  // 没有设置DD标志位即说明当前数据包还未完成，需要返回错误
  if((desc->status&E1000_TXD_STAT_DD)==0){
    goto error_case;
  }

  // 如果发送队列的尾部 tail指向的缓冲区不为空
  //  我们需要先释放该缓冲区
  if(tx_mbufs[tx_index]!=NULL){

    mbuffree(tx_mbufs[tx_index]);
    tx_mbufs[tx_index]=NULL;
  }
  
  //发送的数据设置为 要传输的数据包的首地址以及长度
  desc->addr=(uint64)m->head;
  desc->length=m->len;
  //设置 command的EOP和RS位
  // EOP:End of Packet 表明这个描述符是数据包的结尾
  // RS:report status ,表明该描述符指向的数据发送完成后会设置描述符的E1000_TXD_STAT_DD标志位
  desc->cmd= E1000_TXD_CMD_EOP |E1000_TXD_CMD_RS;


  //将发送的数据放入缓冲区后，环形队列的尾部+1
  tx_mbufs[tx_index]=m;
  regs[E1000_TDT]=(tx_index+1)%TX_RING_SIZE;

  release(&e1000_lock);
  return 0;
  
error_case:
  release(&e1000_lock);
  return -1;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  uint index;
  struct rx_desc * desc;

//一次中断应该把接收队列中的数据都读取出来，应该是循环
  while(1){
    //获取接收环形队列的尾部tail+1的索引
    // tail+1对应的数据包是第一个完成接收的数据包
    index=(regs[E1000_RDT]+1)%RX_RING_SIZE;

    desc = &rx_ring[index];
    //如果该描述符还未设置DD标志，说明网卡还没有接收完成该数据包
    //  说明已经把所有接收完成的数据包接收了，结束循环
    if((desc->status&E1000_RXD_STAT_DD)==0){
      return;
    }


    rx_mbufs[index]->len=desc->length;
    net_rx(rx_mbufs[index]);

    rx_mbufs[index]=mbufalloc(0);
    desc->addr=(uint64)rx_mbufs[index]->head;
    desc->status=0;
    regs[E1000_RDT]=index;
  }



}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}