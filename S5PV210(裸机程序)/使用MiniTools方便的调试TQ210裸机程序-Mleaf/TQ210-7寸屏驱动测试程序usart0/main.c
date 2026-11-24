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

版本 V2.0
********************************************/
#include "include/stdio.h"
#include "lcd.h"

void uart_init(void);

#define GPC0CON   (*(volatile unsigned long *)0xE0200060)
#define	GPC0DAT  (*(volatile unsigned long *)0xE0200064)
int main(void)
{
	int c = 0;

	// 初始化串口
	uart_init();

	// 初始化LCD
	lcd_init();
	lcd_clear_screen(0xff0000);// 红
	//点亮两个LED
	GPC0CON &= ~(0xf<<12);//先将12-15位清零
	GPC0CON |= (1<<12);//0 4 8 12 16 将12位置1即[15:12]=0001 即GPC0_3为输出
	GPC0DAT |= (1<<3);
	
	GPC0CON &= ~(0xf<<16);//0 4 8 12 16
	GPC0CON |= (1<<16);
	GPC0DAT |= (1<<4);
	// 打印菜单
	while(1)
	{
		printf("\r\n###############LCD Test Made By Mleaf HX########\r\n");
		printf("[1] lcd_clear_screen\r\n");
		printf("[2] lcd_draw_hline\r\n");
		printf("[3] lcd_draw_vline\r\n");
		printf("[4] lcd_draw_Rectangle\r\n");
		printf("[5] draw_Circle\r\n");
		printf("Enter your choice:");
		c = getc();
		printf("%c\r\n",c);
		switch(c)
		{
			case '1':
				// 清屏
				lcd_clear_screen(0x0000ff);	// 蓝
				break;
			case '2':
				// 划横线
				LCD_DrawLine(0, 400, 480,400,0xff0000);	// 红
				break;
			case '3':
				// 划竖线
				LCD_DrawLine(240, 0, 240, 800,0x00ff00);		// 绿
				break;
			case '4':
				//画方框
				lcd_draw_Rectangle(0,0,150,150,0xff0000);//红色
				break;
			case '5':
				//画圆
				 Draw_Circle(150,150,50,0xff0000);
				break;
			default:
				break;
		}
	}
	return 0;
}
