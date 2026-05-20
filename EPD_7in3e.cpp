/*****************************************************************************
* | File        :   EPD_7in3e.c
* | Author      :   Waveshare team
* | Function    :   7.3inch e-Paper (F) Driver
* | Info        :
*----------------
* | This version:   V1.0
* | Date        :   2022-10-20
* | Info        :
* -----------------------------------------------------------------------------
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "EPD_7in3e.h"

/******************************************************************************
function :  软件复位
parameter:
******************************************************************************/
static void EPD_7IN3E_Reset(void)
{
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(2);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
}

/******************************************************************************
function :  发送命令
parameter:
     Reg : 命令寄存器
******************************************************************************/
static void EPD_7IN3E_SendCommand(UBYTE Reg)
{
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Reg);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :  发送数据
parameter:
    Data : 写入数据
******************************************************************************/
static void EPD_7IN3E_SendData(UBYTE Data)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Data);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :  等待busy_pin变为高电平
parameter:
******************************************************************************/
static void EPD_7IN3E_ReadBusyH(void)
{
    Debug("e-Paper busy H\r\n");
    // #region agent log
    AGENT_DebugBusyWait("pre-fix", "H1", "EPD_7in3e.cpp:79", "busy_wait_start", 0);
    // #endregion
    unsigned long busyStart = millis();
    bool longWaitLogged = false;
    while(!DEV_Digital_Read(EPD_BUSY_PIN)) {      // 低电平：忙碌，高电平：空闲
        if (!longWaitLogged && (millis() - busyStart) >= 2000) {
            // #region agent log
            AGENT_DebugBusyWait("pre-fix", "H1", "EPD_7in3e.cpp:84", "busy_wait_over_2s", millis() - busyStart);
            // #endregion
            longWaitLogged = true;
        }
        DEV_Delay_ms(1);
    }
    Debug("e-Paper busy H release\r\n");
    // #region agent log
    AGENT_DebugBusyWait("pre-fix", "H1", "EPD_7in3e.cpp:90", "busy_wait_release", millis() - busyStart);
    // #endregion
}

/******************************************************************************
function :  开启显示
parameter:
******************************************************************************/
static void EPD_7IN3E_TurnOnDisplay(void)
{
    
    EPD_7IN3E_SendCommand(0x04); // 上电
    EPD_7IN3E_ReadBusyH();

    // 第二项设置
    EPD_7IN3E_SendCommand(0x06);
    EPD_7IN3E_SendData(0x6F);
    EPD_7IN3E_SendData(0x1F);
    EPD_7IN3E_SendData(0x17);
    EPD_7IN3E_SendData(0x49);

    EPD_7IN3E_SendCommand(0x12); // 显示刷新
    EPD_7IN3E_SendData(0x00);
    EPD_7IN3E_ReadBusyH();
    
    EPD_7IN3E_SendCommand(0x02); // 断电
    EPD_7IN3E_SendData(0X00);
    EPD_7IN3E_ReadBusyH();
}

/******************************************************************************
function :  初始化电子纸寄存器
parameter:
******************************************************************************/
void EPD_7IN3E_Init(void)
{
    // #region agent log
    AGENT_DebugPins("pre-fix", "H2", "EPD_7in3e.cpp:117", "epd_init_entry");
    // #endregion
    EPD_7IN3E_Reset();
    // #region agent log
    AGENT_DebugPins("pre-fix", "H2", "EPD_7in3e.cpp:120", "epd_init_after_reset");
    // #endregion
    EPD_7IN3E_ReadBusyH();
    DEV_Delay_ms(30);

    EPD_7IN3E_SendCommand(0xAA);    // 命令头
    EPD_7IN3E_SendData(0x49);
    EPD_7IN3E_SendData(0x55);
    EPD_7IN3E_SendData(0x20);
    EPD_7IN3E_SendData(0x08);
    EPD_7IN3E_SendData(0x09);
    EPD_7IN3E_SendData(0x18);

    EPD_7IN3E_SendCommand(0x01);//
    EPD_7IN3E_SendData(0x3F);

    EPD_7IN3E_SendCommand(0x00);  
    EPD_7IN3E_SendData(0x5F);
    EPD_7IN3E_SendData(0x69);

    EPD_7IN3E_SendCommand(0x03);
    EPD_7IN3E_SendData(0x00);
    EPD_7IN3E_SendData(0x54);
    EPD_7IN3E_SendData(0x00);
    EPD_7IN3E_SendData(0x44); 

    EPD_7IN3E_SendCommand(0x05);
    EPD_7IN3E_SendData(0x40);
    EPD_7IN3E_SendData(0x1F);
    EPD_7IN3E_SendData(0x1F);
    EPD_7IN3E_SendData(0x2C);

    EPD_7IN3E_SendCommand(0x06);
    EPD_7IN3E_SendData(0x6F);
    EPD_7IN3E_SendData(0x1F);
    EPD_7IN3E_SendData(0x17);
    EPD_7IN3E_SendData(0x49);

    EPD_7IN3E_SendCommand(0x08);
    EPD_7IN3E_SendData(0x6F);
    EPD_7IN3E_SendData(0x1F);
    EPD_7IN3E_SendData(0x1F);
    EPD_7IN3E_SendData(0x22);

    EPD_7IN3E_SendCommand(0x30);
    EPD_7IN3E_SendData(0x03);
    
    EPD_7IN3E_SendCommand(0x50);
    EPD_7IN3E_SendData(0x3F);

    EPD_7IN3E_SendCommand(0x60);
    EPD_7IN3E_SendData(0x02);
    EPD_7IN3E_SendData(0x00);

    EPD_7IN3E_SendCommand(0x61);
    EPD_7IN3E_SendData(0x03);
    EPD_7IN3E_SendData(0x20);
    EPD_7IN3E_SendData(0x01); 
    EPD_7IN3E_SendData(0xE0);

    EPD_7IN3E_SendCommand(0x84);
    EPD_7IN3E_SendData(0x01);

    EPD_7IN3E_SendCommand(0xE3);
    EPD_7IN3E_SendData(0x2F);

    EPD_7IN3E_SendCommand(0x04);     // 上电
    EPD_7IN3E_ReadBusyH();          // 等待电子纸IC释放空闲信号
    // #region agent log
    AGENT_DebugPins("pre-fix", "H4", "EPD_7in3e.cpp:188", "epd_init_power_on_complete");
    // #endregion

}

/******************************************************************************
function :  清屏
parameter:
******************************************************************************/
void EPD_7IN3E_Clear(UBYTE color)
{
    UWORD Width, Height;
    Width = (EPD_7IN3E_WIDTH % 2 == 0)? (EPD_7IN3E_WIDTH / 2 ): (EPD_7IN3E_WIDTH / 2 + 1);
    Height = EPD_7IN3E_HEIGHT;

    EPD_7IN3E_SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_7IN3E_SendData((color<<4)|color);
        }
    }

    EPD_7IN3E_TurnOnDisplay();
}

/******************************************************************************
function :  显示7种颜色块
parameter:
******************************************************************************/
void EPD_7IN3E_Show7Block(void)
{
    UWORD Width, Height;
    Width = (EPD_7IN3E_WIDTH % 2 == 0)? (EPD_7IN3E_WIDTH / 2 ): (EPD_7IN3E_WIDTH / 2 + 1);
    Height = EPD_7IN3E_HEIGHT;
    
    unsigned char const Color_seven[6] = 
    {EPD_7IN3E_BLACK, EPD_7IN3E_YELLOW, EPD_7IN3E_RED, EPD_7IN3E_BLUE, EPD_7IN3E_GREEN, EPD_7IN3E_WHITE};

    // 计算每种颜色应该占多少行（6种颜色平均分配480行）
    UWORD rowsPerColor = Height / 6;
    UBYTE colorByte = 0;

    EPD_7IN3E_SendCommand(0x10);
    
    // 遍历所有行
    for (UWORD j = 0; j < Height; j++) {
        // 确定当前行应该使用哪种颜色
        UWORD colorIndex = j / rowsPerColor;
        if (colorIndex >= 6) colorIndex = 5;  // 确保不越界
        
        colorByte = (Color_seven[colorIndex] << 4) | Color_seven[colorIndex];
        
        // 发送这一行的所有字节
        for (UWORD i = 0; i < Width; i++) {
            EPD_7IN3E_SendData(colorByte);
        }
    }
    
    EPD_7IN3E_TurnOnDisplay();
}

void EPD_7IN3E_Show(void)
{
    unsigned long k,o;
    unsigned char const Color_seven[6] = 
    {EPD_7IN3E_BLACK, EPD_7IN3E_YELLOW, EPD_7IN3E_RED, EPD_7IN3E_BLUE, EPD_7IN3E_GREEN, EPD_7IN3E_WHITE};

    UWORD Width, Height;
    Width = (EPD_7IN3E_WIDTH % 2 == 0)? (EPD_7IN3E_WIDTH / 2 ): (EPD_7IN3E_WIDTH / 2 + 1);
    Height = EPD_7IN3E_HEIGHT;
    k = 0;
    o = 0;

    EPD_7IN3E_SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        if((j > 10) && (j<50))
        for (UWORD i = 0; i < Width; i++) {
                EPD_7IN3E_SendData((Color_seven[0]<<4) |Color_seven[0]);
            }
        else if(o < Height/2)
        for (UWORD i = 0; i < Width; i++) {
                EPD_7IN3E_SendData((Color_seven[0]<<4) |Color_seven[0]);
            }
        
        else
        {
            for (UWORD i = 0; i < Width; i++) {
                EPD_7IN3E_SendData((Color_seven[k]<<4) |Color_seven[k]);
                
            }
            k++ ;
            if(k >= 6)
                k = 0;
        }
            
        o++ ;
        if(o >= Height)
            o = 0;
    }
    EPD_7IN3E_TurnOnDisplay();
}

/******************************************************************************
function :  将RAM中的图像缓冲区发送到电子纸并显示
parameter:
******************************************************************************/
void EPD_7IN3E_Display(UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_7IN3E_WIDTH % 2 == 0)? (EPD_7IN3E_WIDTH / 2 ): (EPD_7IN3E_WIDTH / 2 + 1);
    Height = EPD_7IN3E_HEIGHT;

    EPD_7IN3E_SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_7IN3E_SendData(Image[i + j * Width]);
        }
    }
    EPD_7IN3E_TurnOnDisplay();
}

void EPD_7IN3E_DisplayPart(const UBYTE *Image, UWORD xstart, UWORD ystart, UWORD image_width, UWORD image_heigh)
{
	unsigned long i, j;
	UWORD Width, Height;
	Width = (EPD_7IN3E_WIDTH % 2 == 0)? (EPD_7IN3E_WIDTH / 2 ): (EPD_7IN3E_WIDTH / 2 + 1);
	Height = EPD_7IN3E_HEIGHT;
	// #region agent log
	AGENT_DebugPins("pre-fix", "H4", "EPD_7in3e.cpp:313", "display_part_entry");
	// #endregion
	
	EPD_7IN3E_SendCommand(0x10);
	for(i=0; i<Height; i++) {
		for(j=0; j<Width; j++) {
			if(i<image_heigh+ystart && i>=ystart && j<(image_width+xstart)/2 && j>=xstart/2) {
				EPD_7IN3E_SendData(Image[(j-xstart/2) + (image_width/2*(i-ystart))]);
			}
			else {
				EPD_7IN3E_SendData(0x11);
			}
		}
	}
	EPD_7IN3E_TurnOnDisplay();
}

/******************************************************************************
function :  进入睡眠模式
parameter:
******************************************************************************/
void EPD_7IN3E_Sleep(void)
{
    EPD_7IN3E_SendCommand(0X02); // 深度睡眠
    EPD_7IN3E_SendData(0x00);
    EPD_7IN3E_ReadBusyH();

    EPD_7IN3E_SendCommand(0x07); // 深度睡眠
    EPD_7IN3E_SendData(0XA5);
}
