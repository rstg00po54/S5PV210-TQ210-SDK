/*******************************************
调试时间2013/05/19
过程:lcd 7寸屏（AT070TN92）裸板代码移植到TQ210中
修改的地方为
1:
【
#define HSPW 			(19)
#define HBPD			(26- 1)
#define HFPD 			(210- 1)
#define VSPW			(9)
#define VBPD 			(14 - 1)
#define VFPD 			(22 - 1)
】


2
【
// bit[6~13]:分频系数为5，即VCLK = 166M/(4+1) = 33.2MHz
	VIDCON0 |= (5<<6) | (1<<4);
（5<<6）
】
3:
[
	sp, =0x30000000	
]


作者：Mleaf HX
qq：350983773
注：本程序从友善裸板程序移植而来
版本 V2.0
********************************************/
#include "stdio.h"
#include "lcd.h"
#include "bmp.h"
#define GPC0CON   (*(volatile unsigned long *)0xE0200060)
#define	GPC0DAT  (*(volatile unsigned long *)0xE0200064)
void uart_init(void);
void  hx_delay(volatile unsigned long dly)
{
	for(; dly > 0; dly--);
	for(; dly > 0; dly--);
	for(; dly > 0; dly--);
}

int main(void)
{
	
	//初始化两个LED所在IO口 GPC0_3 GPC0_4
	GPC0CON &= ~(0xf<<12);//先将12-15位清零
	GPC0CON |= (1<<12);//0 4 8 12 16 将12位置1即[15:12]=0001 即GPC0_3为输出
	GPC0CON &= ~(0xf<<16);//0 4 8 12 16
	GPC0CON |= (1<<16);
	
	// 初始化串口
	uart_init();
	
	// 初始化LCD
	lcd_init();
	printf("Draw picture\r\n");
	// 画图
	lcd_clear_screen(0x0);
	lcd_draw_bmp(gImage_bmp);

	// 画字符
	printf("Mleaf-HX\r\n");
	
	for(;;)
	{
		GPC0DAT |= (1<<4);//点亮两个LED
		GPC0DAT |= (1<<3);
		hx_delay(300000);
		GPC0DAT &= ~(1<<4);//关闭两个LED
		GPC0DAT &= ~(1<<3);
		hx_delay(300000);
		
	}
}
