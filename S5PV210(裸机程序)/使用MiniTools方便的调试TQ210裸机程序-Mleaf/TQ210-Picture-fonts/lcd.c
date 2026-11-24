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
#include "font_8x16.h"

/*LCD相关寄存器的定义*/
#define GPF0CON			(*(volatile unsigned long *)0xE0200120)
#define GPF1CON			(*(volatile unsigned long *)0xE0200140)
#define GPF2CON			(*(volatile unsigned long *)0xE0200160)
#define GPF3CON			(*(volatile unsigned long *)0xE0200180)

#define GPD0CON			(*(volatile unsigned long *)0xE02000A0)
#define GPD0DAT			(*(volatile unsigned long *)0xE02000A4)

#define CLK_SRC1		(*(volatile unsigned long *)0xe0100204)
#define CLK_DIV1		(*(volatile unsigned long *)0xe0100304)
#define DISPLAY_CONTROL	(*(volatile unsigned long *)0xe0107008)

#define VIDCON0			(*(volatile unsigned long *)0xF8000000)
#define VIDCON1			(*(volatile unsigned long *)0xF8000004)
#define VIDTCON2		(*(volatile unsigned long *)0xF8000018)
#define WINCON0 		(*(volatile unsigned long *)0xF8000020)
#define WINCON2 		(*(volatile unsigned long *)0xF8000028)
#define SHADOWCON 		(*(volatile unsigned long *)0xF8000034)
#define VIDOSD0A 		(*(volatile unsigned long *)0xF8000040)
#define VIDOSD0B 		(*(volatile unsigned long *)0xF8000044)
#define VIDOSD0C 		(*(volatile unsigned long *)0xF8000048)

#define VIDW00ADD0B0 	(*(volatile unsigned long *)0xF80000A0)
#define VIDW00ADD1B0 	(*(volatile unsigned long *)0xF80000D0)

#define VIDTCON0 		(*(volatile unsigned long *)0xF8000010)
#define VIDTCON1 		(*(volatile unsigned long *)0xF8000014)
//显示参数宏定义
#define HSPW 			(19)
#define HBPD			(26- 1)
#define HFPD 			(210- 1)
#define VSPW			(9)
#define VBPD 			(14 - 1)
#define VFPD 			(22 - 1)
/*使用下面的时序显示会不完全
#define HSPW 			(39)//thpw
#define HBPD			(46 - 1)//thb
#define HFPD 			(210- 1)//thfp
#define VSPW			(19)//tvpw
#define VBPD 			(23 - 1)//tvb
#define VFPD 			(22 - 1)//tvfp*/

// FB地址
#define FB_ADDR			(0x23000000)
#define ROW			(480)
#define COL			(800)
#define HOZVAL			(COL-1)
#define LINEVAL			(ROW-1)
//显示位置设定
#define LeftTopX     0
#define LeftTopY     0
#define RightBotX   799
#define RightBotY   479

//定义要显示图片的大小
#define HEIGHT	480
#define WIDTHEIGHT	800

// 初始化LCD
void lcd_init(void)
{
	// 配置引脚用于LCD功能
	GPF0CON = 0x22222222;
	GPF1CON = 0x22222222;
	GPF2CON = 0x22222222;
	GPF3CON = 0x22222222;

	// 打开背光GPD0_0
	GPD0CON &= ~(0xf<<0);//先将4-0位清零
	GPD0CON |= (1<<0);
	GPD0DAT |= (1<<0);

	// 10: RGB=FIMD I80=FIMD ITU=FIMD
	DISPLAY_CONTROL = 2<<0;

	// bit[26~28]:使用RGB接口
	// bit[18]:RGB 并行
	// bit[2]:选择时钟源为HCLK_DSYS=166MHz
	VIDCON0 &= ~( (3<<26)|(1<<18)|(1<<2) );

	// bit[1]:使能lcd控制器
	// bit[0]:当前帧结束后使能lcd控制器
	VIDCON0 |= ( (1<<0)|(1<<1) );

	// bit[6]:选择需要分频
	// bit[6~13]:分频系数为5，即VCLK = 166M/(4+1) = 33.2MHz
	VIDCON0 |= (5<<6) | (1<<4);


	// AT070TN92.pdf(p14) Timing：VSYNC和HSYNC都是低脉冲
	// s5pv210芯片手册(p1208) 时序图：VSYNC和HSYNC都是高脉冲有效，所以需要反转
	VIDCON1 |= 1<<5 | 1<<6;

	// 设置时序
	VIDTCON0 = VBPD<<16 | VFPD<<8 | VSPW<<0;
	VIDTCON1 = HBPD<<16 | HFPD<<8 | HSPW<<0;
	// 设置长宽
	VIDTCON2 = (LINEVAL << 11) | (HOZVAL << 0);

	// 设置windows1
	// bit[0]:使能
	// bit[2~5]:24bpp
	WINCON0 |= 1<<0;
	WINCON0 &= ~(0xf << 2);
	WINCON0 |= (0xB<<2) | (1<<15);



	// 设置windows1的上下左右
	VIDOSD0A = (LeftTopX<<11) | (LeftTopY << 0);
	VIDOSD0B = (RightBotX<<11) | (RightBotY << 0);
	VIDOSD0C = (LINEVAL + 1) * (HOZVAL + 1);


	// 设置fb的地址
	VIDW00ADD0B0 =FB_ADDR;
	VIDW00ADD1B0 = (((HOZVAL + 1)*4 + 0) * (LINEVAL + 1)) & (0xffffff);

	// 使能channel 0传输数据
	SHADOWCON = 0x1;
	/* LCD控制器开启 */
	VIDCON0  |= 0x3; /* 开启总控制器 */
	WINCON0 |= 1;     /* 开启窗口0 */
}

// 描点
void lcd_draw_pixel(int row, int col, int color)
{
	unsigned long * pixel = (unsigned long  *)FB_ADDR;

	*(pixel + row * COL + col) = color;

}

// 清屏
void lcd_clear_screen(int color)
{
	int i, j;

	for (i = 0; i < ROW; i++)
		for (j = 0; j < COL; j++)
			lcd_draw_pixel(i, j, color);

}

// 划横线
void lcd_draw_hline(int row, int col1, int col2, int color)
{
	int j;

	// 描第row行，第j列
	for (j = col1; j <= col2; j++)
		lcd_draw_pixel(row, j, color);

}

// 划竖线
void lcd_draw_vline(int col, int row1, int row2, int color)
{
	int i;
	// 描第i行，第col列
	for (i = row1; i <= row2; i++)
		lcd_draw_pixel(i, col, color);

}

// 划十字
void lcd_draw_cross(int row, int col, int halflen, int color)
{
	lcd_draw_hline(row, col-halflen, col+halflen, color);
	lcd_draw_vline(col, row-halflen, row+halflen, color);
}
/*****************************************************************************
** 函数名称: Draw_Circle
** 功能描述: 在指定位置画一个指定大小的圆
//(x,y):中心点
//r    :半径
Made by mleaf_hx
*****************************************************************************/
void Draw_Circle(int x0,int y0,char r,int RGB_Code)
{
	int a,b;
	int di;
	a=0;b=r;	  
	di=3-(r<<1);             //判断下个点位置的标志
	while(a<=b)
	{
		lcd_draw_pixel(x0-b,y0-a,RGB_Code);             //3           
		lcd_draw_pixel(x0+b,y0-a,RGB_Code);             //0           
		lcd_draw_pixel(x0-a,y0+b,RGB_Code);             //1       
		lcd_draw_pixel(x0-b,y0-a,RGB_Code);             //7           
		lcd_draw_pixel(x0-a,y0-b,RGB_Code);             //2             
		lcd_draw_pixel(x0+b,y0+a,RGB_Code);             //4               
		lcd_draw_pixel(x0+a,y0-b,RGB_Code);             //5
		lcd_draw_pixel(x0+a,y0+b,RGB_Code);             //6 
		lcd_draw_pixel(x0-b,y0+a,RGB_Code);             
		a++;
		//使用Bresenham算法画圆     
		if(di<0)di +=4*a+6;	  
		else
		{
			di+=10+4*(a-b);   
			b--;
		} 
		lcd_draw_pixel(x0+a,y0+b,RGB_Code);
	}
} 

// 画字符
void lcd_draw_char(unsigned char c)
{
	// 必须是静态变量
	static int x = 0;	// 第几列
	static int y = 0;	// 第几行

	int i,j;
	unsigned char line_dots;

	// 获得字模
	unsigned char *char_dots = (unsigned char *) (fontdata_8x16 + c * 16);


	// 是否需要回车换行
	if (c == '\n')
	{
		y += 16;
		if (y > ROW)
			y = 0;
		return ;
	}
	else if (c == '\r')
	{
		x = 0;
		return;
	}

	for (i = 0; i < 16; i++)
	{
		line_dots = char_dots[i];
		for (j = 0; j < 8; j++)
		{
			// 为1,则描蓝点
			if (line_dots & (0x80 >> j))
			{
				lcd_draw_pixel(y+i, x+j, 0xff);
			}
		}
	}

	// 光标移动到下一个8*16的位置
	x += 8;
	if (x > COL)
	{
		x = 0;
		y += 16;
		if (y > ROW)
			y = 0;
	}
}


// 画图
void lcd_draw_bmp(const unsigned char gImage_bmp[])
{
	int i, j;
	unsigned char *p = (unsigned char *)gImage_bmp;
	int blue, green, red;
	int color;

	// 图片大小800x480像素
	for (i = 0; i < HEIGHT; i++)
		for (j = 0; j < WIDTHEIGHT; j++)
		{
			blue  = *p++;
			green = *p++;
			red   = *p++;
		
			color = red << 16 | green << 8 | blue << 0;
			lcd_draw_pixel(i, j, color);
		}

}

