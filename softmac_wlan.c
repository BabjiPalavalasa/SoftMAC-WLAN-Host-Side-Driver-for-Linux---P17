#include "softmac_wlan.h"

MODULE_AUTHOR("P. Babji");
MODULE_DESCRIPTION("SoftMAC Linux WLAN Host-Side Loopback Driver");
MODULE_LICENSE("GPL");

static bool pkt_log = false;
module_param(pkt_log, bool, 0644);
MODULE_PARM_DESC(pkt_log, "Enable per-packet TX/RX logging (impacts throughput)");

static struct platform_device *g_pdev = NULL;
static struct ieee80211_hw *g_softmac_hw = NULL;

static struct ieee80211_channel my_channels_2ghz[] = {
    { .band = NL80211_BAND_2GHZ, .center_freq = 2412, .hw_value = 1, .max_power = 20 },
    { .band = NL80211_BAND_2GHZ, .center_freq = 2437, .hw_value = 6, .max_power = 20 },
    { .band = NL80211_BAND_2GHZ, .center_freq = 2462, .hw_value = 11, .max_power = 20 },
};


static struct ieee80211_rate my_rates_2ghz[] = {
    { .bitrate = 10,  .hw_value = 0 },
    { .bitrate = 20,  .hw_value = 1 },
    { .bitrate = 55,  .hw_value = 2 },
    { .bitrate = 110, .hw_value = 3 },
    { .bitrate = 60,  .hw_value = 4 },
    { .bitrate = 120, .hw_value = 5 },
    { .bitrate = 240, .hw_value = 6 },
    { .bitrate = 540, .hw_value = 7 },
};

static struct ieee80211_supported_band my_band_2ghz;

static void softmac_rx_work(struct work_struct *work)
{
   unsigned long flags;
    struct softmac_priv *priv = container_of(work, struct softmac_priv, rx_work);
    struct sk_buff *skb;

    while ((skb = skb_dequeue(&priv->rx_queue)) != NULL) {
        struct ieee80211_rx_status *rx_status;
        struct ieee80211_hdr *hdr;

        if (!priv->is_running || !priv->vif) {
            dev_kfree_skb(skb);
            continue;
        }

        rx_status = IEEE80211_SKB_RXCB(skb);
        memset(rx_status, 0, sizeof(*rx_status));
        rx_status->freq = 2412;
        rx_status->band = NL80211_BAND_2GHZ;
        rx_status->signal = -50;
        rx_status->rate_idx = 0;

        hdr = (struct ieee80211_hdr *)skb->data;

        if (skb->len >= 24 && ieee80211_is_data(hdr->frame_control)) {
            unsigned int hdrlen = ieee80211_hdrlen(hdr->frame_control);

            
           if (ieee80211_is_nullfunc(hdr->frame_control)) 
           {
                 ieee80211_rx_ni(priv->hw, skb);
                 continue;
        }
        
            spin_lock_irqsave(&priv->lock, flags);
            priv->rx_packet_count++;
            priv->rx_bytes += skb->len;
            spin_unlock_irqrestore(&priv->lock, flags);

        
            ether_addr_copy(hdr->addr1, priv->vif->addr);
            ether_addr_copy(hdr->addr2, priv->vif->addr);
            hdr->addr2[5] ^= 0x01;

            
            hdr->frame_control &= ~cpu_to_le16(IEEE80211_FCTL_RETRY);
            hdr->seq_ctrl = cpu_to_le16((priv->rx_packet_count & 0x0FFF) << 4);

            
            if (skb->len >= hdrlen + 8 + sizeof(struct iphdr)) {
                u8 *llc = skb->data + hdrlen;

                if (llc[6] == 0x08 && llc[7] == 0x00) {
                    struct iphdr *iph = (struct iphdr *)(llc + 8);
                    unsigned int ip_hdr_len = iph->ihl * 4;
                    __be32 orig_saddr = iph->saddr;

                    iph->saddr = iph->daddr;
                    iph->daddr = orig_saddr;
                    iph->check = 0;
                    iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

                    if (iph->protocol == IPPROTO_UDP &&
                        skb->len >= hdrlen + 8 + ip_hdr_len + sizeof(struct udphdr)) {
                        struct udphdr *udph = (struct udphdr *)((u8 *)iph + ip_hdr_len);
                        int payload_len = ntohs(udph->len) - (int)sizeof(struct udphdr);
                        unsigned char *payload = (unsigned char *)udph + sizeof(struct udphdr);
                        int max_avail = (int)(skb->len - (payload - skb->data));

                        udph->check = 0;

                        if (pkt_log && payload_len > 0 && payload_len <= max_avail) 
                        {
                                int print_len = payload_len > 64 ? 64 : payload_len;
                                pr_info("%s: [RX] Count=%lu | Len=%d | Payload='%.*s'\n",
                                    DRIVER_NAME, priv->rx_packet_count,payload_len, print_len, payload);
                        }
                    }
                }
            }

            skb->ip_summed = CHECKSUM_UNNECESSARY;
            ieee80211_rx_ni(priv->hw, skb);
        } else {
            dev_kfree_skb(skb);
        }
    }
}

static enum hrtimer_restart softmac_throughput_timer_cb(struct hrtimer *timer)
{
    struct softmac_priv *priv = container_of(timer, struct softmac_priv,
                                             throughput_timer);
    unsigned long flags;
    unsigned long tx_b, rx_b;
    unsigned long tx_bps, rx_bps;

    spin_lock_irqsave(&priv->lock, flags);
    if (!priv->is_running) {
        spin_unlock_irqrestore(&priv->lock, flags);
        return HRTIMER_NORESTART;
    }

    tx_b = priv->tx_bytes;
    rx_b = priv->rx_bytes;
    priv->tx_bytes = 0;
    priv->rx_bytes = 0;
    spin_unlock_irqrestore(&priv->lock, flags);

    if (tx_b > 0 || rx_b > 0) {
        tx_bps = tx_b * 8;
        rx_bps = rx_b * 8;

        pr_info("%s: Throughput | TX: %lu bps (%lu Kbps, %lu Mbps) | RX: %lu bps (%lu Kbps, %lu Mbps)\n",
                DRIVER_NAME,
                tx_bps, tx_bps / 1000, tx_bps / 1000000,
                rx_bps, rx_bps / 1000, rx_bps / 1000000);
    }

    hrtimer_forward_now(timer, ktime_set(1, 0));
    return HRTIMER_RESTART;
}

static int softmac_start(struct ieee80211_hw *hw)
{
    struct softmac_priv *priv = hw->priv;
    unsigned long flags;

    spin_lock_irqsave(&priv->lock, flags);
    priv->is_running = true;
    priv->tx_bytes = 0;
    priv->rx_bytes = 0;
    spin_unlock_irqrestore(&priv->lock, flags);

    hrtimer_start(&priv->throughput_timer, ktime_set(1, 0), HRTIMER_MODE_REL);

    pr_info("%s: Interface started successfully (Carrier UP)\n", DRIVER_NAME);
    return 0;
}

static void softmac_stop(struct ieee80211_hw *hw, bool reassoc)
{
    struct softmac_priv *priv = hw->priv;
    unsigned long flags;

    spin_lock_irqsave(&priv->lock, flags);
    priv->is_running = false;
    priv->ibss_active = false;
    spin_unlock_irqrestore(&priv->lock, flags);

    cancel_delayed_work_sync(&priv->peer_keepalive_work);
    hrtimer_cancel(&priv->throughput_timer);
    cancel_work_sync(&priv->rx_work);
    skb_queue_purge(&priv->rx_queue);

    pr_info("%s: Interface stopped\n", DRIVER_NAME);
}

static int softmac_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
    struct softmac_priv *priv = hw->priv;
    unsigned long flags;

    pr_info("%s: Adding virtual interface of type: %d (MAC: %pM)\n",
            DRIVER_NAME, vif->type, vif->addr);

    if (vif->type != NL80211_IFTYPE_MONITOR) {
        spin_lock_irqsave(&priv->lock, flags);
        priv->vif = vif;
        spin_unlock_irqrestore(&priv->lock, flags);
    }

    return 0;
}

static void softmac_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
    struct softmac_priv *priv = hw->priv;
    unsigned long flags;

    pr_info("%s: Removing virtual interface\n", DRIVER_NAME);

    spin_lock_irqsave(&priv->lock, flags);
    if (priv->vif == vif) {
        priv->vif = NULL;
    }
    spin_unlock_irqrestore(&priv->lock, flags);
}

static void softmac_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *tx_control,
                       struct sk_buff *skb)
{
    unsigned long flags;
    struct softmac_priv *priv = hw->priv;
    struct sk_buff *rx_skb;
    struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

    if (skb_queue_len(&priv->rx_queue) < 1000) 
    {
        rx_skb = dev_alloc_skb(skb->len);
        if (rx_skb) 
        {
            if (skb_copy_bits(skb, 0, skb_put(rx_skb, skb->len), skb->len) == 0) 
            {
                struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx_skb->data;

                if (rx_skb->len >= 24 && ieee80211_is_data(hdr->frame_control) &&
                    !is_multicast_ether_addr(hdr->addr1)) 
                {
                    spin_lock_irqsave(&priv->lock, flags);
                    priv->tx_packet_count++;
                    priv->tx_bytes += rx_skb->len;
                    spin_unlock_irqrestore(&priv->lock, flags);

                    unsigned int hdrlen = ieee80211_hdrlen(hdr->frame_control);

                    if (rx_skb->len >= hdrlen + 8 + sizeof(struct iphdr)) 
                    {
                        u8 *llc = rx_skb->data + hdrlen;

                        if (llc[6] == 0x08 && llc[7] == 0x00) 
                        {
                            struct iphdr *iph = (struct iphdr *)(llc + 8);
                            unsigned int ip_hdr_len = iph->ihl * 4;

                            if (iph->protocol == IPPROTO_UDP &&
                                rx_skb->len >= hdrlen + 8 + ip_hdr_len + sizeof(struct udphdr)) 
                            {
                                struct udphdr *udph = (struct udphdr *)((u8 *)iph + ip_hdr_len);
                                int payload_len = ntohs(udph->len) - (int)sizeof(struct udphdr);
                                unsigned char *payload = (unsigned char *)udph + sizeof(struct udphdr);
                                int max_avail = (int)(rx_skb->len - (payload - rx_skb->data));

                         if (pkt_log && payload_len > 0 && payload_len <= max_avail) 
                                {
                                  int print_len = payload_len > 64 ? 64 : payload_len;
                                    pr_info("%s: [TX] Count=%lu | Len=%d | Payload='%.*s'\n",
                                   DRIVER_NAME, priv->tx_packet_count,payload_len, print_len, payload);
                                }
                            }
                        }
                    }

                    skb_queue_tail(&priv->rx_queue, rx_skb);
                    schedule_work(&priv->rx_work);
                } 
                else 
                {
                    dev_kfree_skb(rx_skb);
                }
            } 
              else 
            {
                dev_kfree_skb(rx_skb);
            }
        }
    }

    ieee80211_tx_info_clear_status(info);
    info->flags |= IEEE80211_TX_STAT_ACK;
    ieee80211_tx_status_ni(hw, skb);
}

static int my_config(struct ieee80211_hw *hw, int radio_idx, u32 changed_flags) { return 0; }

static void my_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
                                unsigned int *total_flags, u64 multicast) {
    *total_flags = 0;
}

static void softmac_peer_keepalive_work(struct work_struct *work)
{
    struct softmac_priv *priv = container_of(work, struct softmac_priv,
                                             peer_keepalive_work.work);
    
    struct sk_buff *skb;
    struct ieee80211_hdr_3addr *hdr;
    u8 vif_addr[ETH_ALEN], bssid[ETH_ALEN];
    unsigned long flags;
    bool active;

    spin_lock_irqsave(&priv->lock, flags);
    active = priv->is_running && priv->ibss_active && priv->vif &&
             priv->vif->type == NL80211_IFTYPE_ADHOC;
    if (active) {
        ether_addr_copy(vif_addr, priv->vif->addr);
        ether_addr_copy(bssid, priv->ibss_bssid);
    }
    spin_unlock_irqrestore(&priv->lock, flags);

    if (!active)
        return;

    skb = dev_alloc_skb(sizeof(*hdr));
    if (skb) {
        hdr = skb_put_zero(skb, sizeof(*hdr));
        hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC);
        ether_addr_copy(hdr->addr1, vif_addr);
        ether_addr_copy(hdr->addr2, vif_addr);
        hdr->addr2[5] ^= 0x01;
        ether_addr_copy(hdr->addr3, bssid);
        priv->keepalive_seq = (priv->keepalive_seq + 1) & 0x0FFF;
        hdr->seq_ctrl = cpu_to_le16(priv->keepalive_seq << 4);

        skb_queue_tail(&priv->rx_queue, skb);
        schedule_work(&priv->rx_work);
    }

    schedule_delayed_work(&priv->peer_keepalive_work, msecs_to_jiffies(20000));
}

static void my_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                                struct ieee80211_bss_conf *bss_conf, u64 changed)
{
    struct softmac_priv *priv = hw->priv;
    unsigned long flags;

    if (changed & BSS_CHANGED_ASSOC) {
        pr_info("%s: VIF (%d) joined cell successfully.\n", DRIVER_NAME, vif->type);
    }

    if (vif->type == NL80211_IFTYPE_ADHOC && (changed & BSS_CHANGED_BSSID)) {
        if (is_valid_ether_addr(bss_conf->bssid)) {
            spin_lock_irqsave(&priv->lock, flags);
            ether_addr_copy(priv->ibss_bssid, bss_conf->bssid);
            priv->ibss_active = true;
            spin_unlock_irqrestore(&priv->lock, flags);

            pr_info("%s: IBSS joined (BSSID=%pM), keeping loopback peer alive.\n",
                    DRIVER_NAME, bss_conf->bssid);
            mod_delayed_work(system_wq, &priv->peer_keepalive_work, 0);
        } else {
            spin_lock_irqsave(&priv->lock, flags);
            priv->ibss_active = false;
            spin_unlock_irqrestore(&priv->lock, flags);
            cancel_delayed_work(&priv->peer_keepalive_work);
        }
    }
}

static const struct ieee80211_ops softmac_ops = {
    .start                = softmac_start,
    .stop                 = softmac_stop,
    .add_interface        = softmac_add_interface,
    .remove_interface     = softmac_remove_interface,
    .tx                   = softmac_tx,
    .config               = my_config,
    .configure_filter     = my_configure_filter,
    .bss_info_changed     = my_bss_info_changed,
    .wake_tx_queue        = ieee80211_handle_wake_tx_queue,
    .add_chanctx          = ieee80211_emulate_add_chanctx,
    .remove_chanctx       = ieee80211_emulate_remove_chanctx,
    .change_chanctx       = ieee80211_emulate_change_chanctx,
    .switch_vif_chanctx   = ieee80211_emulate_switch_vif_chanctx,
};

static int __init softmac_init(void)
{
    struct ieee80211_hw *hw; 
    struct softmac_priv *priv; 
    int ret;
    u8 dummy_mac[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 }; 
    u8 addr_mask[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f };

    g_pdev = platform_device_register_simple("softmac_wlan", -1, NULL, 0);
    if (IS_ERR(g_pdev)) return PTR_ERR(g_pdev);

    hw = ieee80211_alloc_hw(sizeof(struct softmac_priv), &softmac_ops);
    if (!hw) {
        platform_device_unregister(g_pdev);
        return -ENOMEM;
    }
    g_softmac_hw = hw; 

    priv = hw->priv;
    priv->hw = hw;
    priv->is_running = false;
    priv->tx_packet_count = 0;
    priv->rx_packet_count = 0;
    spin_lock_init(&priv->lock);

    skb_queue_head_init(&priv->rx_queue); 
    INIT_WORK(&priv->rx_work, softmac_rx_work); 

     priv->ibss_active = false;
     priv->keepalive_seq = 0;
     INIT_DELAYED_WORK(&priv->peer_keepalive_work, softmac_peer_keepalive_work);
     
     priv->tx_bytes = 0;
     priv->rx_bytes = 0;
     hrtimer_setup(&priv->throughput_timer, softmac_throughput_timer_cb,
              CLOCK_MONOTONIC, HRTIMER_MODE_REL);

    SET_IEEE80211_DEV(hw, &g_pdev->dev); 

    hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
                                 BIT(NL80211_IFTYPE_AP) |
                                 BIT(NL80211_IFTYPE_MONITOR) |
                                 BIT(NL80211_IFTYPE_ADHOC);

    hw->wiphy->max_scan_ssids = 1; 
    hw->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM; 
    
    SET_IEEE80211_PERM_ADDR(hw, dummy_mac);
    ether_addr_copy(hw->wiphy->addr_mask, addr_mask);

    memset(&my_band_2ghz, 0, sizeof(my_band_2ghz));
    my_band_2ghz.band = NL80211_BAND_2GHZ;
    my_band_2ghz.channels = my_channels_2ghz;
    my_band_2ghz.n_channels = ARRAY_SIZE(my_channels_2ghz);
    my_band_2ghz.bitrates = my_rates_2ghz;
    my_band_2ghz.n_bitrates = ARRAY_SIZE(my_rates_2ghz);

    hw->wiphy->bands[NL80211_BAND_2GHZ] = &my_band_2ghz;

    ieee80211_hw_set(hw, SIGNAL_DBM);


    ret = ieee80211_register_hw(hw);
    if (ret) {
        ieee80211_free_hw(hw);
        platform_device_unregister(g_pdev);
        g_softmac_hw = NULL;
        g_pdev = NULL;
        return ret;
    }

    pr_info("%s: SoftMAC driver successfully loaded and registered.\n", DRIVER_NAME);
    return 0;
}

static void __exit softmac_exit(void)
{
    struct softmac_priv *priv;

    pr_info("%s: Unloading SoftMAC WLAN driver module...\n", DRIVER_NAME);

    if (g_softmac_hw) {
        priv = g_softmac_hw->priv;

        ieee80211_unregister_hw(g_softmac_hw);

        cancel_delayed_work_sync(&priv->peer_keepalive_work);
        hrtimer_cancel(&priv->throughput_timer);
        cancel_work_sync(&priv->rx_work);
        skb_queue_purge(&priv->rx_queue);

        ieee80211_free_hw(g_softmac_hw);
        g_softmac_hw = NULL;
    }

    if (g_pdev) {
        platform_device_unregister(g_pdev);
        g_pdev = NULL;
    }

    pr_info("%s: SoftMAC driver unloaded successfully.\n", DRIVER_NAME);
}

module_init(softmac_init);
module_exit(softmac_exit);