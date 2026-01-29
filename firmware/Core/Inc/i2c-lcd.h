#ifndef I2C_LCD_H_
#define I2C_LCD_H_

#include "main.h"

// Function Prototypes
void lcd_init (void);   // Initialize LCD
void lcd_send_cmd (char cmd);  // Send command to LCD
void lcd_send_data (char data);  // Send data to LCD
void lcd_send_string (char *str);  // Send string to LCD
void lcd_put_cur(int row, int col);  // Put cursor at the entered position row (0 or 1), col (0-15)
void lcd_clear (void);  // Clear LCD screen

#endif /* I2C_LCD_H_ */
