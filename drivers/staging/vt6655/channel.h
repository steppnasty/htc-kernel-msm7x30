/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: channel.h
 *
 */

#ifndef _CHANNEL_H_
#define _CHANNEL_H_

#include "ttype.h"
#include "card.h"

/*---------------------  Export Classes  ----------------------------*/

typedef struct tagSChannelTblElement {
    BYTE    byChannelNumber;
    unsigned int uFrequency;
    BOOL    bValid;
    BYTE    byMAP;
}SChannelTblElement, *PSChannelTblElement;


/*---------------------  Export Functions  --------------------------*/

BOOL is_channel_valid(unsigned int CountryCode);
void init_channel_table(void *pDeviceHandler);
BYTE get_channel_mapping(void *pDeviceHandler, BYTE byChannelNumber, CARD_PHY_TYPE ePhyType);
BOOL channel_get_list(unsigned int uCountryCodeIdx, unsigned char *pbyChannelTable);
BYTE get_channel_number(void *pDeviceHandler, BYTE byChannelIndex);
BOOL set_channel(void *pDeviceHandler, unsigned int uConnectionChannel);
void set_country_info(void *pDeviceHandler, CARD_PHY_TYPE ePHYType, void *pIE);
BYTE set_support_channels(void *pDeviceHandler, unsigned char *pbyIEs);
void set_country_IE(void *pDeviceHandler, void *pIE);
BOOL get_channel_map_info(void *pDeviceHandler, unsigned int uChannelIndex,
		unsigned char *pbyChannelNumber, unsigned char *pbyMap);
void set_channel_map_info(void *pDeviceHandler, unsigned int uChannelIndex,
		BYTE byMap);
void clear_channel_map_info(void *pDeviceHandler);
BYTE auto_channel_select(void *pDeviceHandler, CARD_PHY_TYPE ePHYType);


#endif /* _CHANNEL_H_ */
