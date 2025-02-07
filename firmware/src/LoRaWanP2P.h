#ifndef LORAPACKET_H
#define LORAPACKET_H

#include <stdbool.h>
#include <stdint.h>

class LoRaWanPHYPayload
{
public:   
    uint8_t mhdr;
    uint8_t payload[64];
    uint8_t payloadLength;
    uint8_t mic[4];

    bool isDataPackage;

    void generateMIC(uint8_t *key);
    void generateMIC(uint8_t *key, uint32_t fCnt);

    bool validateMIC(uint8_t *key);
    bool validateMIC(uint8_t *key, uint32_t fCnt);

    bool populate(uint8_t *buf, uint8_t length);
    uint8_t toBuffer(uint8_t *buf);

private:
    void _generateMIC(uint8_t *key, uint8_t *result, uint32_t fCnt);
};

class LoRaWanMACPayload
{
public: 
    uint8_t devAddr[4];
    
    bool adr;
    bool adrAckReq;
    bool ack;
    bool pending;

    uint16_t fCnt;
    uint8_t fOptsLength;
    uint8_t fOpts[16];

    int fPort;
    uint8_t frmPayloadLength;
    uint8_t frmPayload[64];
  
    bool populate(uint8_t *buf, uint8_t length);
    uint8_t toBuffer(uint8_t *buf);
};

class LoRaWanJoinRequest
{
public:   
    uint8_t appEUI[8];
    uint8_t devEUI[8];
    uint8_t devNonce[2];

    bool populate(uint8_t *buf, uint8_t length);
};

class LoRaWanJoinAccept
{
public:   
    uint8_t appNonce[3];
    uint8_t netID[3];
    uint8_t devAddr[4];
    uint8_t dLSettings;
    uint8_t rxDelay;
    uint8_t cFList[16];

    uint8_t toBuffer(uint8_t *buf);
};

class LoRaWanP2P
{
public:
    uint8_t devEUI[8];
    uint8_t appEUI[8];
    uint8_t appKey[16];

    uint8_t devAddr[4];
    uint8_t appSKey[16];
    uint8_t nwkSKey[16];

    uint32_t fCntDown;
    uint32_t fCntUp;

    bool OTAAEnabled = true;

    void onSave(void (*callback)());
    void onJoin(void (*callback)());
    void onMessage(void (*callback)(uint8_t port, uint8_t *msg, uint8_t length));
    void onResponse(void (*callback)(uint8_t *buffer, uint8_t length, uint32_t rxDelay));

    void parseMessage(uint8_t *buffer, uint8_t length, int rssi);

private:
    void (*_onSave)();
    void (*_onJoin)();
    void (*_onMessage)(uint8_t port, uint8_t *msg, uint8_t length);
    void (*_onResponse)(uint8_t *buffer, uint8_t length, uint32_t rxDelay);

    bool _compare(uint8_t *a, uint8_t *b, uint8_t length);
    void _generateNwkSKey(uint8_t *result, uint8_t *key, uint8_t *AppNonce, uint8_t *NetID, uint8_t *DevNonce);
    void _generateAppSKey(uint8_t *result, uint8_t *key, uint8_t *AppNonce, uint8_t *NetID, uint8_t *DevNonce);
    void _parseJoinRequest(LoRaWanPHYPayload * PHYPayload);
    void _parseDataRequest(LoRaWanPHYPayload * PHYPayload, int rssi);
};

#else
#error "LORAPACKET IS NOT DEFINED"
#endif
