/******************************************************************************
* | File      	:   GUI_Paint.c
* | Author      :   Waveshare electronics
* | Function    :	Achieve drawing: draw points, lines, boxes, circles and
*                   their size, solid dotted line, solid rectangle hollow
*                   rectangle, solid circle hollow circle.
* | Info        :
*   Achieve display characters: Display a single character, string, number
*   Achieve time display: adaptive size display time minutes and seconds
*----------------
* |	This version:   V3.2
* | Date        :   2020-07-23
* | Info        :
* -----------------------------------------------------------------------------
* V3.2(2020-07-23):
* 1. Change: Paint_SetScale(UBYTE scale)
*			Add scale 7 for 5.65f e-Parper
* 2. Change: Paint_SetPixel(UWORD Xpoint, UWORD Ypoint, UWORD Color)  
* 			Add the branch for scale 7
* 3. Change: Paint_Clear(UWORD Color)
*			Add the branch for scale 7
*
* V3.1(2019-10-10):
* 1. Add gray level
*   PAINT Add Scale
* 2. Add void Paint_SetScale(UBYTE scale);
*
* V3.0(2019-04-18):
* 1.Change: 
*    Paint_DrawPoint(..., DOT_STYLE DOT_STYLE)
* => Paint_DrawPoint(..., DOT_STYLE Dot_Style)
*    Paint_DrawLine(..., LINE_STYLE Line_Style, DOT_PIXEL Dot_Pixel)
* => Paint_DrawLine(..., DOT_PIXEL Line_width, LINE_STYLE Line_Style)
*    Paint_DrawRectangle(..., DRAW_FILL Filled, DOT_PIXEL Dot_Pixel)
* => Paint_DrawRectangle(..., DOT_PIXEL Line_width, DRAW_FILL Draw_Fill)
*    Paint_DrawCircle(..., DRAW_FILL Draw_Fill, DOT_PIXEL Dot_Pixel)
* => Paint_DrawCircle(..., DOT_PIXEL Line_width, DRAW_FILL Draw_Filll)
*
* -----------------------------------------------------------------------------
* V2.0(2018-11-15):
* 1.add: Paint_NewImage()
*    Create an image's properties
* 2.add: Paint_SelectImage()
*    Select the picture to be drawn
* 3.add: Paint_SetRotate()
*    Set the direction of the cache    
* 4.add: Paint_RotateImage() 
*    Can flip the picture, Support 0-360 degrees, 
*    but only 90.180.270 rotation is better
* 4.add: Paint_SetMirroring() 
*    Can Mirroring the picture, horizontal, vertical, origin
* 5.add: Paint_DrawString_CN() 
*    Can display Chinese(GB1312)   
*
* ----------------------------------------------------------------------------- 
* V1.0(2018-07-17):
*   Create library
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documnetation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to  whom the Software is
* furished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*
******************************************************************************/
#include "GUI_Paint.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h> // memset()
#include <math.h>

PAINT Paint;

/******************************************************************************
function: 创建图像
parameter:
    image   :   指向图像缓存的指针
    width   :   图片的宽度
    Height  :   图片的高度
    Color   :   图片是否反转
******************************************************************************/
void Paint_NewImage(UBYTE *image, UWORD Width, UWORD Height, UWORD Rotate, UWORD Color)
{
    Paint.Image = NULL;
    Paint.Image = image;

    Paint.WidthMemory = Width;
    Paint.HeightMemory = Height;
    Paint.Color = Color;    
    Paint.Scale = 2;
    Paint.WidthByte = (Width % 8 == 0)? (Width / 8 ): (Width / 8 + 1);
    Paint.HeightByte = Height;    
//    printf("WidthByte = %d, HeightByte = %d\r\n", Paint.WidthByte, Paint.HeightByte);
//    printf(" EPD_WIDTH / 8 = %d\r\n",  122 / 8);
   
    Paint.Rotate = Rotate;
    Paint.Mirror = MIRROR_NONE;
    
    if(Rotate == ROTATE_0 || Rotate == ROTATE_180) {
        Paint.Width = Width;
        Paint.Height = Height;
    } else {
        Paint.Width = Height;
        Paint.Height = Width;
    }
}

/******************************************************************************
function: 选择图像
parameter:
    image : 指向图像缓存的指针
******************************************************************************/
void Paint_SelectImage(UBYTE *image)
{
    Paint.Image = image;
}

/******************************************************************************
function: 选择图像旋转
parameter:
    Rotate : 0,90,180,270
******************************************************************************/
void Paint_SetRotate(UWORD Rotate)
{
    if(Rotate == ROTATE_0 || Rotate == ROTATE_90 || Rotate == ROTATE_180 || Rotate == ROTATE_270) {
        // Debug("Set image Rotate %d\r\n", Rotate);
        Paint.Rotate = Rotate;
    } else {
        Debug("rotate = 0, 90, 180, 270\r\n");
    }
}

/******************************************************************************
function:	选择图像镜像
parameter:
    mirror   :不镜像，水平镜像，垂直镜像，原点镜像
******************************************************************************/
void Paint_SetMirroring(UBYTE mirror)
{
    if(mirror == MIRROR_NONE || mirror == MIRROR_HORIZONTAL || 
        mirror == MIRROR_VERTICAL || mirror == MIRROR_ORIGIN) {
        // Debug("mirror image x:%s, y:%s\r\n",(mirror & 0x01)? "mirror":"none", ((mirror >> 1) & 0x01)? "mirror":"none");
        Paint.Mirror = mirror;
    } else {
        Debug("mirror should be MIRROR_NONE, MIRROR_HORIZONTAL, \
        MIRROR_VERTICAL or MIRROR_ORIGIN\r\n");
    }    
}

void Paint_SetScale(UBYTE scale)
{
    if(scale == 2){
        Paint.Scale = scale;
        Paint.WidthByte = (Paint.WidthMemory % 8 == 0)? (Paint.WidthMemory / 8 ): (Paint.WidthMemory / 8 + 1);
    }
	else if(scale == 4) {
        Paint.Scale = scale;
        Paint.WidthByte = (Paint.WidthMemory % 4 == 0)? (Paint.WidthMemory / 4 ): (Paint.WidthMemory / 4 + 1);
    }
	else if(scale == 6 || scale == 7) {//仅适用于5.65寸电子纸
		Paint.Scale = 7;
		Paint.WidthByte = (Paint.WidthMemory % 2 == 0)? (Paint.WidthMemory / 2 ): (Paint.WidthMemory / 2 + 1);
	}
	else {
        Debug("Set Scale Input parameter error\r\n");
        Debug("Scale Only support: 2 4 7\r\n");
    }
}
/******************************************************************************
function: 绘制像素
parameter:
    Xpoint : X坐标点
    Ypoint : Y坐标点
    Color  : 绘制颜色
******************************************************************************/
void Paint_SetPixel(UWORD Xpoint, UWORD Ypoint, UWORD Color)
{
    if(Xpoint >= Paint.Width || Ypoint >= Paint.Height){
        Debug("Exceeding display boundaries\r\n");
        return;
    }      
    UWORD X, Y;
    switch(Paint.Rotate) {
    case 0:
        X = Xpoint;
        Y = Ypoint;  
        break;
    case 90:
        X = Paint.WidthMemory - Ypoint - 1;
        Y = Xpoint;
        break;
    case 180:
        X = Paint.WidthMemory - Xpoint - 1;
        Y = Paint.HeightMemory - Ypoint - 1;
        break;
    case 270:
        X = Ypoint;
        Y = Paint.HeightMemory - Xpoint - 1;
        break;
    default:
        return;
    }
    
    switch(Paint.Mirror) {
    case MIRROR_NONE:
        break;
    case MIRROR_HORIZONTAL:
        X = Paint.WidthMemory - X - 1;
        break;
    case MIRROR_VERTICAL:
        Y = Paint.HeightMemory - Y - 1;
        break;
    case MIRROR_ORIGIN:
        X = Paint.WidthMemory - X - 1;
        Y = Paint.HeightMemory - Y - 1;
        break;
    default:
        return;
    }

    if(X >= Paint.WidthMemory || Y >= Paint.HeightMemory){
        Debug("Exceeding display boundaries\r\n");
        return;
    }
    
    if(Paint.Scale == 2){
        UDOUBLE Addr = X / 8 + Y * Paint.WidthByte;
        UBYTE Rdata = Paint.Image[Addr];
        if(Color == BLACK)
            Paint.Image[Addr] = Rdata & ~(0x80 >> (X % 8));
        else
            Paint.Image[Addr] = Rdata | (0x80 >> (X % 8));
    }else if(Paint.Scale == 4){
        UDOUBLE Addr = X / 4 + Y * Paint.WidthByte;
        Color = Color % 4;//确保颜色级别为4  --- 0~3
        UBYTE Rdata = Paint.Image[Addr];
        
        Rdata = Rdata & (~(0xC0 >> ((X % 4)*2)));
        Paint.Image[Addr] = Rdata | ((Color << 6) >> ((X % 4)*2));
    }else if(Paint.Scale == 6 || Paint.Scale == 7 || Paint.Scale == 16){
		UDOUBLE Addr = X / 2  + Y * Paint.WidthByte;
		UBYTE Rdata = Paint.Image[Addr];
		Rdata = Rdata & (~(0xF0 >> ((X % 2)*4)));//先清除，再设置值
		Paint.Image[Addr] = Rdata | ((Color << 4) >> ((X % 2)*4));
		// printf("Add =  %d ,data = %d\r\n",Addr,Rdata);	
    }
}

/******************************************************************************
function: 清除图片颜色
parameter:
    Color : 绘制颜色
******************************************************************************/
void Paint_Clear(UWORD Color)
{
    if(Paint.Scale == 2) {
		for (UWORD Y = 0; Y < Paint.HeightByte; Y++) {
			for (UWORD X = 0; X < Paint.WidthByte; X++ ) {//8像素 = 1字节
				UDOUBLE Addr = X + Y*Paint.WidthByte;
				Paint.Image[Addr] = Color;
			}
		}
    }else if(Paint.Scale == 4) {
        for (UWORD Y = 0; Y < Paint.HeightByte; Y++) {
            for (UWORD X = 0; X < Paint.WidthByte; X++ ) {
                UDOUBLE Addr = X + Y*Paint.WidthByte;
                Paint.Image[Addr] = (Color<<6)|(Color<<4)|(Color<<2)|Color;
            }
        }
    }else if(Paint.Scale == 6 || Paint.Scale == 7 || Paint.Scale == 16) {
		for (UWORD Y = 0; Y < Paint.HeightByte; Y++) {
			for (UWORD X = 0; X < Paint.WidthByte; X++ ) {
				UDOUBLE Addr = X + Y*Paint.WidthByte;
				Paint.Image[Addr] = (Color<<4)|Color;
			}
		}		
	}
}

/******************************************************************************
function: 清除窗口颜色
parameter:
    Xstart : x起始点
    Ystart : Y起始点
    Xend   : x结束点
    Yend   : y结束点
    Color  : 绘制颜色
******************************************************************************/
void Paint_ClearWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD Color)
{
    UWORD X, Y;
    for (Y = Ystart; Y < Yend; Y++) {
        for (X = Xstart; X < Xend; X++) {//8像素 = 1字节
            Paint_SetPixel(X, Y, Color);
        }
    }
}

/******************************************************************************
function: 绘制点(Xpoint, Ypoint)并填充颜色
parameter:
    Xpoint		: 点的X坐标
    Ypoint		: 点的Y坐标
    Color		: 绘制颜色
    Dot_Pixel	: 点大小
    Dot_Style	: 点样式
******************************************************************************/
void Paint_DrawPoint(UWORD Xpoint, UWORD Ypoint, UWORD Color,
                     DOT_PIXEL Dot_Pixel, DOT_STYLE Dot_Style)
{
    if (Xpoint >= Paint.Width || Ypoint >= Paint.Height) {
        Debug("Paint_DrawPoint Input exceeds the normal display range\r\n");
        return;
    }

    int16_t XDir_Num , YDir_Num;
    if (Dot_Style == DOT_FILL_AROUND) {
        for (XDir_Num = 0; XDir_Num < 2 * Dot_Pixel - 1; XDir_Num++) {
            for (YDir_Num = 0; YDir_Num < 2 * Dot_Pixel - 1; YDir_Num++) {
                if(Xpoint + XDir_Num - Dot_Pixel < 0 || Ypoint + YDir_Num - Dot_Pixel < 0)
                    break;
                // printf("x = %d, y = %d\r\n", Xpoint + XDir_Num - Dot_Pixel, Ypoint + YDir_Num - Dot_Pixel);
                Paint_SetPixel(Xpoint + XDir_Num - Dot_Pixel, Ypoint + YDir_Num - Dot_Pixel, Color);
            }
        }
    } else {
        for (XDir_Num = 0; XDir_Num <  Dot_Pixel; XDir_Num++) {
            for (YDir_Num = 0; YDir_Num <  Dot_Pixel; YDir_Num++) {
                Paint_SetPixel(Xpoint + XDir_Num - 1, Ypoint + YDir_Num - 1, Color);
            }
        }
    }
}

/******************************************************************************
function: 绘制任意斜率的直线
parameter:
    Xstart ：起始X坐标点
    Ystart ：起始Y坐标点
    Xend   ：结束X坐标点
    Yend   ：结束Y坐标点
    Color  ：线段颜色
    Line_width : 线宽
    Line_Style: 实线和虚线
******************************************************************************/
void Paint_DrawLine(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend,
                    UWORD Color, DOT_PIXEL Line_width, LINE_STYLE Line_Style)
{
    if (Xstart >= Paint.Width || Ystart >= Paint.Height ||
        Xend >= Paint.Width || Yend >= Paint.Height) {
        Debug("Paint_DrawLine Input exceeds the normal display range\r\n");
        return;
    }

    UWORD Xpoint = Xstart;
    UWORD Ypoint = Ystart;
    int dx = (int)Xend - (int)Xstart >= 0 ? Xend - Xstart : Xstart - Xend;
    int dy = (int)Yend - (int)Ystart <= 0 ? Yend - Ystart : Ystart - Yend;

    // 增量方向，1为正，-1为负
    int XAddway = Xstart < Xend ? 1 : -1;
    int YAddway = Ystart < Yend ? 1 : -1;

    // 累积误差
    int Esp = dx + dy;
    char Dotted_Len = 0;

    for (;;) {
        Dotted_Len++;
        // 绘制虚线，2个点实1个点虚
        if (Line_Style == LINE_STYLE_DOTTED && Dotted_Len % 3 == 0) {
            //Debug("LINE_DOTTED\r\n");
            Paint_DrawPoint(Xpoint, Ypoint, IMAGE_BACKGROUND, Line_width, DOT_STYLE_DFT);
            Dotted_Len = 0;
        } else {
            Paint_DrawPoint(Xpoint, Ypoint, Color, Line_width, DOT_STYLE_DFT);
        }
        if (2 * Esp >= dy) {
            if (Xpoint == Xend)
                break;
            Esp += dy;
            Xpoint += XAddway;
        }
        if (2 * Esp <= dx) {
            if (Ypoint == Yend)
                break;
            Esp += dx;
            Ypoint += YAddway;
        }
    }
}

/******************************************************************************
function: 绘制矩形
parameter:
    Xstart ：矩形起始X坐标点
    Ystart ：矩形起始Y坐标点
    Xend   ：矩形结束X坐标点
    Yend   ：矩形结束Y坐标点
    Color  ：矩形线段颜色
    Line_width: 线宽
    Draw_Fill : 是否填充矩形内部
******************************************************************************/
void Paint_DrawRectangle(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend,
                         UWORD Color, DOT_PIXEL Line_width, DRAW_FILL Draw_Fill)
{
    if (Xstart >= Paint.Width || Ystart >= Paint.Height ||
        Xend >= Paint.Width || Yend >= Paint.Height) {
        Debug("Input exceeds the normal display range\r\n");
        return;
    }

    if (Draw_Fill) {
        UWORD Ypoint;
        for(Ypoint = Ystart; Ypoint < Yend; Ypoint++) {
            Paint_DrawLine(Xstart, Ypoint, Xend, Ypoint, Color , Line_width, LINE_STYLE_SOLID);
        }
    } else {
        Paint_DrawLine(Xstart, Ystart, Xend, Ystart, Color, Line_width, LINE_STYLE_SOLID);
        Paint_DrawLine(Xstart, Ystart, Xstart, Yend, Color, Line_width, LINE_STYLE_SOLID);
        Paint_DrawLine(Xend, Yend, Xend, Ystart, Color, Line_width, LINE_STYLE_SOLID);
        Paint_DrawLine(Xend, Yend, Xstart, Yend, Color, Line_width, LINE_STYLE_SOLID);
    }
}

/******************************************************************************
function: 使用8点法在指定位置绘制指定大小的圆
parameter:
    X_Center  ：圆心X坐标
    Y_Center  ：圆心Y坐标
    Radius    ：圆半径
    Color     ：圆线段颜色
    Line_width: 线宽
    Draw_Fill : 是否填充圆内部
******************************************************************************/
void Paint_DrawCircle(UWORD X_Center, UWORD Y_Center, UWORD Radius,
                      UWORD Color, DOT_PIXEL Line_width, DRAW_FILL Draw_Fill)
{
    if (X_Center >= Paint.Width || Y_Center >= Paint.Height) {
        Debug("Paint_DrawCircle Input exceeds the normal display range\r\n");
        return;
    }

    // 从(0, R)作为起点绘制圆
    int16_t XCurrent, YCurrent;
    XCurrent = 0;
    YCurrent = Radius;

    // 累积误差，判断下一个点的位置
    int16_t Esp = 3 - (Radius << 1 );

    int16_t sCountY;
    if (Draw_Fill == DRAW_FILL_FULL) {
        while (XCurrent <= YCurrent ) { // 实心圆
            for (sCountY = XCurrent; sCountY <= YCurrent; sCountY ++ ) {
                Paint_DrawPoint(X_Center + XCurrent, Y_Center + sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//1
                Paint_DrawPoint(X_Center - XCurrent, Y_Center + sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//2
                Paint_DrawPoint(X_Center - sCountY, Y_Center + XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//3
                Paint_DrawPoint(X_Center - sCountY, Y_Center - XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//4
                Paint_DrawPoint(X_Center - XCurrent, Y_Center - sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//5
                Paint_DrawPoint(X_Center + XCurrent, Y_Center - sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//6
                Paint_DrawPoint(X_Center + sCountY, Y_Center - XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//7
                Paint_DrawPoint(X_Center + sCountY, Y_Center + XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);
            }
            if (Esp < 0 )
                Esp += 4 * XCurrent + 6;
            else {
                Esp += 10 + 4 * (XCurrent - YCurrent );
                YCurrent --;
            }
            XCurrent ++;
        }
    } else { // 绘制空心圆
        while (XCurrent <= YCurrent ) {
            Paint_DrawPoint(X_Center + XCurrent, Y_Center + YCurrent, Color, Line_width, DOT_STYLE_DFT);//1
            Paint_DrawPoint(X_Center - XCurrent, Y_Center + YCurrent, Color, Line_width, DOT_STYLE_DFT);//2
            Paint_DrawPoint(X_Center - YCurrent, Y_Center + XCurrent, Color, Line_width, DOT_STYLE_DFT);//3
            Paint_DrawPoint(X_Center - YCurrent, Y_Center - XCurrent, Color, Line_width, DOT_STYLE_DFT);//4
            Paint_DrawPoint(X_Center - XCurrent, Y_Center - YCurrent, Color, Line_width, DOT_STYLE_DFT);//5
            Paint_DrawPoint(X_Center + XCurrent, Y_Center - YCurrent, Color, Line_width, DOT_STYLE_DFT);//6
            Paint_DrawPoint(X_Center + YCurrent, Y_Center - XCurrent, Color, Line_width, DOT_STYLE_DFT);//7
            Paint_DrawPoint(X_Center + YCurrent, Y_Center + XCurrent, Color, Line_width, DOT_STYLE_DFT);//0

            if (Esp < 0 )
                Esp += 4 * XCurrent + 6;
            else {
                Esp += 10 + 4 * (XCurrent - YCurrent );
                YCurrent --;
            }
            XCurrent ++;
        }
    }
}

/******************************************************************************
function: 显示英文字符
parameter:
    Xpoint           ：X坐标
    Ypoint           ：Y坐标
    Acsii_Char       ：要显示的英文字符
    Font             ：显示字符大小的结构体指针
    Color_Foreground : 选择前景色
    Color_Background : 选择背景色
******************************************************************************/
void Paint_DrawChar(UWORD Xpoint, UWORD Ypoint, const char Acsii_Char,
                    sFONT* Font, UWORD Color_Foreground, UWORD Color_Background)
{
    UWORD Page, Column;

    if (Xpoint >= Paint.Width || Ypoint >= Paint.Height) {
        Debug("Paint_DrawChar Input exceeds the normal display range\r\n");
        return;
    }

    uint32_t Char_Offset = (Acsii_Char - ' ') * Font->Height * (Font->Width / 8 + (Font->Width % 8 ? 1 : 0));
    const unsigned char *ptr = &Font->table[Char_Offset];

    for (Page = 0; Page < Font->Height; Page ++ ) {
        for (Column = 0; Column < Font->Width; Column ++ ) {

            // 判断字体背景色和屏幕背景色是否一致
            if (FONT_BACKGROUND == Color_Background) { // 此过程用于加速扫描
                if (*ptr & (0x80 >> (Column % 8)))
                    Paint_SetPixel(Xpoint + Column, Ypoint + Page, Color_Foreground);
                    // Paint_DrawPoint(Xpoint + Column, Ypoint + Page, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
            } else {
                if (*ptr & (0x80 >> (Column % 8))) {
                    Paint_SetPixel(Xpoint + Column, Ypoint + Page, Color_Foreground);
                    // Paint_DrawPoint(Xpoint + Column, Ypoint + Page, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                } else {
                    Paint_SetPixel(Xpoint + Column, Ypoint + Page, Color_Background);
                    // Paint_DrawPoint(Xpoint + Column, Ypoint + Page, Color_Background, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                }
            }
            // 一个像素是8位
            if (Column % 8 == 7)
                ptr++;
        }// 写入一行
        if (Font->Width % 8 != 0)
            ptr++;
    }// 写入全部
}

/******************************************************************************
function:	显示字符串
parameter:
    Xstart           ：X坐标
    Ystart           ：Y坐标
    pString          ：要显示的英文字符串的首地址
    Font             ：显示字符大小的结构体指针
    Color_Foreground : 选择前景色
    Color_Background : 选择背景色
******************************************************************************/
void Paint_DrawString_EN(UWORD Xstart, UWORD Ystart, const char * pString,
                         sFONT* Font, UWORD Color_Foreground, UWORD Color_Background)
{
    UWORD Xpoint = Xstart;
    UWORD Ypoint = Ystart;

    if (Xstart >= Paint.Width || Ystart >= Paint.Height) {
        Debug("Paint_DrawString_EN Input exceeds the normal display range\r\n");
        return;
    }

    while (* pString != '\0') {
        // 如果X方向已满，重新定位到(Xstart,Ypoint)，Ypoint是Y方向加上字符高度
        if ((Xpoint + Font->Width ) > Paint.Width ) {
            Xpoint = Xstart;
            Ypoint += Font->Height;
        }

        // 如果Y方向已满，重新定位到(Xstart, Ystart)
        if ((Ypoint  + Font->Height ) > Paint.Height ) {
            Xpoint = Xstart;
            Ypoint = Ystart;
        }
        Paint_DrawChar(Xpoint, Ypoint, * pString, Font, Color_Background, Color_Foreground);

        // 下一个字符的地址
        pString ++;

        // 下一个字符的横坐标增加字体的宽度
        Xpoint += Font->Width;
    }
}


/******************************************************************************
function: 显示字符串
parameter:
    Xstart  ：X坐标
    Ystart  ：Y坐标
    pString ：要显示的中文字符串和英文字符串的首地址
    Font    ：显示字符大小的结构体指针
    Color_Foreground : 选择前景色
    Color_Background : 选择背景色
******************************************************************************/
void Paint_DrawString_CN(UWORD Xstart, UWORD Ystart, const char * pString, cFONT* font,
                        UWORD Color_Foreground, UWORD Color_Background)
{
    const char* p_text = pString;
    int x = Xstart, y = Ystart;
    int i, j,Num;
    // for (size_t i = 0; p_text[i] != 0; i++)
    // {
    //     Serial.println(*(p_text+i)&0xff, HEX);
    //     Serial.println(i);
    //      if((*(p_text+i)&0xff) <= 0x7F)
    //      {
    //         Serial.println("111111");
    //      }
    //      else{
    //         Serial.println("222222");
    //      }
    // }
    

    /* 在电子纸上逐字符发送字符串 */
    while (*p_text != 0) {
        if((*p_text&0xff) <= 0x7F) {  // ASCII < 126
            for(Num = 0; Num < font->size; Num++) {
                if(*p_text== font->table[Num].index[0]) {
                    const unsigned char* ptr = &font->table[Num].matrix[0];

                    for (j = 0; j < font->Height; j++) {
                        for (i = 0; i < font->Width; i++) {
                            if (FONT_BACKGROUND == Color_Background) { // 此过程用于加速扫描
                                if (*ptr & (0x80 >> (i % 8))) {
                                    Paint_SetPixel(x + i, y + j, Color_Foreground);
                                    // Paint_DrawPoint(x + i, y + j, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                }
                            } else {
                                if (*ptr & (0x80 >> (i % 8))) {
                                    Paint_SetPixel(x + i, y + j, Color_Foreground);
                                    // Paint_DrawPoint(x + i, y + j, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                } else {
                                    Paint_SetPixel(x + i, y + j, Color_Background);
                                    // Paint_DrawPoint(x + i, y + j, Color_Background, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                }
                            }
                            if (i % 8 == 7) {
                                ptr++;
                            }
                        }
                        if (font->Width % 8 != 0) {
                            ptr++;
                        }
                    }
                    break;
                }
            }
            /* 指向下一个字符 */
            p_text += 1;
            /* 列位置增加ASCII宽度 */
            x += font->ASCII_Width;
        } else {        // 中文
            for(Num = 0; Num < font->size; Num++) {
                if ((((*p_text)&0xFF) == font->table[Num].index[0]) && \
                    (((*(p_text + 1))&0xFF) == font->table[Num].index[1]) && \
                    (((*(p_text + 2))&0xFF) == font->table[Num].index[2])) {
                    const unsigned char* ptr = &font->table[Num].matrix[0];

                    for (j = 0; j < font->Height; j++) {
                        for (i = 0; i < font->Width; i++) {
                            if (FONT_BACKGROUND == Color_Background) { // 此过程用于加速扫描
                                if (*ptr & (0x80 >> (i % 8))) {
                                    Paint_SetPixel(x + i, y + j, Color_Foreground);
                                    // Paint_DrawPoint(x + i, y + j, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                }
                            } else {
                                if (*ptr & (0x80 >> (i % 8))) {
                                    Paint_SetPixel(x + i, y + j, Color_Foreground);
                                    // Paint_DrawPoint(x + i, y + j, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                } else {
                                    Paint_SetPixel(x + i, y + j, Color_Background);
                                    // Paint_DrawPoint(x + i, y + j, Color_Background, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                                }
                            }
                            if (i % 8 == 7) {
                                ptr++;
                            }
                        }
                        if (font->Width % 8 != 0) {
                            ptr++;
                        }
                    }
                    break;
                }
            }
            /* 指向下一个字符 */
            p_text += 3;
            /* 列位置增加字体宽度 */
            x += font->Width;
        }
    }
}

/******************************************************************************
function:	显示数字
parameter:
    Xstart           ：X坐标
    Ystart           : Y坐标
    Nummber          : 要显示的数字
    Font             ：显示字符大小的结构体指针
    Color_Foreground : 选择前景色
    Color_Background : 选择背景色
******************************************************************************/
#define  ARRAY_LEN 255
void Paint_DrawNum(UWORD Xpoint, UWORD Ypoint, int32_t Nummber,
                   sFONT* Font, UWORD Color_Foreground, UWORD Color_Background)
{

    int16_t Num_Bit = 0, Str_Bit = 0;
    uint8_t Str_Array[ARRAY_LEN] = {0}, Num_Array[ARRAY_LEN] = {0};
    uint8_t *pStr = Str_Array;

    if (Xpoint >= Paint.Width || Ypoint >= Paint.Height) {
        Debug("Paint_DisNum Input exceeds the normal display range\r\n");
        return;
    }

    // 将数字转换为字符串
    while (Nummber) {
        Num_Array[Num_Bit] = Nummber % 10 + '0';
        Num_Bit++;
        Nummber /= 10;
    }

    // 字符串反转
    while (Num_Bit > 0) {
        Str_Array[Str_Bit] = Num_Array[Num_Bit - 1];
        Str_Bit ++;
        Num_Bit --;
    }

    // 显示
    Paint_DrawString_EN(Xpoint, Ypoint, (const char*)pStr, Font, Color_Background, Color_Foreground);
}

/******************************************************************************
function:	显示时间
parameter:
    Xstart           ：X坐标
    Ystart           : Y坐标
    pTime            : 时间相关结构体
    Font             ：显示字符大小的结构体指针
    Color_Foreground : 选择前景色
    Color_Background : 选择背景色
******************************************************************************/
void Paint_DrawTime(UWORD Xstart, UWORD Ystart, PAINT_TIME *pTime, sFONT* Font,
                    UWORD Color_Foreground, UWORD Color_Background)
{
    uint8_t value[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

    UWORD Dx = Font->Width;

    // 将数据写入缓存
    Paint_DrawChar(Xstart                           , Ystart, value[pTime->Hour / 10], Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx                      , Ystart, value[pTime->Hour % 10], Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx  + Dx / 4 + Dx / 2   , Ystart, ':'                    , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 2 + Dx / 2         , Ystart, value[pTime->Min / 10] , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 3 + Dx / 2         , Ystart, value[pTime->Min % 10] , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 4 + Dx / 2 - Dx / 4, Ystart, ':'                    , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 5                  , Ystart, value[pTime->Sec / 10] , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 6                  , Ystart, value[pTime->Sec % 10] , Font, Color_Background, Color_Foreground);
}

/******************************************************************************
function:	显示单色位图
parameter:
    image_buffer ：转换为位图的图片数据
info:
    使用计算机将图像转换为相应的数组，
    然后将数组直接嵌入到Imagedata.cpp作为.c文件。
******************************************************************************/
void Paint_DrawBitMap(const unsigned char* image_buffer)
{
    UWORD x, y;
    UDOUBLE Addr = 0;

    for (y = 0; y < Paint.HeightByte; y++) {
        for (x = 0; x < Paint.WidthByte; x++) {//8像素 = 1字节
            Addr = x + y * Paint.WidthByte;
            Paint.Image[Addr] = (unsigned char)image_buffer[Addr];
        }
    }
}

/******************************************************************************
function:	将单色位图粘贴到帧缓冲区
parameter:
    image_buffer ：转换为位图的图片数据
    xStart: 起始x坐标
    yStart: 起始y坐标
    imageWidth: 原始图像宽度
    imageHeight: 原始图像高度
    flipColor: 颜色是否反转
info:
    使用此函数将图像数据粘贴到缓冲区
******************************************************************************/
void Paint_DrawBitMap_Paste(const unsigned char* image_buffer, UWORD xStart, UWORD yStart, UWORD imageWidth, UWORD imageHeight, UBYTE flipColor)
{
    UBYTE color, srcImage;
    UWORD x, y;
    UWORD width = (imageWidth%8==0 ? imageWidth/8 : imageWidth/8+1);
    
    for (y = 0; y < imageHeight; y++) {
        for (x = 0; x < imageWidth; x++) {
            srcImage = image_buffer[y*width + x/8];
            if(flipColor)
                color = (((srcImage<<(x%8) & 0x80) == 0) ? 1 : 0);
            else
                color = (((srcImage<<(x%8) & 0x80) == 0) ? 0 : 1);
            Paint_SetPixel(x+xStart, y+yStart, color);
        }
    }
}

/******************************************************************************
function:	显示图像
parameter:
    image            ：图像起始地址
    xStart           : X起始坐标
    yStart           : Y起始坐标
    xEnd             ：图像宽度
    yEnd             : 图像高度
******************************************************************************/
void Paint_DrawImage(const unsigned char *image_buffer, UWORD xStart, UWORD yStart, UWORD W_Image, UWORD H_Image) 
{
    UWORD x, y;
	UWORD w_byte=(W_Image%8)?(W_Image/8)+1:W_Image/8;
    UDOUBLE Addr = 0;
	UDOUBLE pAddr = 0;
    for (y = 0; y < H_Image; y++) {
        for (x = 0; x < w_byte; x++) {//8像素 = 1字节
            Addr = x + y * w_byte;
			pAddr=x+(xStart/8)+((y+yStart)*Paint.WidthByte);
            Paint.Image[pAddr] = (unsigned char)image_buffer[Addr];
        }
    }
}
