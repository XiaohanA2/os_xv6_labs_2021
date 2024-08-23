#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// 声明两个锁
struct spinlock e1000_txlock;
struct spinlock e1000_rxlock;

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
  
  // 获取传输队列的锁，以确保传输操作的线程安全
  acquire(&e1000_txlock);

  // 获取当前传输描述符队列的尾部索引
  uint32 tail = regs[E1000_TDT];

  // 检查尾部描述符是否可用，通过检查描述符的 DD (Done) 位
  if(!(tx_ring[tail].status & E1000_TXD_STAT_DD)){
    // 如果描述符不可用，释放锁并返回 -1 表示传输失败
    release(&e1000_txlock);
    return -1;
  }

  // 如果当前描述符指向的 mbuf 不是空的，释放这个 mbuf
  if(tx_mbufs[tail])
    mbuffree(tx_mbufs[tail]);

  // 设置传输描述符的地址为当前 mbuf 的头部
  tx_ring[tail].addr = (uint64)m->head;

  // 设置传输描述符的长度为当前 mbuf 的长度
  tx_ring[tail].length = (uint16)m->len;

  // 设置传输命令，RS (Report Status) 和 EOP (End of Packet)
  tx_ring[tail].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;

  // 将当前 mbuf 指针存储在 tx_mbufs 数组中，以便后续处理
  tx_mbufs[tail] = m;

  // 更新传输描述符队列的尾部寄存器，通知网卡准备传输
  regs[E1000_TDT] = (tail + 1) % TX_RING_SIZE;

  // 释放传输队列的锁
  release(&e1000_txlock);

  // 返回 0 表示传输成功
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  struct mbuf *newmbuf;  // 用于存储新分配的缓冲区指针
  acquire(&e1000_rxlock);  // 获取接收环形缓冲区的锁，以确保接收操作的线程安全

  uint32 tail = regs[E1000_RDT];  // 获取当前接收描述符队列尾部索引（RDT寄存器值）
  uint32 curr = (tail + 1) % RX_RING_SIZE;  // 计算下一个要处理的接收描述符的索引

  while(1) {
    // 检查接收描述符的状态位，判断数据是否已经被接收到
    if(!(rx_ring[curr].status & E1000_RXD_STAT_DD)) {
      break;  // 如果没有接收到数据（DD位未设置），退出循环
    }

    // 设置接收缓冲区的长度为接收描述符中记录的实际数据长度
    rx_mbufs[curr]->len = rx_ring[curr].length;

    // 将接收到的数据包传递给上层网络栈处理
    net_rx(rx_mbufs[curr]);

    // 更新tail指针为当前描述符索引
    tail = curr;

    // 分配新的缓冲区用于接收下一个数据包
    newmbuf = mbufalloc(0);

    // 将新分配的缓冲区指针存储到rx_mbufs数组中，以便用于后续的接收操作
    rx_mbufs[curr] = newmbuf;

    // 更新接收描述符中的缓冲区地址
    rx_ring[curr].addr = (uint64)newmbuf->head;

    // 清除接收描述符的状态位，准备好接收新的数据
    rx_ring[curr].status = 0;

    // 更新接收描述符队列尾部寄存器为当前处理的描述符索引
    regs[E1000_RDT] = curr;

    // 计算下一个接收描述符的索引
    curr = (curr + 1) % RX_RING_SIZE;
  }

  // 最后，更新接收描述符队列的尾部指针，告知网卡哪些描述符已经处理完毕
  regs[E1000_RDT] = tail;

  release(&e1000_rxlock);  // 释放接收环形缓冲区的锁
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
