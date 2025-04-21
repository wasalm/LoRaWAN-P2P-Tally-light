#include "LoRaWanP2P.h"
#include "encryption.h"
#include <string.h>
#include <Arduino.h>

void LoRaWanP2P::onSave(void (*callback)())
{
    _onSave = callback;
}

void LoRaWanP2P::onJoin(void (*callback)())
{
    _onJoin = callback;
}

void LoRaWanP2P::onMessage(void (*callback)(uint8_t port, uint8_t *msg, uint8_t length))
{
    _onMessage = callback;
}

void LoRaWanP2P::onResponse(void (*callback)(uint8_t *buffer, uint8_t length, uint32_t rxDelay))
{
    _onResponse = callback;
}

void LoRaWanP2P::parseMessage(uint8_t *buffer, uint8_t length, int rssi, bool allowFCntReset)
{
    LoRaWanPHYPayload PHYPayload;

    if (!PHYPayload.populate(buffer, length))
    {
        // Invalid message, ignore
        return;
    }

    if (PHYPayload.mhdr == 0 && OTAAEnabled)
    {
        // Join Request
        _parseJoinRequest(&PHYPayload);
    }

    if (PHYPayload.isDataPackage)
    {
        _parseDataRequest(&PHYPayload, rssi, allowFCntReset);
    }
}

void LoRaWanP2P::_parseJoinRequest(LoRaWanPHYPayload *PHYPayload)
{
    LoRaWanJoinRequest request;
    if (!request.populate(PHYPayload->payload, PHYPayload->payloadLength))
    {
        // Invalid Payload
        return;
    }

    if (!_compare(&devEUI[0], &request.devEUI[0], 8))
    {
        // Message not for us. Ignore
        return;
    }

    if (!_compare(&appEUI[0], &request.appEUI[0], 8))
    {
        // Message not for us. Ignore
        return;
    }

    if (!PHYPayload->validateMIC(&appKey[0]))
    {
        // Invalid MIC ignore
        return;
    }

    LoRaWanJoinAccept accept;
    accept.appNonce[0] = random();
    accept.appNonce[1] = random();
    accept.appNonce[2] = random();

    accept.netID[0] = 0; // We don't have one
    accept.netID[0] = 0;
    accept.netID[0] = 0;

    accept.devAddr[0] = devAddr[0];
    accept.devAddr[1] = devAddr[1];
    accept.devAddr[2] = devAddr[2];
    accept.devAddr[3] = devAddr[3];

    accept.dLSettings = 0x00; // Set fixed at DR0 (not in use)
    accept.rxDelay = 0x01;    // Standard for Europe

    for (int i = 0; i < 16; i++) // Disable all remaining frequencies if possible
    {
        accept.cFList[i] = 0; // CF list
    }

    // Generate network keys and save settings
    _generateNwkSKey(&nwkSKey[0], &appKey[0], &accept.appNonce[0], &accept.netID[0], &request.devNonce[0]);
    _generateAppSKey(&appSKey[0], &appKey[0], &accept.appNonce[0], &accept.netID[0], &request.devNonce[0]);
    fCntDown = 0;
    fCntUp = 0;

    LoRaWanPHYPayload response;
    response.mhdr = 0x20; // Join Accept
    response.payloadLength = accept.toBuffer(&response.payload[0]);
    response.isDataPackage = false;
    response.generateMIC(&appKey[0]);

    uint8_t buf[64];
    uint8_t len = response.toBuffer(&buf[0]);

    AES_Decrypt(&buf[1], &appKey[0]);
    AES_Decrypt(&buf[17], &appKey[0]);

    // First send away as this is time critical
    if (_onResponse)
    {
        _onResponse(buf, len, 5000);
    }

    // Next Save data
    if (!_onSave)
    {
        _onSave();
    }

    // Give control back to user
    if (_onJoin)
    {
        _onJoin();
    }
}

void LoRaWanP2P::_parseDataRequest(LoRaWanPHYPayload *PHYPayload, int rssi, bool allowFCntReset)
{
    LoRaWanMACPayload macPayload;
    bool replay = false;
    bool linkCheck = false;
    bool toSave = false;

    if (!macPayload.populate(PHYPayload->payload, PHYPayload->payloadLength))
    {
        // Invalid Payload
        return;
    }

    if (!_compare(&devAddr[0], &macPayload.devAddr[0], 4))
    {
        // Message not for us. Ignore
        return;
    }

    uint32_t possibleFCnt = (fCntUp & 0xFFFF0000) | macPayload.fCnt;
    if (!PHYPayload->validateMIC(&nwkSKey[0], possibleFCnt))
    {
        possibleFCnt += (1 << 16);
        if (!PHYPayload->validateMIC(&nwkSKey[0], possibleFCnt))
        {
            if (allowFCntReset)
            {
                if (!PHYPayload->validateMIC(&nwkSKey[0], 0))
                {
                    // Invalid MIC, ignore message
                    return;
                }              
            }
        }
    }

    if(possibleFCnt == 0 && allowFCntReset) {
        fCntUp = 0;
        toSave = true;
    }

    if (fCntUp > possibleFCnt)
    {
        // Old message, ignore
        return;
    }

    if (fCntUp == possibleFCnt && fCntUp != 0)
    {
        replay = true; // We do answer this message, but we do not forward it to the user.
    }
    else
    {
        fCntUp = possibleFCnt;
        toSave = true;
    }

    if (macPayload.frmPayloadLength > 0)
    {
        // Decode packet. As one uses xor for encryption, the encode and decode function is identical.
        encodePacket(&macPayload.frmPayload[0],
                     macPayload.frmPayloadLength,
                     possibleFCnt,
                     &macPayload.devAddr[0],
                     &appSKey[0],
                     PHYPayload->mhdr != 0x40 && PHYPayload->mhdr != 0x80);
    }

    if (macPayload.fOptsLength != 0 && macPayload.fOpts[0] == 0x02)
    {
        linkCheck = true;
    }

    if (macPayload.frmPayloadLength > 0 && macPayload.fPort == 0 && macPayload.frmPayload[0] == 0x02)
    {
        linkCheck = true;
    }

    if (PHYPayload->mhdr == 0x80 || linkCheck)
    {
        // Endnode requests confirmation or a link check
        LoRaWanMACPayload responsePayload;

        responsePayload.devAddr[0] = devAddr[0];
        responsePayload.devAddr[1] = devAddr[1];
        responsePayload.devAddr[2] = devAddr[2];
        responsePayload.devAddr[3] = devAddr[3];

        responsePayload.adr = false; // Not implemented
        responsePayload.adrAckReq = false;
        responsePayload.ack = PHYPayload->mhdr == 0x80; // confirmed message

        fCntDown++;
        toSave = true;

        responsePayload.fCnt = fCntDown;

        if (linkCheck)
        {
            responsePayload.fOptsLength = 3;
            responsePayload.fOpts[0] = 0x02;

            int margin = rssi + 120; // Assume lowest is 120
            if (margin < 0)
            {
                responsePayload.fOpts[1] = 0;
            }
            else
            {
                responsePayload.fOpts[1] = margin;
            }

            responsePayload.fOpts[2] = 0x01; // only 1 gateway
        }
        else
        {
            responsePayload.fOptsLength = 0;
        }

        responsePayload.fPort = 0;
        responsePayload.frmPayloadLength = 0;

        LoRaWanPHYPayload response;
        response.mhdr = 0x60; // Unconfirmed data down
        response.payloadLength = responsePayload.toBuffer(&response.payload[0]);
        response.isDataPackage = true;
        response.generateMIC(&nwkSKey[0], fCntDown);

        // No contents to encrypt.

        // Send away
        if (_onResponse)
        {
            uint8_t buf[64];
            uint8_t len = response.toBuffer(&buf[0]);

            _onResponse(buf, len, 1000);
        }
    }

    if (toSave && _onSave)
    {
        _onSave();
    }

    if (!replay && macPayload.fPort != 0 && _onMessage)
    {
        _onMessage(macPayload.fPort, &macPayload.frmPayload[0], macPayload.frmPayloadLength);
    }

}

uint8_t LoRaWanPHYPayload::toBuffer(uint8_t *buf)
{
    buf[0] = mhdr;
    for (uint8_t i = 0; i < payloadLength; i++)
    {
        buf[1 + i] = payload[i];
    }

    for (uint8_t i = 0; i < 4; i++)
    {
        buf[1 + payloadLength + i] = mic[i];
    }

    return 5 + payloadLength;
}

uint8_t LoRaWanJoinAccept::toBuffer(uint8_t *buf)
{
    buf[0] = appNonce[2];
    buf[1] = appNonce[1];
    buf[2] = appNonce[0];

    buf[3] = netID[2];
    buf[4] = netID[1];
    buf[5] = netID[0];

    buf[6] = devAddr[3];
    buf[7] = devAddr[2];
    buf[8] = devAddr[1];
    buf[9] = devAddr[0];

    buf[10] = dLSettings;
    buf[11] = rxDelay;

    for (int i = 0; i < 16; i++)
    {
        buf[12 + i] = cFList[i];
    }

    return 28;
}

uint8_t LoRaWanMACPayload::toBuffer(uint8_t *buf)
{
    buf[0] = devAddr[3];
    buf[1] = devAddr[2];
    buf[2] = devAddr[1];
    buf[3] = devAddr[0];

    uint8_t fCtl = fOptsLength & 0x0f;
    if (adr)
    {
        fCtl |= 0x80;
    }

    if (ack)
    {
        fCtl |= 0x20;
    }

    if (pending)
    {
        fCtl |= 0x10;
    }

    buf[4] = fCtl;

    buf[5] = (fCnt & 0x00ff);
    buf[6] = ((fCnt >> 8) & 0x00FF);

    for (uint8_t i = 0; i < fOptsLength; i++)
    {
        buf[7 + i] = fOpts[i];
    }

    if (fPort == 0 && frmPayloadLength == 0)
    {
        return 7 + fOptsLength;
    }

    buf[7 + fOptsLength] = fPort;
    memcpy(&buf[8 + fOptsLength], &frmPayload[0], frmPayloadLength);

    return 8 + fOptsLength + frmPayloadLength;
}

bool LoRaWanP2P::_compare(uint8_t *a, uint8_t *b, uint8_t length)
{
    for (uint8_t i = 0; i < length; i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

bool LoRaWanPHYPayload::populate(uint8_t *buf, uint8_t length)
{
    if (length < 12 || length > 64) // Message is too small or too large
    {
        return false;
    }

    mhdr = buf[0];
    payloadLength = length - 5;

    memcpy(&mic[0], &buf[length - 4], 4);
    memset(&payload[0], 0, 64);
    memcpy(&payload, &buf[1], payloadLength);

    switch (mhdr)
    {
    case 0x00: // Join Request
    case 0x20: // Join Accept
        isDataPackage = false;
        break;
    case 0x40: // Unconfirmed Data Up
    case 0x60: // Unconfirmed Data Down
    case 0x80: // Confirmed Data Up
    case 0xa0: // Confirmed Data Down
        isDataPackage = true;
        break;

    default:
        // unsupported message
        return false;
        break;
    }

    return true;
}

bool LoRaWanMACPayload::populate(uint8_t *buf, uint8_t length)
{
    devAddr[3] = buf[0];
    devAddr[2] = buf[1];
    devAddr[1] = buf[2];
    devAddr[0] = buf[3];

    fOptsLength = buf[4] & 0x0f;
    pending = (buf[4] & 0x10) >> 4;
    ack = (buf[4] & 0x20) >> 5;
    adrAckReq = (buf[4] & 0x40) >> 6;
    adr = (buf[4] & 0x80) >> 7;

    fCnt = buf[6] << 8 | buf[5];

    memset(fOpts, 0, 16);

    if (length < 7 + fOptsLength)
    {
        // fOptsLength too large
        return false;
    }

    memcpy(fOpts, &buf[7], fOptsLength);

    fPort = 0;
    frmPayloadLength = length - 7 - fOptsLength;

    memset(frmPayload, 0, 64);
    if (frmPayloadLength > 0)
    {
        fPort = buf[7 + fOptsLength];
        frmPayloadLength--;
        memcpy(frmPayload, &buf[8 + fOptsLength], frmPayloadLength);
    }

    return true;
}

bool LoRaWanJoinRequest::populate(uint8_t *buf, uint8_t length)
{
    if (length != 18)
    {
        return false;
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        appEUI[7 - i] = buf[i];
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        devEUI[7 - i] = buf[8 + i];
    }

    devNonce[0] = buf[17];
    devNonce[1] = buf[16];

    return true;
}

void LoRaWanP2P::_generateNwkSKey(uint8_t *result, uint8_t *key, uint8_t *AppNonce, uint8_t *NetID, uint8_t *DevNonce)
{
    memset(result, 0, 16);
    result[0] = 0x01;
    result[1] = AppNonce[2];
    result[2] = AppNonce[1];
    result[3] = AppNonce[0];
    result[4] = NetID[2];
    result[5] = NetID[1];
    result[6] = NetID[0];
    result[7] = DevNonce[1];
    result[8] = DevNonce[0];

    AES_Encrypt(result, key);
    return;
}

void LoRaWanP2P::_generateAppSKey(uint8_t *result, uint8_t *key, uint8_t *AppNonce, uint8_t *NetID, uint8_t *DevNonce)
{
    memset(result, 0, 16);
    result[0] = 0x02;
    result[1] = AppNonce[2];
    result[2] = AppNonce[1];
    result[3] = AppNonce[0];
    result[4] = NetID[2];
    result[5] = NetID[1];
    result[6] = NetID[0];
    result[7] = DevNonce[1];
    result[8] = DevNonce[0];

    AES_Encrypt(result, key);
    return;
}

void LoRaWanPHYPayload::generateMIC(uint8_t *key)
{
    _generateMIC(key, &mic[0], 0);
}

void LoRaWanPHYPayload::generateMIC(uint8_t *key, uint32_t fCnt)
{
    _generateMIC(key, &mic[0], fCnt);
}

bool LoRaWanPHYPayload::validateMIC(uint8_t *key, uint32_t fCnt)
{
    uint8_t newMIC[] = {0, 0, 0, 0};

    _generateMIC(key, &newMIC[0], fCnt);

    return mic[0] == newMIC[0] &&
           mic[1] == newMIC[1] &&
           mic[2] == newMIC[2] &&
           mic[3] == newMIC[3];
}

bool LoRaWanPHYPayload::validateMIC(uint8_t *key)
{
    return validateMIC(key, 0);
}

void LoRaWanPHYPayload::_generateMIC(uint8_t *key, uint8_t *result, uint32_t fCnt)
{
    uint8_t buf[128];
    uint8_t len = 0;

    if (isDataPackage)
    {
        // B0 = ( 0x49 | 4 x 0x00 | Dir | 4 x DevAddr | 4 x FCnt |  0x00 | len )
        // MIC is cmac [0:3] of ( aes128_cmac(NwkSKey, B0 | Data )

        buf[0] = 0x49; // 1 byte MIC code

        buf[1] = 0x00; // 4 byte 0x00
        buf[2] = 0x00;
        buf[3] = 0x00;
        buf[4] = 0x00;

        if (mhdr == 0x40 || mhdr == 0x80)
        {
            buf[5] = 0;
        }
        else
        {
            buf[5] = 1;
        }

        // DevAddr
        buf[6] = payload[0];
        buf[7] = payload[1];
        buf[8] = payload[2];
        buf[9] = payload[3];

        buf[10] = (fCnt & 0x000000FF); // 4 byte FCNT
        buf[11] = ((fCnt >> 8) & 0x000000FF);
        buf[12] = ((fCnt >> 16) & 0x000000FF);
        buf[13] = ((fCnt >> 24) & 0x000000FF);

        buf[14] = 0x00; // 1 byte 0x00

        buf[15] = payloadLength + 1; // 1 byte len

        buf[16] = mhdr;

        memcpy(&buf[17], payload, payloadLength);
        len = payloadLength + 17;
    }

    // Join Request or Join Accept
    if (mhdr == 0x00 || mhdr == 0x20)
    {
        // MIC is cmac [0:3] of ( aes128_cmac(AppKey, Data )
        buf[0] = mhdr;
        memcpy(&buf[1], payload, payloadLength);
        len = payloadLength + 1;
    }

    uint8_t cmac[16];
    AES_CMAC(&buf[0], len, &cmac[0], key);
    memcpy(&result[0], &cmac[0], 4);

    return;
}
