#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/* Tat hoan toan IPv6 va uIP de su dung Rime stack */
#undef NETSTACK_CONF_WITH_IPV6
#define NETSTACK_CONF_WITH_IPV6 0

#undef UIP_CONF_IPV6
#define UIP_CONF_IPV6 0

/* Cau hinh ngan xep mang Rime tieu chuan */
#define NETSTACK_CONF_NETWORK     rime_driver
#define NETSTACK_CONF_MAC         csma_driver
#define NETSTACK_CONF_RDC         nullrdc_driver
#define NETSTACK_CONF_FRAMER      framer_802154

#endif /* PROJECT_CONF_H_ */


