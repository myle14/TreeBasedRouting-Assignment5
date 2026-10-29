#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/leds.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "symbols.h"
#include <stdio.h>
#include <string.h>

/* So luong neighbor toi da luu trong bang */
#define MAX_NEIGHBORS 16

/* Chu ky phat beacon dinh tuyen (10 giay) */
#define BEACON_INTERVAL (10 * CLOCK_SECOND)

/* Chu ky gui du lieu ve Sink (15 giay) */
#define DATA_INTERVAL   (15 * CLOCK_SECOND)

/* Dinh nghia cac loai goi tin */
enum {
  PACKET_TYPE_BEACON = 1,
  PACKET_TYPE_DATA   = 2
};

/* Cau truc goi tin Beacon */
struct beacon_msg {
  uint8_t type;
  linkaddr_t sender;
  uint8_t hop_count;
  uint16_t seqno; /* So thu tu goi tin de tinh link-PRR */
};

/* Cau truc goi tin Du lieu */
struct data_msg {
  uint8_t type;
  linkaddr_t origin;     /* Node nguon tao du lieu */
  linkaddr_t final_dest; /* Node dich (Root/Sink) */
  linkaddr_t next_hop;   /* Node cha BNN tiep theo */
  uint16_t seqno;
  uint16_t reading_val;  /* Du lieu cam bien */
};

/* Cau truc mot phan tu trong Neighbor Table */
struct neighbor_entry {
  struct neighbor_entry *next;
  linkaddr_t addr;
  uint8_t hop_count;
  int8_t rssi;
  uint16_t last_seqno;
  uint16_t received_packets;
  uint16_t total_expected;
  uint8_t prr;           /* Ty le nhan thanh cong link-PRR (0 - 100%) */
  clock_time_t last_seen;
};

MEMB(neighbor_mem, struct neighbor_entry, MAX_NEIGHBORS);
LIST(neighbor_table);

static linkaddr_t current_parent;
static uint8_t my_hop_count = 255;
static uint8_t is_root = 0;
static uint16_t beacon_seqno_counter = 0;
static uint16_t data_seqno_counter = 0;

static struct broadcast_conn bc_conn;
static struct unicast_conn uc_conn;

PROCESS(tree_routing_process, "Tree Routing Process");
PROCESS(data_traffic_process, "Data Traffic Sender Process");
AUTOSTART_PROCESSES(&tree_routing_process, &data_traffic_process);

/* Cap nhat thong tin neighbor va tinh toan link-PRR */
static void update_neighbor(const linkaddr_t *from, uint8_t hop, int8_t rssi, uint16_t seqno) {
  struct neighbor_entry *n = NULL;

  for(n = list_head(neighbor_table); n != NULL; n = n->next) {
    if(linkaddr_cmp(&n->addr, from)) {
      break;
    }
  }

  if(n == NULL) {
    n = memb_alloc(&neighbor_mem);
    if(n == NULL) {
      n = list_pop(neighbor_table);
    }
    if(n != NULL) {
      linkaddr_copy(&n->addr, from);
      n->hop_count = hop;
      n->rssi = rssi;
      n->last_seqno = seqno;
      n->received_packets = 1;
      n->total_expected = 1;
      n->prr = 100;
      n->last_seen = clock_time();
      list_add(neighbor_table, n);
    }
  } else {
    n->hop_count = hop;
    n->rssi = rssi;
    n->last_seen = clock_time();

    /* Tinh toan PRR dua tren khoang cach seqno giua cac goi nhan duoc */
    if(seqno > n->last_seqno) {
      uint16_t diff = seqno - n->last_seqno;
      n->total_expected += diff;
      n->received_packets++;
      n->last_seqno = seqno;
      n->prr = (uint8_t)((n->received_packets * 100) / n->total_expected);
    } else if(seqno == n->last_seqno) {
      /* Bo qua goi trung lap */
    } else {
      n->last_seqno = seqno;
      n->received_packets++;
      n->total_expected++;
    }
  }
}

/* Thuat toan chon BNN dua tren Hop count va link-PRR tot nhat */
static void select_bnn_prr(void) {
  if(is_root) return;

  struct neighbor_entry *n = NULL;
  struct neighbor_entry *best = NULL;
  uint8_t min_hop = 255;
  uint8_t best_prr = 0;

  /* Tim muc hop count nho nhat de tranh tao vong lap */
  for(n = list_head(neighbor_table); n != NULL; n = n->next) {
    if(n->hop_count < min_hop) {
      min_hop = n->hop_count;
    }
  }

  /* Trong so cac node co min_hop, chon node co link-PRR cao nhat */
  for(n = list_head(neighbor_table); n != NULL; n = n->next) {
    if(n->hop_count == min_hop) {
      if(best == NULL || n->prr > best_prr) {
        best = n;
        best_prr = n->prr;
      }
    }
  }

  if(best != NULL) {
    linkaddr_copy(&current_parent, &best->addr);
    my_hop_count = best->hop_count + 1;
    printf("[ROUTING] Chon BNN: %d.%d | Hop: %u | PRR: %u%% | RSSI: %d dBm\n",
           current_parent.u8[0], current_parent.u8[1],
           my_hop_count, best->prr, best->rssi);
  }
}

/* Callback nhan goi broadcast beacon */
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
  struct beacon_msg msg;
  if(packetbuf_datalen() != sizeof(struct beacon_msg)) {
    return;
  }
  memcpy(&msg, packetbuf_dataptr(), sizeof(struct beacon_msg));

  int8_t rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
  update_neighbor(from, msg.hop_count, rssi, msg.seqno);
  select_bnn_prr();
}

static const struct broadcast_callbacks bc_call = {broadcast_recv};

/* Callback nhan unicast: Sink tiep nhan hoac Node trung gian forward */
static void unicast_recv(struct unicast_conn *c, const linkaddr_t *from) {
  struct data_msg msg;
  if(packetbuf_datalen() != sizeof(struct data_msg)) {
    return;
  }
  memcpy(&msg, packetbuf_dataptr(), sizeof(struct data_msg));

  if(is_root) {
    printf("[SINK RECEIVED] Tu node %d.%d | Seq: %u | Gia tri: %u\n",
           msg.origin.u8[0], msg.origin.u8[1], msg.seqno, msg.reading_val);
  } else {
    /* Chuyen tiep goi tin len BNN da chon */
    if(!linkaddr_cmp(&current_parent, &linkaddr_null)) {
      linkaddr_copy(&msg.next_hop, &current_parent);
      packetbuf_copyfrom(&msg, sizeof(struct data_msg));
      unicast_send(&uc_conn, &current_parent);
      printf("[FORWARD] Chuyen tiep goi cua %d.%d len BNN %d.%d\n",
             msg.origin.u8[0], msg.origin.u8[1],
             current_parent.u8[0], current_parent.u8[1]);
    }
  }
}

static const struct unicast_callbacks uc_call = {unicast_recv, NULL};

/* Thread 1: Khoi tao va duy tri bang phat Beacon dinh ky */
PROCESS_THREAD(tree_routing_process, ev, data) {
  static struct etimer beacon_timer;

  PROCESS_EXITHANDLER(broadcast_close(&bc_conn); unicast_close(&uc_conn);)
  PROCESS_BEGIN();

  memb_init(&neighbor_mem);
  list_init(neighbor_table);
  broadcast_open(&bc_conn, 129, &bc_call);
  unicast_open(&uc_conn, 146, &uc_call);

  /* Thiet lap mac dinh: Node 1 dong vai tro Sink/Root */
  if(linkaddr_node_addr.u8[0] == 1 && linkaddr_node_addr.u8[1] == 0) {
    is_root = 1;
    my_hop_count = 0;
    linkaddr_copy(&current_parent, &linkaddr_node_addr);
    printf("[SYSTEM] Node 1 khoi tao la Root (Sink)\n");
  } else {
    is_root = 0;
    my_hop_count = 255;
    linkaddr_copy(&current_parent, &linkaddr_null);
  }

  while(1) {
    etimer_set(&beacon_timer, BEACON_INTERVAL + (random_rand() % (CLOCK_SECOND * 2)));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&beacon_timer));

    if(is_root || my_hop_count < 255) {
      struct beacon_msg b_msg;
      b_msg.type = PACKET_TYPE_BEACON;
      linkaddr_copy(&b_msg.sender, &linkaddr_node_addr);
      b_msg.hop_count = my_hop_count;
      b_msg.seqno = ++beacon_seqno_counter;

      packetbuf_copyfrom(&b_msg, sizeof(struct beacon_msg));
      broadcast_send(&bc_conn);
    }
  }

  PROCESS_END();
}

/* Thread 2: Gui du lieu ve Root thong qua BNN */
PROCESS_THREAD(data_traffic_process, ev, data) {
  static struct etimer data_timer;

  PROCESS_BEGIN();

  /* Cho 25 giay de mang on dinh topology truoc khi bat dau truyen data */
  etimer_set(&data_timer, CLOCK_SECOND * 25);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&data_timer));

  while(1) {
    etimer_set(&data_timer, DATA_INTERVAL + (random_rand() % (CLOCK_SECOND * 3)));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&data_timer));

    if(!is_root && !linkaddr_cmp(&current_parent, &linkaddr_null)) {
      struct data_msg d_msg;
      d_msg.type = PACKET_TYPE_DATA;
      linkaddr_copy(&d_msg.origin, &linkaddr_node_addr);
      d_msg.final_dest.u8[0] = 1;
      d_msg.final_dest.u8[1] = 0;
      linkaddr_copy(&d_msg.next_hop, &current_parent);
      d_msg.seqno = ++data_seqno_counter;
      d_msg.reading_val = (uint16_t)(random_rand() % 100);

      packetbuf_copyfrom(&d_msg, sizeof(struct data_msg));
      unicast_send(&uc_conn, &current_parent);
      printf("[DATA SEND] Gui data seq=%u len BNN %d.%d\n",
             d_msg.seqno, current_parent.u8[0], current_parent.u8[1]);
    }
  }

  PROCESS_END();
}


