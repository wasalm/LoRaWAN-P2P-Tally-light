//
//  crypto.c
//  lorawan
//
//  Copied from https://github.com/things4u/ESP-1ch-Gateway
//

// ----------------------------------------------------------------------------
// ENCODEPACKET
// In Sensor mode, we have to encode the user payload before sending.
// The same applies to decoding packages in the payload for _LOCALSERVER.
// The library files for AES are added to the library directory in AES.
// For the moment we use the AES library made by ideetron as this library
// is also used in the LMIC stack and is small in size.
//
// The function below follows the LoRa spec exactly.
//
// The resulting mumber of Bytes is returned by the functions. This means
// 16 bytes per block, and as we add to the last block we also return 16
// bytes for the last block.
//
// The LMIC code does not do this, so maybe we shorten the last block to only
// the meaningful bytes in the last block. This means that encoded buffer
// is exactly as big as the original message.
//
// NOTE:: Be aware that the LICENSE of the used AES library files
//    that we call with AES_Encrypt() is GPL3. It is used as-is,
//  but not part of this code.
//
// cmac = aes128_encrypt(K, Block_A[i])
//
// Parameters:
//    Data:        Data to encoide
//    DataLength:    Length of the data field
//    FrameCount:    xx
//    DevAddr:    Device ID
//    AppSKey:    Device specific
//    Direction:    Uplink==0, e.g. receivePakt(), semsorPacket(),
// ----------------------------------------------------------------------------

#include "encryption.h"
#include <AES.h>
#include <string.h>

void AES_Encrypt(uint8_t* data, uint8_t* key) {
    AESTiny128 aes;

    uint8_t result[16];

    aes.setKey(key, 16);
    aes.encryptBlock(&result[0], data);

    for(int i=0; i<16; i++) {
        data[i]=result[i];
    }    
} 

void AES_Decrypt(uint8_t* data, uint8_t* key) {
    AES128 aes;

    uint8_t result[16];

    aes.setKey(key, 16);
    aes.decryptBlock(&result[0], data);
    
    for(int i=0; i<16; i++) {
        data[i]=result[i];
    }    
} 

uint8_t encodePacket(uint8_t *Data, uint8_t DataLength, uint32_t FrameCount, uint8_t *DevAddr, uint8_t *AppSKey, uint8_t Direction)
{
    uint8_t i, j;
    uint8_t Block_A[16];
    uint8_t bLen=16;                        // Block length is 16 except for last block in message
        
    uint8_t restLength = DataLength % 16;    // We work in blocks of 16 bytes, this is the rest
    uint8_t numBlocks  = DataLength / 16;    // Number of whole blocks to encrypt
    if (restLength>0) numBlocks++;            // And add block for the rest if any

    for(i = 1; i <= numBlocks; i++) {
        Block_A[0] = 0x01;
        
        Block_A[1] = 0x00;
        Block_A[2] = 0x00;
        Block_A[3] = 0x00;
        Block_A[4] = 0x00;

        Block_A[5] = Direction;                // 0 is uplink

        Block_A[6] = DevAddr[3];            // Only works for and with ABP
        Block_A[7] = DevAddr[2];
        Block_A[8] = DevAddr[1];
        Block_A[9] = DevAddr[0];

        Block_A[10] = (FrameCount & 0x000000FF); // 4 byte FCNT
        Block_A[11] = ((FrameCount >> 8) & 0x000000FF);
        Block_A[12] = ((FrameCount >> 16) & 0x000000FF);
        Block_A[13] = ((FrameCount >> 24) & 0x000000FF);

        Block_A[14] = 0x00;

        Block_A[15] = i;

        // Encrypt and calculate the S
        AES_Encrypt(Block_A, AppSKey);
        
        // Last block? set bLen to rest
        if ((i == numBlocks) && (restLength>0)) bLen = restLength;
        
        for(j = 0; j < bLen; j++) {
            *Data = *Data ^ Block_A[j];
            Data++;
        }
    }

    //return(numBlocks*16);            // Do we really want to return all 16 bytes in lastblock
    return(DataLength);                // or only 16*(numBlocks-1)+bLen;
}

// ----------------------------------------------------------------------------
// XOR()
// perform x-or function for buffer and key
// Since we do this ONLY for keys and X, Y we know that we need to XOR 16 bytes.
//
// ----------------------------------------------------------------------------
void mXor(uint8_t *buf, uint8_t *key)
{
    for (uint8_t i = 0; i < 16; ++i) buf[i] ^= key[i];
}


// ----------------------------------------------------------------------------
// SHIFT-LEFT
// Shift the buffer buf left one bit
// Parameters:
//    - buf: An array of uint8_t bytes
//    - len: Length of the array in bytes
// ----------------------------------------------------------------------------
void shift_left(uint8_t * buf, uint8_t len)
{
    while (len--) {
        uint8_t next = len ? buf[1] : 0;            // len 0 to 15

        uint8_t val = (*buf << 1);
        if (next & 0x80) val |= 0x01;
        *buf++ = val;
    }
}


// ----------------------------------------------------------------------------
// generate_subkey
// RFC 4493, para 2.3
// -----------------------------------------------------------------------------
void generate_subkey(uint8_t *key, uint8_t *k1, uint8_t *k2)
{

    memset(k1, 0, 16);                                // Fill subkey1 with 0x00
    
    // Step 1: Assume k1 is an all zero block
    AES_Encrypt(k1,key);
    
    // Step 2: Analyse outcome of Encrypt operation (in k1), generate k1
    if (k1[0] & 0x80) {
        shift_left(k1,16);
        k1[15] ^= 0x87;
    }
    else {
        shift_left(k1,16);
    }
    
    // Step 3: Generate k2
    for (int i=0; i<16; i++) k2[i]=k1[i];
    
    if (k1[0] & 0x80) {                                // use k1(==k2) according rfc
        shift_left(k2,16);
        k2[15] ^= 0x87;
    }
    else {
        shift_left(k2,16);
    }
    
    // step 4: Done, return k1 and k2
    return;
}

void AES_CMAC(uint8_t *data, uint8_t len, uint8_t *result, uint8_t * key)
{
    uint8_t X[16];
    uint8_t Y[16];
    
    // ------------------------------------
    // Step 1: Generate the subkeys
    //
    uint8_t k1[16];
    uint8_t k2[16];
    generate_subkey(key, k1, k2);
    
    // ------------------------------------
    // Copy the data to a new buffer
    //
    uint8_t micBuf[len];                    // data
    for (uint8_t i=0; i<len; i++) micBuf[i]=data[i];
    
    // ------------------------------------
    // Step 2: Calculate the number of blocks for CMAC
    //
    uint8_t numBlocks = len/16;            // Compensate for B0 block
    if ((len % 16)!=0) numBlocks++;            // If we have only a part block, take it all
    
    // ------------------------------------
    // Step 3: Calculate padding is necessary
    //
    uint8_t restBits = len%16;                // if numBlocks is not a multiple of 16 bytes
    
    
    // ------------------------------------
    // Step 5: Make a buffer of zeros
    //
    memset(X, 0, 16);
    
    // ------------------------------------
    // Step 6: Do the actual encoding according to RFC
    //
    for(uint8_t i= 0x0; i < (numBlocks - 1); i++) {
        for (uint8_t j=0; j<16; j++) Y[j] = micBuf[(i*16)+j];
        mXor(Y, X);
        AES_Encrypt(Y, key);
        for (uint8_t j=0; j<16; j++) X[j] = Y[j];
    }
    

    // ------------------------------------
    // Step 4: If there is a rest Block, padd it
    // Last block. We move step 4 to the end as we need Y
    // to compute the last block
    //
    if (restBits) {
        for (uint8_t i=0; i<16; i++) {
            if (i< restBits) Y[i] = micBuf[((numBlocks-1)*16)+i];
            if (i==restBits) Y[i] = 0x80;
            if (i> restBits) Y[i] = 0x00;
        }
        mXor(Y, k2);
    }
    else {
        for (uint8_t i=0; i<16; i++) {
            Y[i] = micBuf[((numBlocks-1)*16)+i];
        }
        mXor(Y, k1);
    }
    mXor(Y, X);
    AES_Encrypt(Y,key);
    
    // ------------------------------------
    // Step 7: done, return the MIC size.
    // Only 4 bytes are returned (32 bits), which is less than the RFC recommends.
    // We return by appending 4 bytes to data, so there must be space in data array.
    //
    
    for(int i=0; i<16; i++) {
        result[i]=Y[i];
    }    
    
    return;
}
