/*****************************************************************************
* | File      	:	Debug.h
* | Author      :   Waveshare team
* | Function    :	使用printf调试
* | Info        :
*   图像扫描
*      请使用逐行扫描生成图像或字体
*----------------
* |	This version:   V1.0
* | Date        :   2018-01-11
* | Info        :   Basic version
*
******************************************************************************/
#ifndef __DEBUG_H
#define __DEBUG_H

#define USE_DEBUG 1
#if USE_DEBUG
	#define Debug(__info) Serial.print(__info)
#else
	#define Debug(__info)  
#endif

#endif
