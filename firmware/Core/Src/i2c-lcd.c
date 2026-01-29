#include "i2c-lcd.h"

extern I2C_HandleTypeDef hi2c2;  // Change this handler here if you are using hi2c2, etc.

// I2C Address calculation:
// 0x27 is the 7-bit address. HAL requires the 8-bit address (shifted left by 1).
#define SLAVE_ADDRESS_LCD (0x27 << 1)

void lcd_send_cmd (char cmd)
{
    char data_u, data_l;
    uint8_t data_t[4];
    data_u = (cmd&0xf0);
    data_l = ((cmd<<4)&0xf0);

    data_t[0] = data_u|0x0C;  // en=1, rs=0 -> bxxxx1100 (Backlight on)
    data_t[1] = data_u|0x08;  // en=0, rs=0 -> bxxxx1000
    data_t[2] = data_l|0x0C;  // en=1, rs=0 -> bxxxx1100
    data_t[3] = data_l|0x08;  // en=0, rs=0 -> bxxxx1000

    HAL_I2C_Master_Transmit (&hi2c2, SLAVE_ADDRESS_LCD,(uint8_t *) data_t, 4, 100);
}

void lcd_send_data (char data)
{
    char data_u, data_l;
    uint8_t data_t[4];
    data_u = (data&0xf0);
    data_l = ((data<<4)&0xf0);

    data_t[0] = data_u|0x0D;  // en=1, rs=1 -> bxxxx1101 (Backlight on)
    data_t[1] = data_u|0x09;  // en=0, rs=1 -> bxxxx1001
    data_t[2] = data_l|0x0D;  // en=1, rs=1 -> bxxxx1101
    data_t[3] = data_l|0x09;  // en=0, rs=1 -> bxxxx1001

    HAL_I2C_Master_Transmit (&hi2c2, SLAVE_ADDRESS_LCD,(uint8_t *) data_t, 4, 100);
}

void lcd_clear (void)
{
    lcd_send_cmd(0x01); // Send "Clear Display" command
    HAL_Delay(2);        // Clear command takes longer to process
}

void lcd_put_cur(int row, int col)
{
    switch (row)
    {
        case 0:
            col |= 0x80;
            break;
        case 1:
            col |= 0xC0;
            break;
    }
    lcd_send_cmd(col);
}

void lcd_init (void)
{
    // 4 bit initialisation
    HAL_Delay(50);  // wait for >40ms
    lcd_send_cmd(0x30);
    HAL_Delay(5);   // wait for >4.1ms
    lcd_send_cmd(0x30);
    HAL_Delay(1);   // wait for >100us
    lcd_send_cmd(0x30);
    HAL_Delay(10);
    lcd_send_cmd(0x20);  // 4bit mode
    HAL_Delay(10);

    // Display initialisation
    lcd_send_cmd(0x28); // Function set --> DL=0 (4 bit mode), N = 1 (2 line display) F = 0 (5x8 characters)
    HAL_Delay(1);
    lcd_send_cmd(0x08); // Display on/off control --> D=0,C=0, B=0  ---> display off
    HAL_Delay(1);
    lcd_send_cmd (0x01); // Clear display
    HAL_Delay(1);
    HAL_Delay(1);
    lcd_send_cmd(0x06); // Entry mode set --> I/D = 1 (increment cursor) & S = 0 (no shift)
    HAL_Delay(1);
    lcd_send_cmd(0x0C); // Display on/off control --> D = 1, C and B = 0. (Display ON, Cursor OFF, Blink OFF)
}

void lcd_send_string (char *str)
{
    while (*str) lcd_send_data (*str++);
}
