/* main.c */
#include "main.h"
#include "mfrc522.h"
#include "at24cxx.h"
#include "i2c-lcd.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* --- Hardware Handles --- */
I2C_HandleTypeDef hi2c2;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart1;
MFRC522_HandleTypeDef rfid;
MFRC522_Key key;

/* --- Definitions --- */
#define DS3231_I2C_ADDR (0x68 << 1)
#define BTN_PREV_PIN GPIO_PIN_1
#define BTN_NEXT_PIN GPIO_PIN_2
#define BTN_PORT GPIOA

typedef struct __attribute__((packed)) {
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t uid[5];
    uint8_t status;
} RFID_Log;

#define LOG_SIZE sizeof(RFID_Log)

/* --- Function Prototypes --- */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C2_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
uint8_t MFRC522_ReadVersion(MFRC522_HandleTypeDef *dev);

/* --- RTC Helper Functions --- */
uint8_t bcd2dec(uint8_t b) { return ((b >> 4) * 10) + (b & 0x0F); }
uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

void DS3231_SetTime(void) {
    uint8_t buf[7];
    buf[0] = dec2bcd(0);  // Seconds
    buf[1] = dec2bcd(12); // Minutes
    buf[2] = dec2bcd(11); // Hours (24h format)
    buf[3] = dec2bcd(2);  // Day of week (Mon=1)
    buf[4] = dec2bcd(13); // Day
    buf[5] = dec2bcd(1);  // Month
    buf[6] = dec2bcd(26); // Year (2026)
    HAL_I2C_Mem_Write(&hi2c2, DS3231_I2C_ADDR, 0x00, 1, buf, 7, 100);
}

void DS3231_GetDateTime(RTC_TimeTypeDef *t, RTC_DateTypeDef *d) {
   uint8_t buf[7];
   HAL_I2C_Mem_Read(&hi2c2, DS3231_I2C_ADDR, 0x00, 1, buf, 7, 100);
   t->Seconds = bcd2dec(buf[0] & 0x7F);
   t->Minutes = bcd2dec(buf[1]);
   t->Hours   = bcd2dec(buf[2] & 0x3F);
   d->WeekDay = bcd2dec(buf[3]);
   d->Date    = bcd2dec(buf[4]);
   d->Month   = bcd2dec(buf[5] & 0x1F);
   d->Year    = bcd2dec(buf[6]);
}

/* --- Serial Helper Functions --- */
void PrintHex(uint8_t *data, uint8_t len) {
    char hexBuffer[4];
    for (int i = 0; i < len; i++) {
        sprintf(hexBuffer, "%02X ", data[i]);
        HAL_UART_Transmit(&huart1, (uint8_t*)hexBuffer, 3, 100);
    }
}

void PrintASCII(uint8_t *data, uint8_t len) {
    char asciiBuffer[2];
    asciiBuffer[1] = 0;
    for (int i = 0; i < len; i++) {
        if (isprint(data[i])) {
            asciiBuffer[0] = (char)data[i];
        } else {
            asciiBuffer[0] = '.';
        }
        HAL_UART_Transmit(&huart1, (uint8_t*)asciiBuffer, 1, 100);
    }
}

void PrintMsg(char *str) {
    HAL_UART_Transmit(&huart1, (uint8_t*)str, strlen(str), 100);
}

/* --- Logic Helper Functions --- */

// Check if UID exists in EEPROM
uint8_t Is_Card_Already_Logged(uint8_t* uid) {
    uint16_t logCount = 0;
    RFID_Log tempLog;

    AT24Cxx_ReadByte(0x0000, (uint8_t*)&logCount, 2);
    if (logCount == 0xFFFF) return 0;

    for (uint16_t i = 0; i < logCount; i++) {
        uint16_t readAddr = 2 + (i * LOG_SIZE);
        AT24Cxx_ReadByte(readAddr, (uint8_t*)&tempLog, LOG_SIZE);

        // Compare UIDs (assuming 4 or 5 byte UIDs)
        if (memcmp(tempLog.uid, uid, 5) == 0) {
            return 1; // Found duplicate
        }
    }
    return 0; // Not found
}

void Log_RFID_Event(uint8_t* uid, uint8_t status) {
    RFID_Log newLog;
    uint16_t logCount = 0;
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    // 1. Get Time
    DS3231_GetDateTime(&sTime, &sDate);

    // 2. Prepare Log
    newLog.year   = sDate.Year;
    newLog.month  = sDate.Month;
    newLog.day    = sDate.Date;
    newLog.hour   = sTime.Hours;
    newLog.minute = sTime.Minutes;
    newLog.second = sTime.Seconds;
    newLog.status = status;
    memcpy(newLog.uid, uid, 5);

    // 3. Read Current Count
    AT24Cxx_ReadByte(0x0000, (uint8_t*)&logCount, 2);
    if (logCount == 0xFFFF) logCount = 0;

    // 4. Calculate Address
    uint16_t writeAddr = 2 + (logCount * sizeof(RFID_Log));

    // Circular Buffer Logic
    if (writeAddr + sizeof(RFID_Log) >= 32768) { // Max size for AT24C256
        logCount = 0;
        writeAddr = 2;
    }

    // 5. Write Log Data
    AT24Cxx_WriteByte(writeAddr, (uint8_t*)&newLog, sizeof(RFID_Log));
    HAL_Delay(10); // CRITICAL: EEPROM Write Cycle Time

    // 6. Update Counter
    logCount++;
    AT24Cxx_WriteByte(0x0000, (uint8_t*)&logCount, 2);
    HAL_Delay(10); // CRITICAL: EEPROM Write Cycle Time
}

/* --- LCD Helper Functions --- */
void LCD_Show_Scan_Screen() {
    lcd_clear();
    lcd_put_cur(0, 0);
    lcd_send_string("Scan RFID Card..");

    // Optional: Show current time on second line
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    DS3231_GetDateTime(&sTime, &sDate);
    char timeStr[16];
    sprintf(timeStr, "%02d:%02d:%02d", sTime.Hours, sTime.Minutes, sTime.Seconds);
    lcd_put_cur(1, 4);
    lcd_send_string(timeStr);
}

void LCD_Show_Log(uint16_t index, uint16_t total) {
    RFID_Log tempLog;
    uint16_t readAddr = 2 + (index * LOG_SIZE);
    AT24Cxx_ReadByte(readAddr, (uint8_t*)&tempLog, LOG_SIZE);

    lcd_clear();

    char line1[20];
    char line2[20];

    // Line 1: Clean UID display
    sprintf(line1, "%d:%02X%02X%02X%02X",
            index + 1,
            tempLog.uid[0], tempLog.uid[1],
            tempLog.uid[2], tempLog.uid[3]);

    lcd_put_cur(0, 0);
    lcd_send_string(line1);

    // Line 2: Date and Time
    sprintf(line2, "D:%02d/%02d T:%02d:%02d",
            tempLog.day, tempLog.month,
            tempLog.hour, tempLog.minute);

    lcd_put_cur(1, 0);
    lcd_send_string(line2);
}


MFRC522_Status WriteToSpecificSectorBlock(MFRC522_HandleTypeDef *dev, uint8_t sector_num, uint8_t block_in_sector, uint8_t *data_ptr, MFRC522_Key *auth_key) {
    // 1. Calculate the absolute block address
    // Each sector has 4 blocks. Absolute Block = (Sector * 4) + Offset
    uint8_t absolute_block = (sector_num * 4) + block_in_sector;

    // 2. Determine the Trailer Block for authentication
    // Authentication is always done against the Sector Trailer (the 4th block of every sector)
    uint8_t trailer_block = (sector_num * 4) + 3;

    // 3. Authenticate the sector
    MFRC522_Status auth_status = MFRC522_Authenticate(dev, trailer_block, auth_key, &dev->uid);
    if (auth_status != MFRC522_OK) {
        return MFRC522_ERR;
    }

    // 4. Write the 16-byte data to the block
    MFRC522_Status write_status = MFRC522_WriteBlock(dev, absolute_block, data_ptr);

    return write_status;
}



/* --- MAIN --- */
int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C2_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  //commented out SetTime
  //DS3231_SetTime();

  // Initialize Peripherals
  lcd_init();
  LCD_Show_Scan_Screen();

  // CLEARING THE EXISTING LOGS =======================
  uint16_t zero = 0;
  AT24Cxx_WriteByte(0x0000, (uint8_t*)&zero, 2); // Reset count to 0

  uint8_t clearBuf[LOG_SIZE] = {0};
  for(int i=0; i<10; i++) {
      AT24Cxx_WriteByte(2 + (i * LOG_SIZE), clearBuf, LOG_SIZE);
  }
  //===============================

  rfid.hspi = &hspi1;
  rfid.cs_port = GPIOA;
  rfid.cs_pin = GPIO_PIN_4;
  rfid.rst_port = GPIOB;
  rfid.rst_pin = GPIO_PIN_0;
  MFRC522_Init(&rfid);

  // Initialize Key
  for (int i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  // Serial Debug Header
  PrintMsg("   MFRC522 System Ready           \r\n");
  PrintMsg("==================================\r\n");

  /* Variable to track UI State */
  int16_t viewIndex = -1;
  uint32_t lastInteractionTime = 0;
  uint8_t inViewMode = 0;
  const uint32_t DEBOUNCE_DELAY = 200; // 200ms debounce
  uint32_t lastDebounceTime = 0;

  while (1)
  {
      // --- PART 1: BUTTON LOGIC (View Logs) ---

	  uint32_t currentMillis = HAL_GetTick();

	      // --- PART 1: BUTTON LOGIC (Active Low: RESET = Pressed) ---
	      // Check if enough time has passed since last press (Debouncing)
	      if (currentMillis - lastDebounceTime > DEBOUNCE_DELAY)
	      {
	          uint8_t btnPrevPressed = (HAL_GPIO_ReadPin(BTN_PORT, BTN_PREV_PIN) == GPIO_PIN_RESET);
	          uint8_t btnNextPressed = (HAL_GPIO_ReadPin(BTN_PORT, BTN_NEXT_PIN) == GPIO_PIN_RESET);

	          if (btnPrevPressed || btnNextPressed)
	          {
	              lastDebounceTime = currentMillis;     // Reset debounce timer
	              lastInteractionTime = currentMillis;  // Reset inactivity timeout

	              // Read Total Logs
	              uint16_t totalLogs = 0;
	              AT24Cxx_ReadByte(0x0000, (uint8_t*)&totalLogs, 2);

	              if (totalLogs == 0xFFFF || totalLogs == 0) {
	                  lcd_clear();
	                  lcd_put_cur(0,0);
	                  lcd_send_string("No Logs Saved");
	                  HAL_Delay(1000);
	                  LCD_Show_Scan_Screen();
	                  inViewMode = 0;
	              }
	              else {
	                  inViewMode = 1; // Enter/Refresh View Mode

	                  // Logic for PREV Button
	                  if (btnPrevPressed) {
	                      if (viewIndex == -1) viewIndex = totalLogs - 1; // Initial entry
	                      else if (viewIndex == 0) viewIndex = totalLogs - 1; // Wrap to end
	                      else viewIndex--;
	                  }
	                  // Logic for NEXT Button
	                  else if (btnNextPressed) {
	                      if (viewIndex == -1) viewIndex = 0; // Initial entry
	                      else if (viewIndex >= totalLogs - 1) viewIndex = 0; // Wrap to start
	                      else viewIndex++;
	                  }

	                  LCD_Show_Log(viewIndex, totalLogs);
	              }
	          }
	      }

	      // Timeout Logic: Return to Scan Screen after 5 seconds of inactivity
	      if (inViewMode && (currentMillis - lastInteractionTime > 5000)) {
	          inViewMode = 0;
	          viewIndex = -1;
	          LCD_Show_Scan_Screen();
	      }

	      // If in View Mode, skip RFID scanning to prevent interference
	      if (inViewMode) continue;


	      // --- PART 2: RFID LOGIC (Unchanged) ---
	      if (!MFRC522_IsNewCardPresent(&rfid)) {
	          // Update time on LCD every second if idle
	          static uint32_t lastTimeUpdate = 0;
	          if (HAL_GetTick() - lastTimeUpdate > 1000) {
	              // Only update time if we are NOT in view mode
	              if(!inViewMode) LCD_Show_Scan_Screen();
	              lastTimeUpdate = HAL_GetTick();
	          }
	          continue;
	      }

	      if (!MFRC522_ReadCardSerial(&rfid)) {
	          continue;
	      }

	      //=============WRITE TO SECTOR AND BLOCK===================
	      uint8_t my_data[16] = "73611F90________"; // 16 bytes
	      MFRC522_Status write_result;

	      // Block 8 is Sector 2, Block 0
	      write_result = WriteToSpecificSectorBlock(&rfid, 2, 0, my_data, &key);
	      if (write_result == MFRC522_OK) {
	          PrintMsg("Write Successful!\r\n");
	      } else {
	          PrintMsg("Write Failed!\r\n");
	      }
	      //=============END OF WRITE TO SECTOR AND BLOCK===================


	      // [Rest of your RFID Save/Dump logic remains exactly the same...]
	      PrintMsg("\r\n[Card Detected] UID: ");
	      PrintHex(rfid.uid.uidByte, rfid.uid.size);
	      PrintMsg("\r\n");

	      if (Is_Card_Already_Logged(rfid.uid.uidByte)) {
	          lcd_clear();
	          lcd_put_cur(0, 1);
	          lcd_send_string("Already Logged");
	          PrintMsg("Status: Already Logged\r\n");
	          HAL_Delay(1500);
	      } else {
	          Log_RFID_Event(rfid.uid.uidByte, 1);
	          lcd_clear();
	          lcd_put_cur(0, 2);
	          lcd_send_string("Card Logged!");
	          PrintMsg("Status: New Event Logged\r\n");
	          HAL_Delay(1500);
	      }

	      LCD_Show_Scan_Screen();

      // --- DUMP SECTORS (Keep exactly as requested) ---
      for (int sector = 0; sector < 16; sector++) {
          int trailerBlock = (sector * 4) + 3;
          MFRC522_Status status = MFRC522_Authenticate(&rfid, trailerBlock, &key, &rfid.uid);
          if (status != MFRC522_OK) {
              char buf[30];
              sprintf(buf, "Sector %02d: Auth Failed\r\n", sector);
              PrintMsg(buf);
              continue;
          }
          for (int blockOffset = 0; blockOffset < 4; blockOffset++) {
              int currentBlock = (sector * 4) + blockOffset;
              uint8_t buffer[18];
              status = MFRC522_ReadBlock(&rfid, currentBlock, buffer);
              if (status == MFRC522_OK) {
                  char buf[20];
                  sprintf(buf, "  Block %02d: ", currentBlock);
                  PrintMsg(buf);
                  PrintHex(buffer, 16);
                  PrintMsg(" | ");
                  PrintASCII(buffer, 16);
                  PrintMsg("\r\n");
              } else {
                  PrintMsg("  Block Read Failed\r\n");
              }
          }
      }
      PrintMsg("--- End of Dump ---\r\n");

      MFRC522_Halt(&rfid);
      MFRC522_StopCrypto1(&rfid);
      HAL_Delay(100);
  }
}

/* --- System Configuration --- */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
}

static void MX_I2C2_Init(void) {
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x0010020A;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  HAL_I2C_Init(&hi2c2);
  HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE);
  HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0);
}

static void MX_SPI1_Init(void) {
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  HAL_SPI_Init(&hspi1);
}

static void MX_USART1_UART_Init(void) {
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  HAL_UART_Init(&huart1);
}

static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* --- SPI CS Pins Setup (Keep as is) --- */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* Configure GPIO pins : PA1 PA2 */
  GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}


void Error_Handler(void) {
  __disable_irq();
  while (1) {}
}
