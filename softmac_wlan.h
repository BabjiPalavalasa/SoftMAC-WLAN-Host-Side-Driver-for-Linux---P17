#ifndef _SOFTMAC_WLAN_H_
#define _SOFTMAC_WLAN_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h> 
#include <linux/platform_device.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/etherdevice.h>
#define DRIVER_NAME "softmac_wlan"

struct softmac_priv {
    struct ieee80211_hw *hw;
    struct net_device *netdev;
    struct ieee80211_vif *vif;
    
    bool is_running;
    unsigned long tx_packet_count;
    unsigned long rx_packet_count;
    
    spinlock_t lock;
    struct sk_buff_head rx_queue; 
    struct work_struct rx_work;

    struct delayed_work peer_keepalive_work;
    u8 ibss_bssid[ETH_ALEN];
    bool ibss_active;
    u16 keepalive_seq;

    struct delayed_work throughput_work;
    unsigned long tx_bytes;
    unsigned long rx_bytes;

    ktime_t last_tp_time;
};
#endif