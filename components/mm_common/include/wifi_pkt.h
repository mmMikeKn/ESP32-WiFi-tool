#ifndef _WIFI_PKT_H
#define _WIFI_PKT_H

#include "esp_system.h"

/*
Capabilities Information: 0x0411
            .... .... .... ...1 = ESS capabilities: Transmitter is an AP
            .... .... .... ..0. = IBSS status: Transmitter belongs to a BSS
            .... ..0. .... 00.. = CFP participation capabilities: No point coordinator at AP (0x0000)
            .... .... ...1 .... = Privacy: AP/STA can support WEP
            .... .... ..0. .... = Short Preamble: Not Allowed
            .... .... .0.. .... = PBCC: Not Allowed
            .... .... 0... .... = Channel Agility: Not in use
            .... ...0 .... .... = Spectrum Management: Not Implemented
            .... .1.. .... .... = Short Slot Time: In use
            .... 0... .... .... = Automatic Power Save Delivery: Not Implemented
            ...0 .... .... .... = Radio Measurement: Not Implemented
            ..0. .... .... .... = DSSS-OFDM: Not Allowed
            .0.. .... .... .... = Delayed Block Ack: Not Implemented
            0... .... .... .... = Immediate Block Ack: Not Implemented
            0x0411
*/
typedef struct {
    uint16_t version:2; //It is a 2 bit long field which indicates the current protocol version which is fixed to be 0 for now.
    uint16_t type:2; // It is a 2 bit long field which determines the function of frame i.e management(00), control(01) or data(10). The value 11 is reserved.
    uint16_t subtype:4; //It is a 4 bit long field which indicates sub-type of the frame like 0000 for association request, 1000 for beacon.
    uint16_t to_ds:1; //It is a 1 bit long field which when set indicates that destination frame is for DS(distribution system).
    uint16_t from_ds:1; //It is a 1 bit long field which when set indicates frame coming from DS.
    uint16_t more_frag:1; // t is 1 bit long field which when set to 1 means frame is followed by other fragments.
    uint16_t retry:1; // It is 1 bit long field, if the current frame is a retransmission of an earlier frame, this bit is set to 1.
    uint16_t power_mgmt:1; //It is 1 bit long field which indicates the mode of a station after successful transmission of a frame. Set to 1 the field indicates that the station goes into power-save mode. If the field is set to 0, the station stays active.
    uint16_t more_data:1; // It is 1 bit long field which is used to indicates a receiver that a sender has more data to send than the current frame. This can be used by an access point to indicate to a station in power-save mode that more packets are buffered or it can be used by a station to indicate to an access point after being polled that more polling is necessary as the station has more data ready to transmit.
    uint16_t wep:1; // It is 1 bit long field which indicates that the standard security mechanism of 802.11 is applied.
    uint16_t order:1; // It is 1 bit long field, if this bit is set to 1 the received frames must be processed in strict order.
} wifi_frame_ctrl_t;

typedef struct {
    uint16_t fragment_num:4;
    uint16_t seq_num:12;
} wifi_seq_ctrl_t;

typedef struct {
    wifi_frame_ctrl_t frame_ctrl; //0b1000 - beacon
    uint16_t duration; // It is 4 bytes long field which contains the value indicating the period of time in which the medium is occupied(in µs).    
    uint8_t dest_address[6]; //Destination address (broadcast) 0xff..0ff
    uint8_t src_address[6]; // Source address
    uint8_t bssid[6]; 
    uint16_t seq_ctrl; //fragment number = 0
    // -- body --
    uint8_t timestamp[8]; // ESP32 = any. 
    uint16_t beacon_interval; //  Beacon Interval field represent the number of time units (TU) between  target beacon transmission times (TBTT). Default value is 100TU (102.4 milliseconds)
    uint16_t capability_info; // This field contains number of subfields that are used to indicate requested or advertised optional capabilities.
    uint8_t var_list[256]; // list
} wifi_beacon_body_t;

/* reason. 0..15  least significant -> Most significant bits
0 Reserved; unused
1 Unspecified
2 Prior authentication is not valid
3 Station has left the basic service area or extended service area and is deauthenticated
4 Inactivity timer expired and station was disassociated
5 Disassociated due to insufficient resources at the access point
6 Incorrect frame type or subtype received from unauthenticated station
7 Incorrect frame type or subtype received from unassociated station
8 Station has left the basic service area or extended service area and is disassociated
9 Association or reassociation requested before authentication is complete
10 (802.11h) Disassociated because of unacceptable values in Power Capability element
11 (802.11h) Disassociated because of unacceptable values in Supported Channels element
12 Reserved
13 (802.11i) Invalid information element (added with 802.11i, and likely one of the 802.11i information elements)
14 (802.11i) Message integrity check failure
15 (802.11i) 4-way keying handshake timeout
16 (802.11i) Group key handshake timeout
17 (802.11i) 4-way handshake information element has different security parameters from initial parameter set
18 (802.11i) Invalid group cipher 
19 (802.11i) Invalid pairwise cipher
20 (802.11i) Invalid Authentication and Key Management Protocol
21 (802.11i) Unsupported Robust Security Network Information Element (RSN IE) version
22 (802.11i) Invalid capabilities in RSN information element
23 (802.11i) 802.1X authentication failure
24 (802.11i) Proposed cipher suite rejected due to configured policy
25-65,535 Reserved; unused
*/
typedef struct {
    wifi_frame_ctrl_t frame_ctrl; // 0b1010 - disassociation, 0xb1100 - deauthentication
    uint16_t duration; // It is 4 bytes long field which contains the value indicating the period of time in which the medium is occupied(in µs).    
    uint8_t dest_address[6]; //Destination address (broadcast) 0xff..0ff
    uint8_t src_address[6]; // Source address
    uint8_t bssid[6]; 
    wifi_seq_ctrl_t seq_ctrl;
    // -- body --
    uint16_t reason; // 
} wifi_deauth_deaus_body_t;

typedef struct {
    wifi_frame_ctrl_t frame_ctrl; 
    uint16_t duration; // It is 4 bytes long field which contains the value indicating the period of time in which the medium is occupied(in µs).    
    uint8_t address1[6]; 
    uint8_t address2[6]; 
    uint8_t address3[6]; 
    wifi_seq_ctrl_t seq_ctrl; 
    uint8_t body[0]; 
} wifi_common_body_t;

typedef struct {
    uint8_t id;
    uint8_t len;
    uint8_t data[0];
} wifi_var_item_t;

#endif