//
//  crypto.h
//  lorawan
//
//  Copied from https://github.com/things4u/ESP-1ch-Gateway
//

#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <stdint.h>
void AES_Encrypt(uint8_t* data, uint8_t* key);
void AES_Decrypt(uint8_t* data, uint8_t* key);
uint8_t encodePacket(uint8_t *Data, uint8_t DataLength, uint32_t FrameCount, uint8_t *DevAddr, uint8_t *AppSKey, uint8_t Direction);
void AES_CMAC(uint8_t *data, uint8_t len, uint8_t *result, uint8_t * AppKey);

#else
#error "ENCRYPTION_H not defined"
#endif

//MIT License
//
//Copyright (c) 2016-2021 M. Westenberg / Things4U
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.
