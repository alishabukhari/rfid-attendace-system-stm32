/* file: mfrc522.c */
#include "mfrc522.h"
#include <string.h>

/* Registers */
#define CommandReg     0x01 << 1
#define ComIEnReg      0x02 << 1
#define DivIEnReg      0x03 << 1
#define ComIrqReg      0x04 << 1
#define DivIrqReg      0x05 << 1
#define ErrorReg       0x06 << 1
#define Status1Reg     0x07 << 1
#define Status2Reg     0x08 << 1
#define FIFODataReg    0x09 << 1
#define FIFOLevelReg   0x0A << 1
#define WaterLevelReg  0x0B << 1
#define ControlReg     0x0C << 1
#define BitFramingReg  0x0D << 1
#define CollReg        0x0E << 1

#define ModeReg        0x11 << 1
#define TxModeReg      0x12 << 1
#define RxModeReg      0x13 << 1
#define TxControlReg   0x14 << 1
#define TxASKReg       0x15 << 1
#define TxSelReg       0x16 << 1
#define RxSelReg       0x17 << 1
#define RxThresholdReg 0x18 << 1
#define DemodReg       0x19 << 1
#define RFCfgReg       0x26 << 1
#define GsNReg         0x27 << 1
#define CWGsPReg       0x28 << 1
#define ModGsPReg      0x29 << 1
#define TModeReg       0x2A << 1
#define TPrescalerReg  0x2B << 1
#define TReloadRegH    0x2C << 1
#define TReloadRegL    0x2D << 1
#define TCounterValueRegH 0x2E << 1
#define TCounterValueRegL 0x2F << 1

/* Added missing registers */
#define ModWidthReg    0x24 << 1
#define CRCResultRegM  0x21 << 1
#define CRCResultRegL  0x22 << 1
#define VersionReg     0x37 << 1

static void CS_LOW(MFRC522_HandleTypeDef *dev) {
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

static void CS_HIGH(MFRC522_HandleTypeDef *dev) {
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

void MFRC522_WriteRegister(MFRC522_HandleTypeDef *dev, uint8_t reg, uint8_t val) {
    uint8_t data[2] = { reg & 0x7E, val };
    CS_LOW(dev);
    HAL_SPI_Transmit(dev->hspi, data, 2, 10);
    CS_HIGH(dev);
}

static uint8_t ReadReg(MFRC522_HandleTypeDef *dev, uint8_t reg) {
    uint8_t tx = reg | 0x80;
    uint8_t rx;
    CS_LOW(dev);
    HAL_SPI_Transmit(dev->hspi, &tx, 1, 10);
    HAL_SPI_Receive(dev->hspi, &rx, 1, 10);
    CS_HIGH(dev);
    return rx;
}

static void AntennaOn(MFRC522_HandleTypeDef *dev) {
    uint8_t temp = ReadReg(dev, TxControlReg);
    if ((temp & 0x03) != 0x03) {
        MFRC522_WriteRegister(dev, TxControlReg, temp | 0x03);
    }
}

/* Helper to check communication */
uint8_t MFRC522_ReadVersion(MFRC522_HandleTypeDef *dev) {
    return ReadReg(dev, VersionReg);
}

void MFRC522_Init(MFRC522_HandleTypeDef *dev) {
    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_SET); // Ensure HIGH
    HAL_Delay(50);

    MFRC522_WriteRegister(dev, CommandReg, PCD_RESETPHASE);
    HAL_Delay(50);

    // Timer settings for 25ms timeout
    MFRC522_WriteRegister(dev, TModeReg, 0x80);
    MFRC522_WriteRegister(dev, TPrescalerReg, 0xA9);
    MFRC522_WriteRegister(dev, TReloadRegH, 0x03);
    MFRC522_WriteRegister(dev, TReloadRegL, 0xE8);

    MFRC522_WriteRegister(dev, TxASKReg, 0x40);
    MFRC522_WriteRegister(dev, ModeReg, 0x3D);

    // FORCE MAX GAIN
    MFRC522_WriteRegister(dev, RFCfgReg, 0x07 << 4);

    // Reset the internal phase of the antenna
    AntennaOn(dev);
}

static void MFRC522_CalculateCRC(MFRC522_HandleTypeDef *dev, uint8_t *pIndata, uint8_t len, uint8_t *pOutData) {
    MFRC522_WriteRegister(dev, CommandReg, PCD_IDLE);
    MFRC522_WriteRegister(dev, DivIrqReg, 0x04);
    MFRC522_WriteRegister(dev, FIFOLevelReg, 0x80);

    for (uint8_t i = 0; i < len; i++) {
        MFRC522_WriteRegister(dev, FIFODataReg, pIndata[i]);
    }
    MFRC522_WriteRegister(dev, CommandReg, PCD_CALCCRC);

    uint16_t i = 5000;
    uint8_t n;
    do {
        n = ReadReg(dev, DivIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x04));

    pOutData[0] = ReadReg(dev, CRCResultRegL);
    pOutData[1] = ReadReg(dev, CRCResultRegM);
}

MFRC522_Status MFRC522_ToCard(MFRC522_HandleTypeDef *dev, uint8_t cmd, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint16_t *backLen) {
    uint8_t status = MFRC522_ERR;
    uint8_t irqEn = 0x00;
    uint8_t waitIRq = 0x00;
    uint8_t lastBits;
    uint8_t n;
    uint32_t i;

    if (cmd == PCD_AUTHENT) {
        irqEn = 0x12;
        waitIRq = 0x10;
    } else if (cmd == PCD_TRANSCEIVE) {
        irqEn = 0x77;
        waitIRq = 0x30;
    }

    MFRC522_WriteRegister(dev, ComIEnReg, irqEn | 0x80);
    MFRC522_WriteRegister(dev, ComIrqReg, 0x7F);        // Clear all IRQ bits
    MFRC522_WriteRegister(dev, FIFOLevelReg, 0x80);     // Flush FIFO
    MFRC522_WriteRegister(dev, CommandReg, PCD_IDLE);   // Stop any active command

    // Writing data to FIFO
    for (i = 0; i < sendLen; i++) {
        MFRC522_WriteRegister(dev, FIFODataReg, sendData[i]);
    }

    // Execute command
    MFRC522_WriteRegister(dev, CommandReg, cmd);
    if (cmd == PCD_TRANSCEIVE) {
        MFRC522_WriteRegister(dev, BitFramingReg, ReadReg(dev, BitFramingReg) | 0x80); // StartSend
    }

    // Wait for completion
    i = 2000; // Adjusted timeout loop
    do {
        n = ReadReg(dev, ComIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

    MFRC522_WriteRegister(dev, BitFramingReg, ReadReg(dev, BitFramingReg) & (~0x80)); // StopSend

    if (i != 0) {
        if (!(ReadReg(dev, ErrorReg) & 0x1B)) { // Check for Errors (BufferOvfl, Collerr, CRCErr, ProtErr)
            status = MFRC522_OK;
            if (n & irqEn & 0x01) status = MFRC522_TIMEOUT;

            if (cmd == PCD_TRANSCEIVE) {
                n = ReadReg(dev, FIFOLevelReg);
                lastBits = ReadReg(dev, ControlReg) & 0x07;
                if (lastBits) *backLen = (n - 1) * 8 + lastBits;
                else *backLen = n * 8;

                if (n == 0) n = 1;
                if (n > 16) n = 16;

                // Read the resulting data from FIFO
                for (i = 0; i < n; i++) {
                    backData[i] = ReadReg(dev, FIFODataReg);
                }
                backData[n] = 0; // Null terminate for safety
            }
        }
    }
    return status;
}

bool MFRC522_IsNewCardPresent(MFRC522_HandleTypeDef *dev) {
    uint8_t buffer[2];
    uint16_t len;

    MFRC522_WriteRegister(dev, TxModeReg, 0x00);
    MFRC522_WriteRegister(dev, RxModeReg, 0x00);
    //MFRC522_WriteRegister(dev, ModWidthReg, 0x26);

    buffer[0] = PICC_REQIDL;
    MFRC522_WriteRegister(dev, BitFramingReg, 0x07);

    MFRC522_Status status = MFRC522_ToCard(dev, PCD_TRANSCEIVE, buffer, 1, buffer, &len);

    if (status == MFRC522_OK) {
        // If we get here, the card finally talked back!
        return true;
    }

    return false;
}

bool MFRC522_SelectTag(MFRC522_HandleTypeDef *dev) {
    uint8_t buffer[9];
    uint16_t len = 0;

    buffer[0] = PICC_SELC;
    buffer[1] = 0x70;

    buffer[2] = dev->uid.uidByte[0];
    buffer[3] = dev->uid.uidByte[1];
    buffer[4] = dev->uid.uidByte[2];
    buffer[5] = dev->uid.uidByte[3];

    buffer[6] = buffer[2] ^ buffer[3] ^ buffer[4] ^ buffer[5];

    MFRC522_CalculateCRC(dev, buffer, 7, &buffer[7]);

    MFRC522_Status status = MFRC522_ToCard(dev, PCD_TRANSCEIVE, buffer, 9, buffer, &len);

    if (status == MFRC522_OK && len == 0x18) {
        dev->uid.sak = buffer[0];
        return true;
    }
    return false;
}

bool MFRC522_ReadCardSerial(MFRC522_HandleTypeDef *dev) {
    uint8_t buffer[9];
    uint16_t len;

    MFRC522_WriteRegister(dev, BitFramingReg, 0x00);
    buffer[0] = PICC_ANTICOLL;
    buffer[1] = 0x20;

    MFRC522_Status status = MFRC522_ToCard(dev, PCD_TRANSCEIVE, buffer, 2, buffer, &len);

    if (status == MFRC522_OK) {
        for(int i=0; i<4; i++) dev->uid.uidByte[i] = buffer[i];
        dev->uid.size = 4;

        if (MFRC522_SelectTag(dev)) {
            return true;
        }
    }
    return false;
}

MFRC522_Status MFRC522_Authenticate(MFRC522_HandleTypeDef *dev, uint8_t blockAddr, MFRC522_Key *key, MFRC522_UID *uid) {
    uint8_t buff[12];
    uint16_t len;

    buff[0] = PICC_AUTH1A;
    buff[1] = blockAddr;
    memcpy(&buff[2], key->keyByte, 6);
    memcpy(&buff[8], uid->uidByte, 4);

    MFRC522_ToCard(dev, PCD_AUTHENT, buff, 12, NULL, &len);

    if ((ReadReg(dev, Status2Reg) & 0x08) == 0) {
        return MFRC522_ERR;
    }
    return MFRC522_OK;
}

MFRC522_Status MFRC522_ReadBlock(MFRC522_HandleTypeDef *dev, uint8_t blockAddr, uint8_t *buffer) {
    uint8_t buf[4];
    uint16_t len;

    buf[0] = PICC_READ;
    buf[1] = blockAddr;
    MFRC522_CalculateCRC(dev, buf, 2, &buf[2]);

    MFRC522_Status status = MFRC522_ToCard(dev, PCD_TRANSCEIVE, buf, 4, buffer, &len);

    if (status != MFRC522_OK || len != 0x90) {
        return MFRC522_ERR;
    }
    return MFRC522_OK;
}

MFRC522_Status MFRC522_WriteBlock(MFRC522_HandleTypeDef *dev, uint8_t blockAddr, uint8_t *buffer) {
    uint8_t buf[18];
    uint16_t len;

    buf[0] = PICC_WRITE;
    buf[1] = blockAddr;
    MFRC522_CalculateCRC(dev, buf, 2, &buf[2]);

    if (MFRC522_ToCard(dev, PCD_TRANSCEIVE, buf, 4, buf, &len) != MFRC522_OK) return MFRC522_ERR;

    memcpy(buf, buffer, 16);
    MFRC522_CalculateCRC(dev, buf, 16, &buf[16]);

    if (MFRC522_ToCard(dev, PCD_TRANSCEIVE, buf, 18, buf, &len) != MFRC522_OK) return MFRC522_ERR;

    return MFRC522_OK;
}

void MFRC522_Halt(MFRC522_HandleTypeDef *dev) {
    uint16_t unLen;
    uint8_t buff[4];
    buff[0] = PICC_HALT;
    buff[1] = 0;
    MFRC522_CalculateCRC(dev, buff, 2, &buff[2]);
    MFRC522_ToCard(dev, PCD_TRANSCEIVE, buff, 4, buff, &unLen);
}

void MFRC522_StopCrypto1(MFRC522_HandleTypeDef *dev) {
     MFRC522_WriteRegister(dev, Status2Reg, 0x00);
}
