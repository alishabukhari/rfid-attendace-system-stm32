/* file: mfrc522.h */
#ifndef MFRC522_H
#define MFRC522_H

#include "stm32f0xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* MFRC522 Registers */
#define PCD_IDLE       0x00
#define PCD_AUTHENT    0x0E
#define PCD_RECEIVE    0x08
#define PCD_TRANSMIT   0x04
#define PCD_TRANSCEIVE 0x0C
#define PCD_RESETPHASE 0x0F
#define PCD_CALCCRC    0x03

#define PICC_REQIDL    0x26
#define PICC_REQALL    0x52
#define PICC_ANTICOLL  0x93
#define PICC_SELC      0x93
#define PICC_AUTH1A    0x60
#define PICC_AUTH1B    0x61
#define PICC_READ      0x30
#define PICC_WRITE     0xA0
#define PICC_HALT      0x50

/* Status Enumerations */
typedef enum {
    MFRC522_OK = 0,
    MFRC522_ERR,
    MFRC522_TIMEOUT
} MFRC522_Status;

/* UID Struct */
typedef struct {
    uint8_t size;
    uint8_t uidByte[10];
    uint8_t sak;
} MFRC522_UID;

/* Key Struct */
typedef struct {
    uint8_t keyByte[6];
} MFRC522_Key;

/* Handle Struct */
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    GPIO_TypeDef *rst_port;
    uint16_t rst_pin;
    MFRC522_UID uid;
} MFRC522_HandleTypeDef;

/* Functions */
void MFRC522_Init(MFRC522_HandleTypeDef *dev);
bool MFRC522_IsNewCardPresent(MFRC522_HandleTypeDef *dev);
bool MFRC522_ReadCardSerial(MFRC522_HandleTypeDef *dev);
MFRC522_Status MFRC522_Authenticate(MFRC522_HandleTypeDef *dev, uint8_t blockAddr, MFRC522_Key *key, MFRC522_UID *uid);
MFRC522_Status MFRC522_ReadBlock(MFRC522_HandleTypeDef *dev, uint8_t blockAddr, uint8_t *buffer);
MFRC522_Status MFRC522_WriteBlock(MFRC522_HandleTypeDef *dev, uint8_t blockAddr, uint8_t *buffer);
void MFRC522_Halt(MFRC522_HandleTypeDef *dev);
void MFRC522_StopCrypto1(MFRC522_HandleTypeDef *dev);

#endif
