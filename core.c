// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include <linux/bitfield.h>
#include <linux/version.h>

#include "core.h"
#include "txrx.h"
#include "debug.h"
#include "mac.h"
#include "fw.h"
#include "efuse.h"
#include "phy.h"
#include "reg.h"
#include "ser.h"
#include "coex.h"

static struct ieee80211_channel rtw89_channels_2ghz[] = {
	{ .center_freq = 2412, .hw_value = 1, },
	{ .center_freq = 2417, .hw_value = 2, },
	{ .center_freq = 2422, .hw_value = 3, },
	{ .center_freq = 2427, .hw_value = 4, },
	{ .center_freq = 2432, .hw_value = 5, },
	{ .center_freq = 2437, .hw_value = 6, },
	{ .center_freq = 2442, .hw_value = 7, },
	{ .center_freq = 2447, .hw_value = 8, },
	{ .center_freq = 2452, .hw_value = 9, },
	{ .center_freq = 2457, .hw_value = 10, },
	{ .center_freq = 2462, .hw_value = 11, },
	{ .center_freq = 2467, .hw_value = 12, },
	{ .center_freq = 2472, .hw_value = 13, },
	{ .center_freq = 2484, .hw_value = 14, },
};

static struct ieee80211_channel rtw89_channels_5ghz[] = {
	{.center_freq = 5180, .hw_value = 36,},
	{.center_freq = 5200, .hw_value = 40,},
	{.center_freq = 5220, .hw_value = 44,},
	{.center_freq = 5240, .hw_value = 48,},
	{.center_freq = 5260, .hw_value = 52,},
	{.center_freq = 5280, .hw_value = 56,},
	{.center_freq = 5300, .hw_value = 60,},
	{.center_freq = 5320, .hw_value = 64,},
	{.center_freq = 5500, .hw_value = 100,},
	{.center_freq = 5520, .hw_value = 104,},
	{.center_freq = 5540, .hw_value = 108,},
	{.center_freq = 5560, .hw_value = 112,},
	{.center_freq = 5580, .hw_value = 116,},
	{.center_freq = 5600, .hw_value = 120,},
	{.center_freq = 5620, .hw_value = 124,},
	{.center_freq = 5640, .hw_value = 128,},
	{.center_freq = 5660, .hw_value = 132,},
	{.center_freq = 5680, .hw_value = 136,},
	{.center_freq = 5700, .hw_value = 140,},
	{.center_freq = 5720, .hw_value = 144,},
	{.center_freq = 5745, .hw_value = 149,},
	{.center_freq = 5765, .hw_value = 153,},
	{.center_freq = 5785, .hw_value = 157,},
	{.center_freq = 5805, .hw_value = 161,},
	{.center_freq = 5825, .hw_value = 165,
	 .flags = IEEE80211_CHAN_NO_HT40MINUS},
};

static struct ieee80211_rate rtw89_bitrates[] = {
	{ .bitrate = 10,  .hw_value = 0x00, },
	{ .bitrate = 20,  .hw_value = 0x01, },
	{ .bitrate = 55,  .hw_value = 0x02, },
	{ .bitrate = 110, .hw_value = 0x03, },
	{ .bitrate = 60,  .hw_value = 0x04, },
	{ .bitrate = 90,  .hw_value = 0x05, },
	{ .bitrate = 120, .hw_value = 0x06, },
	{ .bitrate = 180, .hw_value = 0x07, },
	{ .bitrate = 240, .hw_value = 0x08, },
	{ .bitrate = 360, .hw_value = 0x09, },
	{ .bitrate = 480, .hw_value = 0x0a, },
	{ .bitrate = 540, .hw_value = 0x0b, },
};

u16 rtw89_ra_report_to_bitrate(struct rtw89_dev *rtwdev, u8 rpt_rate)
{
	struct ieee80211_rate rate;

	if (unlikely(rpt_rate >= ARRAY_SIZE(rtw89_bitrates))) {
		rtw89_info(rtwdev, "invalid rpt rate %d\n", rpt_rate);
		return 0;
	}

	rate = rtw89_bitrates[rpt_rate];

	return rate.bitrate;
}

static struct ieee80211_supported_band rtw89_sband_2ghz = {
	.band		= NL80211_BAND_2GHZ,
	.channels	= rtw89_channels_2ghz,
	.n_channels	= ARRAY_SIZE(rtw89_channels_2ghz),
	.bitrates	= rtw89_bitrates,
	.n_bitrates	= ARRAY_SIZE(rtw89_bitrates),
	.ht_cap		= {0},
	.vht_cap	= {0},
};

static struct ieee80211_supported_band rtw89_sband_5ghz = {
	.band		= NL80211_BAND_5GHZ,
	.channels	= rtw89_channels_5ghz,
	.n_channels	= ARRAY_SIZE(rtw89_channels_5ghz),

	/* 5G has no CCK rates, 1M/2M/5.5M/11M */
	.bitrates	= rtw89_bitrates + 4,
	.n_bitrates	= ARRAY_SIZE(rtw89_bitrates) - 4,
	.ht_cap		= {0},
	.vht_cap	= {0},
};

static void rtw89_get_channel_params(struct cfg80211_chan_def *chandef,
				     struct rtw89_channel_params *chan_param)
{
	struct ieee80211_channel *channel = chandef->chan;
	enum nl80211_chan_width width = chandef->width;
	u8 *cch_by_bw = chan_param->cch_by_bw;
	u32 primary_freq, center_freq;
	u8 center_chan;
	u8 bandwidth = RTW89_CHANNEL_WIDTH_20;
	u8 primary_chan_idx = 0;
	u8 i;

	center_chan = channel->hw_value;
	primary_freq = channel->center_freq;
	center_freq = chandef->center_freq1;

	/* assign the center channel used while 20M bw is selected */
	cch_by_bw[RTW89_CHANNEL_WIDTH_20] = channel->hw_value;

	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		bandwidth = RTW89_CHANNEL_WIDTH_20;
		primary_chan_idx = RTW89_SC_DONT_CARE;
		break;
	case NL80211_CHAN_WIDTH_40:
		bandwidth = RTW89_CHANNEL_WIDTH_40;
		if (primary_freq > center_freq) {
			primary_chan_idx = RTW89_SC_20_UPPER;
			center_chan -= 2;
		} else {
			primary_chan_idx = RTW89_SC_20_LOWER;
			center_chan += 2;
		}
		break;
	case NL80211_CHAN_WIDTH_80:
		bandwidth = RTW89_CHANNEL_WIDTH_80;
		if (primary_freq > center_freq) {
			if (primary_freq - center_freq == 10) {
				primary_chan_idx = RTW89_SC_20_UPPER;
				center_chan -= 2;
			} else {
				primary_chan_idx = RTW89_SC_20_UPMOST;
				center_chan -= 6;
			}
			/* assign the center channel used
			 * while 40M bw is selected
			 */
			cch_by_bw[RTW89_CHANNEL_WIDTH_40] = center_chan + 4;
		} else {
			if (center_freq - primary_freq == 10) {
				primary_chan_idx = RTW89_SC_20_LOWER;
				center_chan += 2;
			} else {
				primary_chan_idx = RTW89_SC_20_LOWEST;
				center_chan += 6;
			}
			/* assign the center channel used
			 * while 40M bw is selected
			 */
			cch_by_bw[RTW89_CHANNEL_WIDTH_40] = center_chan - 4;
		}
		break;
	default:
		center_chan = 0;
		break;
	}

	chan_param->center_chan = center_chan;
	chan_param->primary_chan = channel->hw_value;
	chan_param->bandwidth = bandwidth;
	chan_param->pri_ch_idx = primary_chan_idx;

	/* assign the center channel used while current bw is selected */
	cch_by_bw[bandwidth] = center_chan;

	for (i = bandwidth + 1; i <= RTW89_MAX_CHANNEL_WIDTH; i++)
		cch_by_bw[i] = 0;
}

void rtw89_set_channel(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_channel_params ch_param;
	struct rtw89_channel_help_params bak;
	u8 center_chan, bandwidth;
	u8 band_type;
	bool band_changed;
	u8 i;

	rtw89_get_channel_params(&hw->conf.chandef, &ch_param);
	if (WARN(ch_param.center_chan == 0, "Invalid channel\n"))
		return;

	center_chan = ch_param.center_chan;
	bandwidth = ch_param.bandwidth;
	band_type = center_chan > 14 ? RTW89_BAND_5G : RTW89_BAND_2G;
	band_changed = hal->current_band_type != band_type;

	hal->current_band_width = bandwidth;
	hal->current_channel = center_chan;
	hal->current_primary_channel = ch_param.primary_chan;
	hal->current_band_type = band_type;

	switch (center_chan) {
	case 1 ... 14:
		hal->current_subband = RTW89_CH_2G;
		break;
	case 36 ... 64:
		hal->current_subband = RTW89_CH_5G_BAND_1;
		break;
	case 100 ... 144:
		hal->current_subband = RTW89_CH_5G_BAND_3;
		break;
	case 149 ... 177:
		hal->current_subband = RTW89_CH_5G_BAND_4;
		break;
	}

	for (i = RTW89_CHANNEL_WIDTH_20; i <= RTW89_MAX_CHANNEL_WIDTH; i++)
		hal->cch_by_bw[i] = ch_param.cch_by_bw[i];

	rtw89_chip_set_channel_prepare(rtwdev, &bak);

	chip->ops->set_channel(rtwdev, &ch_param);

	if (band_changed)
		rtw89_btc_ntfy_switch_band(rtwdev, RTW89_PHY_0, hal->current_band_type);

	rtw89_chip_set_txpwr(rtwdev);

	rtw89_chip_set_channel_done(rtwdev, &bak);
}

static enum rtw89_core_tx_type
rtw89_core_get_tx_type(struct rtw89_dev *rtwdev,
		       struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;

	if (ieee80211_is_mgmt(fc) || ieee80211_is_nullfunc(fc))
		return RTW89_CORE_TX_TYPE_MGMT;

	return RTW89_CORE_TX_TYPE_DATA;
}

static void
rtw89_core_tx_update_ampdu_info(struct rtw89_dev *rtwdev,
				struct rtw89_core_tx_request *tx_req, u8 tid)
{
	struct ieee80211_sta *sta = tx_req->sta;
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct rtw89_sta *rtwsta;
	u8 ampdu_num;

	if (!sta) {
		rtw89_warn(rtwdev, "cannot set ampdu info without sta\n");
		return;
	}

	rtwsta = (struct rtw89_sta *)sta->drv_priv;

	ampdu_num = (u8)((rtwsta->ampdu_params[tid].agg_num ?
			  rtwsta->ampdu_params[tid].agg_num :
			  4 << sta->ht_cap.ampdu_factor) - 1);

	desc_info->agg_en = true;
	desc_info->ampdu_density = sta->ht_cap.ampdu_density;
	desc_info->ampdu_num = ampdu_num;
}

static void
rtw89_core_tx_update_sec_key(struct rtw89_dev *rtwdev,
			     struct rtw89_core_tx_request *tx_req)
{
	struct ieee80211_vif *vif = tx_req->vif;
	struct ieee80211_tx_info *info;
	struct ieee80211_key_conf *key;
	struct rtw89_vif *rtwvif;
	struct rtw89_addr_cam_entry *addr_cam;
	struct rtw89_sec_cam_entry *sec_cam;
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct sk_buff *skb = tx_req->skb;
	u8 sec_type = RTW89_SEC_KEY_TYPE_NONE;

	if (!vif) {
		rtw89_warn(rtwdev, "cannot set sec key without vif\n");
		return;
	}

	rtwvif = (struct rtw89_vif *)vif->drv_priv;
	addr_cam = &rtwvif->addr_cam;

	info = IEEE80211_SKB_CB(skb);
	key = info->control.hw_key;
	sec_cam = addr_cam->sec_entries[key->hw_key_idx];
	if (!sec_cam) {
		rtw89_warn(rtwdev, "sec cam entry is empty\n");
		return;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		sec_type = RTW89_SEC_KEY_TYPE_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		sec_type = RTW89_SEC_KEY_TYPE_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		sec_type = RTW89_SEC_KEY_TYPE_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		sec_type = RTW89_SEC_KEY_TYPE_CCMP128;
		break;
	default:
		rtw89_warn(rtwdev, "key cipher not supported %d\n", key->cipher);
		return;
	}

	desc_info->sec_en = true;
	desc_info->sec_type = sec_type;
	desc_info->sec_cam_idx = sec_cam->sec_cam_idx;
}

static void
rtw89_core_tx_update_mgmt_info(struct rtw89_dev *rtwdev,
			       struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	u8 qsel, ch_dma;

	qsel = RTW89_TX_QSEL_B0_MGMT;
	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	desc_info->qsel = RTW89_TX_QSEL_B0_MGMT;
	desc_info->ch_dma = ch_dma;

	/* fixed data rate for mgmt frames */
	desc_info->en_wd_info = true;
	desc_info->use_rate = true;
	desc_info->dis_data_fb = true;
	desc_info->data_rate = 0x00;
}

static void
rtw89_core_tx_update_h2c_info(struct rtw89_dev *rtwdev,
			      struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;

	desc_info->is_bmc = false;
	desc_info->wd_page = false;
	desc_info->ch_dma = RTW89_DMA_H2C;
}

static void
rtw89_core_tx_update_data_info(struct rtw89_dev *rtwdev,
			       struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct sk_buff *skb = tx_req->skb;
	u8 tid, tid_indicate;
	u8 qsel, ch_dma;

	tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
	tid_indicate = rtw89_core_get_tid_indicate(rtwdev, tid);
	qsel = rtw89_core_get_qsel(rtwdev, tid);
	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	desc_info->ch_dma = ch_dma;
	desc_info->tid_indicate = tid_indicate;
	desc_info->qsel = qsel;

	/* enable wd_info for AMPDU */
	desc_info->en_wd_info = true;

	if (IEEE80211_SKB_CB(skb)->flags & IEEE80211_TX_CTL_AMPDU)
		rtw89_core_tx_update_ampdu_info(rtwdev, tx_req, tid);
	if (IEEE80211_SKB_CB(skb)->control.hw_key)
		rtw89_core_tx_update_sec_key(rtwdev, tx_req);
}

static void
rtw89_core_tx_update_desc_info(struct rtw89_dev *rtwdev,
			       struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct sk_buff *skb = tx_req->skb;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	enum rtw89_core_tx_type tx_type;
	bool is_bmc;
	u16 seq;

	seq = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	if (tx_req->tx_type != RTW89_CORE_TX_TYPE_FWCMD) {
		tx_type = rtw89_core_get_tx_type(rtwdev, skb);
		tx_req->tx_type = tx_type;
	}
	is_bmc = (is_broadcast_ether_addr(hdr->addr1) ||
		  is_multicast_ether_addr(hdr->addr1));

	desc_info->seq = seq;
	desc_info->pkt_size = skb->len;
	desc_info->is_bmc = is_bmc;
	desc_info->wd_page = true;

	switch (tx_req->tx_type) {
	case RTW89_CORE_TX_TYPE_MGMT:
		rtw89_core_tx_update_mgmt_info(rtwdev, tx_req);
		break;
	case RTW89_CORE_TX_TYPE_DATA:
		rtw89_core_tx_update_data_info(rtwdev, tx_req);
		break;
	case RTW89_CORE_TX_TYPE_FWCMD:
		rtw89_core_tx_update_h2c_info(rtwdev, tx_req);
		break;
	}
}

void rtw89_core_tx_kick_off(struct rtw89_dev *rtwdev, u8 qsel)
{
	u8 ch_dma;

	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	rtw89_hci_tx_kick_off(rtwdev, ch_dma);
}

int rtw89_h2c_tx(struct rtw89_dev *rtwdev,
		 struct sk_buff *skb, bool fwdl)
{
	struct rtw89_core_tx_request tx_req = {0};
	int ret;

	tx_req.skb = skb;
	tx_req.tx_type = RTW89_CORE_TX_TYPE_FWCMD;
	if (fwdl)
		tx_req.desc_info.fw_dl = true;

	rtw89_core_tx_update_desc_info(rtwdev, &tx_req);

	if (!fwdl)
		rtw89_hex_dump(rtwdev, RTW89_DBG_FW, "H2C: ", skb->data, skb->len);

	ret = rtw89_hci_tx_write(rtwdev, &tx_req);
	if (ret) {
		rtw89_err(rtwdev, "failed to transmit skb to HCI\n");
		return ret;
	}
	rtw89_hci_tx_kick_off(rtwdev, RTW89_TXCH_CH12);

	return 0;
}

int rtw89_core_tx_write(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, struct sk_buff *skb, int *qsel)
{
	struct rtw89_core_tx_request tx_req = {0};
	int ret;

	tx_req.skb = skb;
	tx_req.sta = sta;
	tx_req.vif = vif;

	rtw89_core_tx_update_desc_info(rtwdev, &tx_req);
	ret = rtw89_hci_tx_write(rtwdev, &tx_req);
	if (ret) {
		rtw89_err(rtwdev, "failed to transmit skb to HCI\n");
		return ret;
	}

	if (qsel)
		*qsel = tx_req.desc_info.qsel;

	return 0;
}

void rtw89_core_fill_txdesc(struct rtw89_dev *rtwdev,
			    struct rtw89_tx_desc_info *desc_info,
			    void *txdesc)
{
	RTW89_SET_TXWD_BODY_WP_OFFSET(txdesc, desc_info->wp_offset);
	RTW89_SET_TXWD_BODY_WD_INFO_EN(txdesc, desc_info->en_wd_info);
	RTW89_SET_TXWD_BODY_CHANNEL_DMA(txdesc, desc_info->ch_dma);
	RTW89_SET_TXWD_BODY_HDR_LLC_LEN(txdesc, desc_info->hdr_llc_len);
	RTW89_SET_TXWD_BODY_WD_PAGE(txdesc, desc_info->wd_page);
	RTW89_SET_TXWD_BODY_FW_DL(txdesc, desc_info->fw_dl);
	RTW89_SET_TXWD_BODY_SW_SEQ(txdesc, desc_info->seq);

	RTW89_SET_TXWD_BODY_TID_INDICATE(txdesc, desc_info->tid_indicate);
	RTW89_SET_TXWD_BODY_QSEL(txdesc, desc_info->qsel);
	RTW89_SET_TXWD_BODY_TXPKT_SIZE(txdesc, desc_info->pkt_size);
	RTW89_SET_TXWD_BODY_AGG_EN(txdesc, desc_info->agg_en);

	if (!desc_info->en_wd_info)
		return;

	RTW89_SET_TXWD_INFO_USE_RATE(txdesc, desc_info->use_rate);
	RTW89_SET_TXWD_INFO_DATA_RATE(txdesc, desc_info->data_rate);
	RTW89_SET_TXWD_INFO_DISDATAFB(txdesc, desc_info->dis_data_fb);
	RTW89_SET_TXWD_INFO_MAX_AGGNUM(txdesc, desc_info->ampdu_num);
	RTW89_SET_TXWD_INFO_AMPDU_DENSITY(txdesc, desc_info->ampdu_density);
	RTW89_SET_TXWD_INFO_SEC_TYPE(txdesc, desc_info->sec_type);
	RTW89_SET_TXWD_INFO_SEC_HW_ENC(txdesc, desc_info->sec_en);
	RTW89_SET_TXWD_INFO_SEC_CAM_IDX(txdesc, desc_info->sec_cam_idx);
}
EXPORT_SYMBOL(rtw89_core_fill_txdesc);

static int rtw89_core_rx_process_mac_ppdu(struct rtw89_dev *rtwdev,
					  struct sk_buff *skb,
					  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	bool rx_cnt_valid = false;
	u8 plcp_size = 0;
	u8 usr_num = 0;
	u8 *phy_sts;

	rx_cnt_valid = RTW89_GET_RXINFO_RX_CNT_VLD(skb->data);
	plcp_size = RTW89_GET_RXINFO_PLCP_LEN(skb->data) << 3;
	usr_num = RTW89_GET_RXINFO_USR_NUM(skb->data);
	if (usr_num > RTW89_PPDU_MAX_USR) {
		rtw89_warn(rtwdev, "Invalid user number in mac info\n");
		return -EINVAL;
	}

	phy_sts = skb->data + RTW89_PPDU_MAC_INFO_SIZE;
	phy_sts += usr_num * RTW89_PPDU_MAC_INFO_USR_SIZE;
	/* 8-byte alignment */
	if (usr_num & BIT(0))
		phy_sts += RTW89_PPDU_MAC_INFO_USR_SIZE;
	if (rx_cnt_valid)
		phy_sts += RTW89_PPDU_MAC_RX_CNT_SIZE;
	phy_sts += plcp_size;

	phy_ppdu->buf = phy_sts;
	phy_ppdu->len = skb->data + skb->len - phy_sts;

	return 0;
}

static void rtw89_core_rx_process_phy_ppdu_iter(void *data,
						struct ieee80211_sta *sta)
{
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct rtw89_rx_phy_ppdu *phy_ppdu = (struct rtw89_rx_phy_ppdu *)data;

	if (rtwsta->mac_id == phy_ppdu->mac_id)
		ewma_rssi_add(&rtwsta->avg_rssi, phy_ppdu->rssi_avg);
}

#define VAR_LEN 0xff
#define VAR_LEN_UNIT 8
static u16 rtw89_core_get_phy_status_ie_len(struct rtw89_dev *rtwdev, u8 *addr)
{
	static const u8 physts_ie_len_tab[32] = {
		16, 32, 24, 24, 8, 8, 8, 8, VAR_LEN, 8, VAR_LEN, 176, VAR_LEN,
		VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, 16, 24, VAR_LEN,
		VAR_LEN, VAR_LEN, 0, 24, 24, 24, 24, 32, 32, 32, 32
	};
	u16 ie_len;
	u8 ie;

	ie = RTW89_GET_PHY_STS_IE_TYPE(addr);
	if (physts_ie_len_tab[ie] != VAR_LEN)
		ie_len = physts_ie_len_tab[ie];
	else
		ie_len = RTW89_GET_PHY_STS_IE_LEN(addr) * VAR_LEN_UNIT;

	return ie_len;
}

static void rtw89_core_parse_phy_status_ie01(struct rtw89_dev *rtwdev, u8 *addr,
					     struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	s16 cfo;

	/* sign conversion for S(12,2) */
	cfo = sign_extend32(RTW89_GET_PHY_STS_IE0_CFO(addr), 11);
	rtw89_phy_cfo_parse(rtwdev, cfo, phy_ppdu);
}

static int rtw89_core_process_phy_status_ie(struct rtw89_dev *rtwdev, u8 *addr,
					    struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	u8 ie;

	ie = RTW89_GET_PHY_STS_IE_TYPE(addr);
	switch (ie) {
	case RTW89_PHYSTS_IE01_CMN_OFDM:
		rtw89_core_parse_phy_status_ie01(rtwdev, addr, phy_ppdu);
		break;
	default:
		break;
	}

	return 0;
}

static void rtw89_core_update_phy_ppdu(struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	s8 *rssi = phy_ppdu->rssi;

	phy_ppdu->rssi_avg = RTW89_GET_PHY_STS_RSSI_AVG(phy_ppdu->buf);
	rssi[RF_PATH_A] =
		(s8)(RTW89_GET_PHY_STS_RSSI_A(phy_ppdu->buf) >> 1) - MAX_RSSI;
	rssi[RF_PATH_B] =
		(s8)(RTW89_GET_PHY_STS_RSSI_B(phy_ppdu->buf) >> 1) - MAX_RSSI;
	rssi[RF_PATH_C] =
		(s8)(RTW89_GET_PHY_STS_RSSI_C(phy_ppdu->buf) >> 1) - MAX_RSSI;
	rssi[RF_PATH_D] =
		(s8)(RTW89_GET_PHY_STS_RSSI_D(phy_ppdu->buf) >> 1) - MAX_RSSI;
}

static int rtw89_core_rx_process_phy_ppdu(struct rtw89_dev *rtwdev,
					  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	if (RTW89_GET_PHY_STS_LEN(phy_ppdu->buf) << 3 != phy_ppdu->len) {
		rtw89_warn(rtwdev, "phy ppdu len mismatch\n");
		return -EINVAL;
	}
	rtw89_core_update_phy_ppdu(phy_ppdu);
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_core_rx_process_phy_ppdu_iter,
					  phy_ppdu);

	return 0;
}

static int rtw89_core_rx_parse_phy_sts(struct rtw89_dev *rtwdev,
				       struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	u16 ie_len;
	u8 *pos, *end;

	if (!phy_ppdu->to_self)
		return 0;

	pos = (u8 *)phy_ppdu->buf + PHY_STS_HDR_LEN;
	end = (u8 *)phy_ppdu->buf + phy_ppdu->len;
	while (pos < end) {
		ie_len = rtw89_core_get_phy_status_ie_len(rtwdev, pos);
		rtw89_core_process_phy_status_ie(rtwdev, pos, phy_ppdu);
		pos += ie_len;
		if (pos > end || ie_len == 0) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "phy status parse failed\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void rtw89_core_rx_process_phy_sts(struct rtw89_dev *rtwdev,
					  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	int ret;

	ret = rtw89_core_rx_parse_phy_sts(rtwdev, phy_ppdu);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_TXRX, "parse phy sts failed\n");
	else
		phy_ppdu->valid = true;
}

static bool rtw89_core_rx_ppdu_match(struct rtw89_dev *rtwdev,
				     struct rtw89_rx_desc_info *desc_info,
				     struct ieee80211_rx_status *status)
{
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;
	u8 data_rate_mode, bw, rate_idx = MASKBYTE0, gi_ltf;
	u16 data_rate;
	bool ret;

	data_rate = desc_info->data_rate;
	data_rate_mode = GET_DATA_RATE_MODE(data_rate);
	if (data_rate_mode == DATA_RATE_MODE_NON_HT) {
		rate_idx = GET_DATA_RATE_NOT_HT_IDX(data_rate);
		/* No 4 CCK rates for 5G */
		if (status->band == NL80211_BAND_5GHZ)
			rate_idx -= 4;
	} else if (data_rate_mode == DATA_RATE_MODE_HT) {
		rate_idx = GET_DATA_RATE_HT_IDX(data_rate);
	} else if (data_rate_mode == DATA_RATE_MODE_VHT) {
		rate_idx = GET_DATA_RATE_VHT_HE_IDX(data_rate);
	} else if (data_rate_mode == DATA_RATE_MODE_HE) {
		rate_idx = GET_DATA_RATE_VHT_HE_IDX(data_rate);
	} else {
		rtw89_warn(rtwdev, "invalid RX rate mode %d\n", data_rate_mode);
	}

	if (desc_info->bw == RTW89_CHANNEL_WIDTH_80)
		bw = RATE_INFO_BW_80;
	else if (desc_info->bw == RTW89_CHANNEL_WIDTH_40)
		bw = RATE_INFO_BW_40;
	else
		bw = RATE_INFO_BW_20;

	switch (desc_info->gi_ltf) {
	case RTW89_GILTF_SGI_4XHE08:
	case RTW89_GILTF_2XHE08:
	case RTW89_GILTF_1XHE08:
		gi_ltf = NL80211_RATE_INFO_HE_GI_0_8;
		break;
	case RTW89_GILTF_2XHE16:
	case RTW89_GILTF_1XHE16:
		gi_ltf = NL80211_RATE_INFO_HE_GI_1_6;
		break;
	case RTW89_GILTF_LGI_4XHE32:
		gi_ltf = NL80211_RATE_INFO_HE_GI_3_2;
		break;
	default:
		gi_ltf = U8_MAX;
	}
	ret = rtwdev->ppdu_sts.curr_rx_ppdu_cnt[band] == desc_info->ppdu_cnt &&
	      status->rate_idx == rate_idx &&
	      status->he_gi == gi_ltf &&
	      status->bw == bw;

	return ret;
}

static void rtw89_core_rx_pending_skb(struct rtw89_dev *rtwdev,
				      struct rtw89_rx_phy_ppdu *phy_ppdu,
				      struct rtw89_rx_desc_info *desc_info,
				      struct sk_buff *skb)
{
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;
	int curr = rtwdev->ppdu_sts.curr_rx_ppdu_cnt[band];
	struct sk_buff *skb_ppdu = NULL, *tmp;
	struct ieee80211_rx_status *rx_status;

	if (curr > RTW89_MAX_PPDU_CNT)
		return;

	skb_queue_walk_safe(&rtwdev->ppdu_sts.rx_queue[band], skb_ppdu, tmp) {
		skb_unlink(skb_ppdu, &rtwdev->ppdu_sts.rx_queue[band]);
		rx_status = IEEE80211_SKB_RXCB(skb_ppdu);
		if (rtw89_core_rx_ppdu_match(rtwdev, desc_info, rx_status))
			rtw89_chip_query_ppdu(rtwdev, phy_ppdu, rx_status);
		ieee80211_rx_irqsafe(rtwdev->hw, skb_ppdu);
	}
}

static void rtw89_core_rx_process_ppdu_sts(struct rtw89_dev *rtwdev,
					   struct rtw89_rx_desc_info *desc_info,
					   struct sk_buff *skb)
{
	struct rtw89_rx_phy_ppdu phy_ppdu = {.buf = skb->data, .valid = false,
					     .len = skb->len,
					     .to_self = desc_info->addr1_match,
					     .mac_id = desc_info->mac_id};
	int ret;

	if (desc_info->mac_info_valid)
		rtw89_core_rx_process_mac_ppdu(rtwdev, skb, &phy_ppdu);
	ret = rtw89_core_rx_process_phy_ppdu(rtwdev, &phy_ppdu);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_TXRX, "process ppdu failed\n");

	rtw89_core_rx_process_phy_sts(rtwdev, &phy_ppdu);
	rtw89_core_rx_pending_skb(rtwdev, &phy_ppdu, desc_info, skb);
	dev_kfree_skb_any(skb);
}

static void rtw89_core_rx_process_report(struct rtw89_dev *rtwdev,
					 struct rtw89_rx_desc_info *desc_info,
					 struct sk_buff *skb)
{
	switch (desc_info->pkt_type) {
	case RTW89_CORE_RX_TYPE_C2H:
		rtw89_fw_c2h_irqsafe(rtwdev, skb);
		break;
	case RTW89_CORE_RX_TYPE_PPDU_STAT:
		rtw89_core_rx_process_ppdu_sts(rtwdev, desc_info, skb);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_TXRX, "unhandled pkt_type=%d\n",
			    desc_info->pkt_type);
		dev_kfree_skb_any(skb);
		break;
	}
}

void rtw89_core_query_rxdesc(struct rtw89_dev *rtwdev,
			     struct rtw89_rx_desc_info *desc_info,
			     u8 *data, u32 data_offset)
{
	struct rtw89_rxdesc_short *rxd_s;
	struct rtw89_rxdesc_long *rxd_l;
	u8 shift_len, drv_info_len;

	rxd_s = (struct rtw89_rxdesc_short *)(data + data_offset);
	desc_info->pkt_size = RTW89_GET_RXWD_PKT_SIZE(rxd_s);
	desc_info->drv_info_size = RTW89_GET_RXWD_DRV_INFO_SIZE(rxd_s);
	desc_info->long_rxdesc = RTW89_GET_RXWD_LONG_RXD(rxd_s);
	desc_info->pkt_type = RTW89_GET_RXWD_RPKT_TYPE(rxd_s);
	desc_info->mac_info_valid = RTW89_GET_RXWD_MAC_INFO_VALID(rxd_s);
	desc_info->bw = RTW89_GET_RXWD_BW(rxd_s);
	desc_info->data_rate = RTW89_GET_RXWD_DATA_RATE(rxd_s);
	desc_info->gi_ltf = RTW89_GET_RXWD_GI_LTF(rxd_s);
	desc_info->user_id = RTW89_GET_RXWD_USER_ID(rxd_s);
	desc_info->sr_en = RTW89_GET_RXWD_SR_EN(rxd_s);
	desc_info->ppdu_cnt = RTW89_GET_RXWD_PPDU_CNT(rxd_s);
	desc_info->ppdu_type = RTW89_GET_RXWD_PPDU_TYPE(rxd_s);
	desc_info->free_run_cnt = RTW89_GET_RXWD_FREE_RUN_CNT(rxd_s);
	desc_info->icv_err = RTW89_GET_RXWD_ICV_ERR(rxd_s);
	desc_info->crc32_err = RTW89_GET_RXWD_CRC32_ERR(rxd_s);
	desc_info->hw_dec = RTW89_GET_RXWD_HW_DEC(rxd_s);
	desc_info->sw_dec = RTW89_GET_RXWD_SW_DEC(rxd_s);
	desc_info->addr1_match = RTW89_GET_RXWD_A1_MATCH(rxd_s);

	shift_len = desc_info->shift << 1; /* 2-byte unit */
	drv_info_len = desc_info->drv_info_size << 3; /* 8-byte unit */
	desc_info->offset = data_offset + shift_len + drv_info_len;
	desc_info->ready = true;

	if (!desc_info->long_rxdesc)
		return;

	rxd_l = (struct rtw89_rxdesc_long *)(data + data_offset);
	desc_info->frame_type = RTW89_GET_RXWD_TYPE(rxd_l);
	desc_info->addr_cam_valid = RTW89_GET_RXWD_ADDR_CAM_VLD(rxd_l);
	desc_info->addr_cam_id = RTW89_GET_RXWD_ADDR_CAM_ID(rxd_l);
	desc_info->sec_cam_id = RTW89_GET_RXWD_SEC_CAM_ID(rxd_l);
	desc_info->mac_id = RTW89_GET_RXWD_MAC_ID(rxd_l);
	desc_info->rx_pl_id = RTW89_GET_RXWD_RX_PL_ID(rxd_l);
}
EXPORT_SYMBOL(rtw89_core_query_rxdesc);

static void rtw89_core_update_rx_status(struct rtw89_dev *rtwdev,
					struct rtw89_rx_desc_info *desc_info,
					struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	u16 data_rate;
	u8 data_rate_mode;

	/* currently using single PHY */
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;

	if (desc_info->icv_err || desc_info->crc32_err)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (desc_info->hw_dec &&
	    !(desc_info->sw_dec || desc_info->icv_err))
		rx_status->flag |= RX_FLAG_DECRYPTED;

	if (desc_info->bw == RTW89_CHANNEL_WIDTH_80)
		rx_status->bw = RATE_INFO_BW_80;
	else if (desc_info->bw == RTW89_CHANNEL_WIDTH_40)
		rx_status->bw = RATE_INFO_BW_40;
	else
		rx_status->bw = RATE_INFO_BW_20;

	data_rate = desc_info->data_rate;
	data_rate_mode = GET_DATA_RATE_MODE(data_rate);
	if (data_rate_mode == DATA_RATE_MODE_NON_HT) {
		rx_status->encoding = RX_ENC_LEGACY;
		rx_status->rate_idx = GET_DATA_RATE_NOT_HT_IDX(data_rate);
		/* No 4 CCK rates for 5G */
		if (rx_status->band == NL80211_BAND_5GHZ)
			rx_status->rate_idx -= 4;
	} else if (data_rate_mode == DATA_RATE_MODE_HT) {
		rx_status->encoding = RX_ENC_HT;
		rx_status->rate_idx = GET_DATA_RATE_HT_IDX(data_rate);
	} else if (data_rate_mode == DATA_RATE_MODE_VHT) {
		rx_status->encoding = RX_ENC_VHT;
		rx_status->rate_idx = GET_DATA_RATE_VHT_HE_IDX(data_rate);
		rx_status->nss = GET_DATA_RATE_NSS(data_rate) + 1;
	} else if (data_rate_mode == DATA_RATE_MODE_HE) {
		rx_status->encoding = RX_ENC_HE;
		rx_status->rate_idx = GET_DATA_RATE_VHT_HE_IDX(data_rate);
		rx_status->nss = GET_DATA_RATE_NSS(data_rate) + 1;
	} else {
		rtw89_warn(rtwdev, "invalid RX rate mode %d\n", data_rate_mode);
	}

	switch (desc_info->gi_ltf) {
	case RTW89_GILTF_SGI_4XHE08:
	case RTW89_GILTF_2XHE08:
	case RTW89_GILTF_1XHE08:
		rx_status->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
		break;
	case RTW89_GILTF_2XHE16:
	case RTW89_GILTF_1XHE16:
		rx_status->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
		break;
	case RTW89_GILTF_LGI_4XHE32:
		rx_status->he_gi = NL80211_RATE_INFO_HE_GI_3_2;
		break;
	default:
		break;
	}

	rx_status->flag |= RX_FLAG_MACTIME_START;
	rx_status->mactime = desc_info->free_run_cnt;
}

static void rtw89_core_flush_ppdu_rx_queue(struct rtw89_dev *rtwdev,
					   struct rtw89_rx_desc_info *desc_info)
{
	struct rtw89_ppdu_sts_info *ppdu_sts = &rtwdev->ppdu_sts;
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;
	struct sk_buff *skb_ppdu, *tmp;

	skb_queue_walk_safe(&ppdu_sts->rx_queue[band], skb_ppdu, tmp) {
		skb_unlink(skb_ppdu, &ppdu_sts->rx_queue[band]);
		ieee80211_rx_irqsafe(rtwdev->hw, skb_ppdu);
	}
}

void rtw89_core_rx(struct rtw89_dev *rtwdev,
		   struct rtw89_rx_desc_info *desc_info,
		   struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct rtw89_ppdu_sts_info *ppdu_sts = &rtwdev->ppdu_sts;
	u8 ppdu_cnt = desc_info->ppdu_cnt;
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;

	if (desc_info->pkt_type != RTW89_CORE_RX_TYPE_WIFI) {
		rtw89_core_rx_process_report(rtwdev, desc_info, skb);
		return;
	}

	if (ppdu_sts->curr_rx_ppdu_cnt[band] != ppdu_cnt) {
		rtw89_core_flush_ppdu_rx_queue(rtwdev, desc_info);
		ppdu_sts->curr_rx_ppdu_cnt[band] = ppdu_cnt;
	}

	rx_status = IEEE80211_SKB_RXCB(skb);
	memset(rx_status, 0, sizeof(struct ieee80211_rx_status));
	rtw89_core_update_rx_status(rtwdev, desc_info, rx_status);
	if (desc_info->long_rxdesc &&
	    BIT(desc_info->frame_type) & PPDU_FILTER_BITMAP)
		skb_queue_tail(&ppdu_sts->rx_queue[band], skb);
	else
		ieee80211_rx_irqsafe(rtwdev->hw, skb);
}
EXPORT_SYMBOL(rtw89_core_rx);

static void rtw89_core_ba_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev =
		container_of(work, struct rtw89_dev, ba_work);
	struct rtw89_txq *rtwtxq, *tmp;
	int ret;

	spin_lock_bh(&rtwdev->ba_lock);
	list_for_each_entry_safe(rtwtxq, tmp, &rtwdev->ba_list, list) {
		struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);
		struct ieee80211_sta *sta = txq->sta;
		u8 tid = txq->tid;

		if (!sta) {
			rtw89_warn(rtwdev, "cannot start BA without sta\n");
			list_del_init(&rtwtxq->list);
			continue;
		}

		ret = ieee80211_start_tx_ba_session(sta, tid, 0);
		if (ret) {
			rtw89_info(rtwdev,
				   "failed to setup BA session for %pM:%2d: %d\n",
				   sta->addr, tid, ret);
			if (ret == -EINVAL)
				set_bit(RTW89_TXQ_F_BLOCK_BA, &rtwtxq->flags);
		}
		list_del_init(&rtwtxq->list);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

static void rtw89_core_free_sta_pending_ba(struct rtw89_dev *rtwdev,
					   struct ieee80211_sta *sta)
{
	struct rtw89_txq *rtwtxq, *tmp;

	spin_lock_bh(&rtwdev->ba_lock);
	list_for_each_entry_safe(rtwtxq, tmp, &rtwdev->ba_list, list) {
		struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);

		if (sta == txq->sta)
			list_del_init(&rtwtxq->list);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

static void rtw89_core_txq_check_agg(struct rtw89_dev *rtwdev,
				     struct rtw89_txq *rtwtxq,
				     struct sk_buff *skb)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);

	if (unlikely(skb_get_queue_mapping(skb) == IEEE80211_AC_VO))
		return;

	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		return;

	if (unlikely(!txq->sta))
		return;

	if (unlikely(test_bit(RTW89_TXQ_F_BLOCK_BA, &rtwtxq->flags)))
		return;

	if (test_bit(RTW89_TXQ_F_AMPDU, &rtwtxq->flags)) {
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_AMPDU;
		return;
	}

	spin_lock_bh(&rtwdev->ba_lock);
	if (list_empty(&rtwtxq->list)) {
		list_add_tail(&rtwtxq->list, &rtwdev->ba_list);
		ieee80211_queue_work(hw, &rtwdev->ba_work);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

static void rtw89_core_txq_push(struct rtw89_dev *rtwdev,
				struct rtw89_txq *rtwtxq,
				unsigned long frame_cnt,
				unsigned long byte_cnt)
{
	struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);
	struct ieee80211_vif *vif = txq->vif;
	struct ieee80211_sta *sta = txq->sta;
	struct sk_buff *skb;
	unsigned long i;
	int ret;

	for (i = 0; i < frame_cnt; i++) {
		skb = ieee80211_tx_dequeue(rtwdev->hw, txq);
		if (!skb) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX, "dequeue a NULL skb\n");
			return;
		}
		rtw89_core_txq_check_agg(rtwdev, rtwtxq, skb);
		ret = rtw89_core_tx_write(rtwdev, vif, sta, skb, NULL);
		if (ret) {
			rtw89_err(rtwdev, "failed to push txq: %d\n", ret);
			ieee80211_free_txskb(rtwdev->hw, skb);
			break;
		}
	}
}

static u32 rtw89_check_and_reclaim_tx_resource(struct rtw89_dev *rtwdev, u8 tid)
{
	u8 qsel, ch_dma;

	qsel = rtw89_core_get_qsel(rtwdev, tid);
	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	return rtw89_hci_check_and_reclaim_tx_resource(rtwdev, ch_dma);
}

static void rtw89_core_txq_schedule(struct rtw89_dev *rtwdev, u8 ac)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_txq *txq;
	struct rtw89_txq *rtwtxq;
	unsigned long frame_cnt;
	unsigned long byte_cnt;
	u32 tx_resource;

	ieee80211_txq_schedule_start(hw, ac);
	while ((txq = ieee80211_next_txq(hw, ac))) {
		rtwtxq = (struct rtw89_txq *)txq->drv_priv;
		tx_resource = rtw89_check_and_reclaim_tx_resource(rtwdev, txq->tid);

		ieee80211_txq_get_depth(txq, &frame_cnt, &byte_cnt);
		frame_cnt = min_t(unsigned long, frame_cnt, tx_resource);
		rtw89_core_txq_push(rtwdev, rtwtxq, frame_cnt, byte_cnt);
		ieee80211_return_txq(hw, txq, false);
		if (frame_cnt != 0)
			rtw89_core_tx_kick_off(rtwdev, rtw89_core_get_qsel(rtwdev, txq->tid));
	}
	ieee80211_txq_schedule_end(hw, ac);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
static void rtw89_core_txq_tasklet(struct tasklet_struct *t)
#else
static void rtw89_core_txq_tasklet(unsigned long arg)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	struct rtw89_dev *rtwdev = from_tasklet(rtwdev, t, txq_tasklet);
#else
	struct rtw89_dev *rtwdev = (struct rtw89_dev *)arg;
#endif
	u8 ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		rtw89_core_txq_schedule(rtwdev, ac);
}

static void rtw89_track_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						track_work.work);

	mutex_lock(&rtwdev->mutex);

	if (!test_bit(RTW89_FLAG_RUNNING, rtwdev->flags))
		goto out;

	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->track_work,
				     RTW89_TRACK_WORK_PERIOD);

	rtw89_phy_stat_track(rtwdev);
	rtw89_phy_env_monitor_track(rtwdev);
	rtw89_phy_dig(rtwdev);
	rtw89_chip_rfk_track(rtwdev);
	rtw89_phy_ra_update(rtwdev);
	rtw89_phy_cfo_track(rtwdev);

out:
	mutex_unlock(&rtwdev->mutex);
}

int rtw89_core_power_on(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_mac_pwr_on(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to start power sequence\n");
		goto err;
	}

	return 0;

err:
	return ret;
}

u8 rtw89_core_acquire_bit_map(unsigned long *addr, unsigned long size)
{
	unsigned long bit;

	bit = find_first_zero_bit(addr, size);
	if (bit < size)
		set_bit(bit, addr);

	return bit;
}

void rtw89_core_release_bit_map(unsigned long *addr, u8 bit)
{
	clear_bit(bit, addr);
}

#define RTW89_TYPE_MAPPING(_type)	\
	case NL80211_IFTYPE_ ## _type:	\
		rtwvif->wifi_role = RTW89_WIFI_ROLE_ ## _type;	\
		break
void rtw89_vif_type_mapping(struct ieee80211_vif *vif, bool assoc)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	switch (vif->type) {
	RTW89_TYPE_MAPPING(ADHOC);
	RTW89_TYPE_MAPPING(STATION);
	RTW89_TYPE_MAPPING(AP);
	RTW89_TYPE_MAPPING(AP_VLAN);
	RTW89_TYPE_MAPPING(MONITOR);
	RTW89_TYPE_MAPPING(MESH_POINT);
	RTW89_TYPE_MAPPING(P2P_CLIENT);
	RTW89_TYPE_MAPPING(P2P_GO);
	RTW89_TYPE_MAPPING(P2P_DEVICE);
	RTW89_TYPE_MAPPING(NAN);
	default:
		WARN_ON(1);
		break;
	}

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		rtwvif->net_type = RTW89_NET_TYPE_AP_MODE;
		rtwvif->self_role = RTW89_SELF_ROLE_AP;
		break;
	case NL80211_IFTYPE_ADHOC:
		rtwvif->net_type = RTW89_NET_TYPE_AD_HOC;
		break;
	case NL80211_IFTYPE_STATION:
		if (assoc) {
			rtwvif->net_type = RTW89_NET_TYPE_INFRA;
			rtwvif->trigger = vif->bss_conf.he_support;
		} else {
			rtwvif->net_type = RTW89_NET_TYPE_NO_LINK;
			rtwvif->trigger = false;
		}
		rtwvif->self_role = RTW89_SELF_ROLE_CLIENT;
		rtwvif->addr_cam.sec_ent_mode = RTW89_ADDR_CAM_SEC_NORMAL;
		break;
	default:
		WARN_ON(1);
		break;
	}
}

int rtw89_core_sta_add(struct rtw89_dev *rtwdev,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	int i;

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		rtw89_core_txq_init(rtwdev, sta->txq[i]);

	ewma_rssi_init(&rtwsta->avg_rssi);

	if (vif->type == NL80211_IFTYPE_STATION) {
		rtwvif->mgd.ap = sta;
		rtw89_chip_rfk_channel(rtwdev);
	}

	return 0;
}

int rtw89_core_sta_disassoc(struct rtw89_dev *rtwdev,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta)
{
	rtwdev->total_sta_assoc--;

	return 0;
}

int rtw89_core_sta_disconnect(struct rtw89_dev *rtwdev,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	int ret;

	rtw89_core_free_sta_pending_ba(rtwdev, sta);

	rtw89_vif_type_mapping(vif, false);

	ret = rtw89_fw_h2c_assoc_cmac_tbl(rtwdev, vif, sta);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cmac table\n");
		return ret;
	}

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif, 1);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c join info\n");
		return ret;
	}

	/* update cam aid mac_id net_type */
	rtw89_fw_h2c_cam(rtwdev, rtwvif);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	return ret;
}

int rtw89_core_sta_assoc(struct rtw89_dev *rtwdev,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	int ret;

	rtw89_vif_type_mapping(vif, true);

	ret = rtw89_fw_h2c_assoc_cmac_tbl(rtwdev, vif, sta);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cmac table\n");
		return ret;
	}

	/* for station mode, assign the mac_id from itself */
	if (vif->type == NL80211_IFTYPE_STATION)
		rtwsta->mac_id = rtwvif->mac_id;

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif, 0);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c join info\n");
		return ret;
	}

	/* update cam aid mac_id net_type */
	rtw89_fw_h2c_cam(rtwdev, rtwvif);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	ret = rtw89_fw_h2c_general_pkt(rtwdev, rtwsta->mac_id);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c general packet\n");
		return ret;
	}

	rtwdev->total_sta_assoc++;
	rtw89_phy_ra_assoc(rtwdev, sta);

	return ret;
}

int rtw89_core_sta_remove(struct rtw89_dev *rtwdev,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	return 0;
}

static void rtw89_init_ht_cap(struct rtw89_dev *rtwdev,
			      struct ieee80211_sta_ht_cap *ht_cap)
{
	ht_cap->ht_supported = true;
	ht_cap->cap = 0;
	ht_cap->cap |= IEEE80211_HT_CAP_SGI_20 |
			IEEE80211_HT_CAP_MAX_AMSDU |
			(1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);

	ht_cap->cap |= IEEE80211_HT_CAP_LDPC_CODING;

	ht_cap->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_DSSSCCK40 |
		       IEEE80211_HT_CAP_SGI_40;
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;
	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	ht_cap->mcs.rx_mask[0] = 0xFF;
	ht_cap->mcs.rx_mask[1] = 0xFF;
	ht_cap->mcs.rx_mask[4] = 0x01;
	ht_cap->mcs.rx_highest = cpu_to_le16(300);
}

static void rtw89_init_vht_cap(struct rtw89_dev *rtwdev,
			       struct ieee80211_sta_vht_cap *vht_cap)
{
	u16 mcs_map;
	__le16 highest;

	vht_cap->vht_supported = true;
	vht_cap->cap = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
		       IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_HTC_VHT |
		       IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
		       0;
	vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;
	vht_cap->cap |= IEEE80211_VHT_CAP_RXLDPC;

	mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 14;
	highest = cpu_to_le16(780);
	mcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << 2;
	vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(mcs_map);
	vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(mcs_map);
	vht_cap->vht_mcs.rx_highest = highest;
	vht_cap->vht_mcs.tx_highest = highest;
}

#define RTW89_SBAND_IFTYPES_NR 2

static void rtw89_init_he_cap(struct rtw89_dev *rtwdev,
			      enum nl80211_band band,
			      struct ieee80211_supported_band *sband)
{
	struct ieee80211_sband_iftype_data *iftype_data;
	u16 mcs_map = 0;
	int i;
	int nss = rtwdev->chip->rx_nss;
	int idx = 0;

	iftype_data = kcalloc(RTW89_SBAND_IFTYPES_NR, sizeof(*iftype_data), GFP_KERNEL);
	if (!iftype_data)
		return;

	for (i = 0; i < 8; i++) {
		if (i < nss)
			mcs_map |= IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2);
		else
			mcs_map |= IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2);
	}

	for (i = 0; i < NUM_NL80211_IFTYPES; i++) {
		struct ieee80211_sta_he_cap *he_cap;
		u8 *mac_cap_info;
		u8 *phy_cap_info;

		switch (i) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_AP:
			break;
		default:
			continue;
		}

		if (idx >= RTW89_SBAND_IFTYPES_NR) {
			rtw89_warn(rtwdev, "run out of iftype_data\n");
			break;
		}

		iftype_data[idx].types_mask = BIT(i);
		he_cap = &iftype_data[idx].he_cap;
		mac_cap_info = he_cap->he_cap_elem.mac_cap_info;
		phy_cap_info = he_cap->he_cap_elem.phy_cap_info;

		he_cap->has_he = true;
		if (i == NL80211_IFTYPE_AP)
			mac_cap_info[0] = IEEE80211_HE_MAC_CAP0_HTC_HE;
		if (i == NL80211_IFTYPE_STATION)
			mac_cap_info[1] = IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US;
		mac_cap_info[2] = IEEE80211_HE_MAC_CAP2_ALL_ACK |
				  IEEE80211_HE_MAC_CAP2_BSR;
		mac_cap_info[3] = 2 << IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_SHIFT;
		if (i == NL80211_IFTYPE_AP)
			mac_cap_info[3] |= IEEE80211_HE_MAC_CAP3_OMI_CONTROL;
		mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_OPS |
				  IEEE80211_HE_MAC_CAP4_AMDSU_IN_AMPDU;
		if (i == NL80211_IFTYPE_STATION)
			mac_cap_info[5] = IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX;
		if (band == NL80211_BAND_2GHZ)
			phy_cap_info[0] = IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
		else if (band == NL80211_BAND_5GHZ)
			phy_cap_info[0] = IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
		phy_cap_info[1] = IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
				  IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
				  IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
		phy_cap_info[2] = IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
				  IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
				  IEEE80211_HE_PHY_CAP2_DOPPLER_TX;
		phy_cap_info[3] = IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM;
		if (i == NL80211_IFTYPE_STATION)
			phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM |
					   IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2;
		if (i == NL80211_IFTYPE_AP)
			phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA;
		phy_cap_info[6] = IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE;
		phy_cap_info[7] = IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_AR |
				  IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI;
		phy_cap_info[8] = IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
				  IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI |
				  IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996;
		phy_cap_info[9] = IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM |
				  IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
				  IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
				  IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
				  IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US;
		if (i == NL80211_IFTYPE_STATION)
			phy_cap_info[9] |= IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU;
		he_cap->he_mcs_nss_supp.rx_mcs_80 = cpu_to_le16(mcs_map);
		he_cap->he_mcs_nss_supp.tx_mcs_80 = cpu_to_le16(mcs_map);

		idx++;
	}

	sband->iftype_data = iftype_data;
	sband->n_iftype_data = idx;
}

static int rtw89_core_set_supported_band(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_supported_band *sband_2ghz = NULL, *sband_5ghz = NULL;
	u32 size = sizeof(struct ieee80211_supported_band);

	sband_2ghz = kmemdup(&rtw89_sband_2ghz, size, GFP_KERNEL);
	if (!sband_2ghz)
		goto err;
	rtw89_init_ht_cap(rtwdev, &sband_2ghz->ht_cap);
	rtw89_init_he_cap(rtwdev, NL80211_BAND_2GHZ, sband_2ghz);
	hw->wiphy->bands[NL80211_BAND_2GHZ] = sband_2ghz;

	sband_5ghz = kmemdup(&rtw89_sband_5ghz, size, GFP_KERNEL);
	if (!sband_5ghz)
		goto err;
	rtw89_init_ht_cap(rtwdev, &sband_5ghz->ht_cap);
	rtw89_init_vht_cap(rtwdev, &sband_5ghz->vht_cap);
	rtw89_init_he_cap(rtwdev, NL80211_BAND_5GHZ, sband_5ghz);
	hw->wiphy->bands[NL80211_BAND_5GHZ] = sband_5ghz;

	return 0;

err:
	hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;
	if (sband_2ghz)
		kfree(sband_2ghz->iftype_data);
	if (sband_5ghz)
		kfree(sband_5ghz->iftype_data);
	kfree(sband_2ghz);
	kfree(sband_5ghz);
	return -ENOMEM;
}

static void rtw89_core_clr_supported_band(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;

	kfree(hw->wiphy->bands[NL80211_BAND_2GHZ]->iftype_data);
	kfree(hw->wiphy->bands[NL80211_BAND_5GHZ]->iftype_data);
	kfree(hw->wiphy->bands[NL80211_BAND_2GHZ]);
	kfree(hw->wiphy->bands[NL80211_BAND_5GHZ]);
	hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;
}

static void rtw89_core_ppdu_sts_init(struct rtw89_dev *rtwdev)
{
	int i;

	for (i = 0; i < RTW89_PHY_MAX; i++)
		skb_queue_head_init(&rtwdev->ppdu_sts.rx_queue[i]);
	for (i = 0; i < RTW89_PHY_MAX; i++)
		rtwdev->ppdu_sts.curr_rx_ppdu_cnt[i] = U8_MAX;
}

int rtw89_core_init(struct rtw89_dev *rtwdev)
{
	int ret;

	rtwdev->mac.rpwm_seq_num = RPWM_SEQ_NUM_MAX;

	INIT_LIST_HEAD(&rtwdev->ba_list);
	INIT_WORK(&rtwdev->ba_work, rtw89_core_ba_work);
	INIT_DELAYED_WORK(&rtwdev->track_work, rtw89_track_work);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	tasklet_setup(&rtwdev->txq_tasklet, rtw89_core_txq_tasklet);
#else
	tasklet_init(&rtwdev->txq_tasklet, rtw89_core_txq_tasklet,
		     (unsigned long)rtwdev);
#endif
	spin_lock_init(&rtwdev->ba_lock);
	mutex_init(&rtwdev->mutex);
	mutex_init(&rtwdev->rf_mutex);
	rtwdev->total_sta_assoc = 0;

	INIT_WORK(&rtwdev->c2h_work, rtw89_fw_c2h_work);
	skb_queue_head_init(&rtwdev->c2h_queue);
	rtw89_core_ppdu_sts_init(rtwdev);

	ret = rtw89_load_firmware(rtwdev);
	if (ret) {
		rtw89_warn(rtwdev, "no firmware loaded\n");
		return ret;
	}
	rtw89_ser_init(rtwdev);

	return 0;
}
EXPORT_SYMBOL(rtw89_core_init);

void rtw89_core_deinit(struct rtw89_dev *rtwdev)
{
	rtw89_ser_deinit(rtwdev);
	rtw89_unload_firmware(rtwdev);

	tasklet_kill(&rtwdev->txq_tasklet);
	mutex_destroy(&rtwdev->rf_mutex);
	mutex_destroy(&rtwdev->mutex);
}
EXPORT_SYMBOL(rtw89_core_deinit);

static void rtw89_read_chip_ver(struct rtw89_dev *rtwdev)
{
	u8 cut;

	cut = rtw89_read32_mask(rtwdev, R_AX_SYS_CFG1, B_AX_CHIP_VER_MSK);
	if (cut <= CHIP_CUT_B) {
		if (rtw89_read32(rtwdev, R_AX_GPIO0_7_FUNC_SEL) == RTW89_R32_DEAD)
			cut = CHIP_CUT_A;
		else
			cut = CHIP_CUT_B;
	}

	rtwdev->hal.cut_version = cut;
}

static int rtw89_chip_efuse_info_setup(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_mac_partial_init(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_parse_efuse_map(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_parse_phycap_map(rtwdev);
	if (ret)
		return ret;

	rtw89_mac_pwr_off(rtwdev);

	return 0;
}

static int rtw89_chip_board_info_setup(struct rtw89_dev *rtwdev)
{
	rtw89_chip_fem_setup(rtwdev);

	return 0;
}

int rtw89_chip_info_setup(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_read_chip_ver(rtwdev);

	ret = rtw89_chip_efuse_info_setup(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_chip_board_info_setup(rtwdev);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(rtw89_chip_info_setup);

static int rtw89_core_register_hw(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	int ret;

	hw->vif_data_size = sizeof(struct rtw89_vif);
	hw->sta_data_size = sizeof(struct rtw89_sta);
	hw->txq_data_size = sizeof(struct rtw89_txq);

	SET_IEEE80211_PERM_ADDR(hw, efuse->addr);

	hw->queues = IEEE80211_NUM_ACS;
	hw->max_rx_aggregation_subframes = RTW89_MAX_AGG_NUM;
	hw->max_tx_aggregation_subframes = RTW89_MAX_AGG_NUM;

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, RX_INCLUDES_FCS);
	ieee80211_hw_set(hw, TX_AMSDU);
	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

	hw->wiphy->features |= NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;

	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_CAN_REPLACE_PTK0);

	ret = rtw89_core_set_supported_band(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to set supported band\n");
		return ret;
	}

	hw->wiphy->reg_notifier = rtw89_regd_notifier;

	ret = ieee80211_register_hw(hw);
	if (ret) {
		rtw89_err(rtwdev, "failed to register hw\n");
		goto err;
	}

	ret = rtw89_regd_init(rtwdev, rtw89_regd_notifier);
	if (ret) {
		rtw89_err(rtwdev, "failed to init regd\n");
		goto err;
	}

	return 0;

err:
	return ret;
}

static void rtw89_core_unregister_hw(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;

	ieee80211_unregister_hw(hw);
	rtw89_core_clr_supported_band(rtwdev);
}

int rtw89_core_register(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_core_register_hw(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to register core hw\n");
		return ret;
	}

	rtw89_debugfs_init(rtwdev);

	return 0;
}
EXPORT_SYMBOL(rtw89_core_register);

void rtw89_core_unregister(struct rtw89_dev *rtwdev)
{
	rtw89_core_unregister_hw(rtwdev);
}
EXPORT_SYMBOL(rtw89_core_unregister);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless core module");
MODULE_LICENSE("Dual BSD/GPL");
