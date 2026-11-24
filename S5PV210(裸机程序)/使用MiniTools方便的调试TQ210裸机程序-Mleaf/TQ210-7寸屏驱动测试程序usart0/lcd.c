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


作者：Mleaf HX
qq：350983773
注：本程序从友善裸板程序移植而来
********************************************/
#include "lcd.h"
//显示参数宏定义
#define HSPW 			(19)
#define HBPD			(26- 1)
#define HFPD 			(210- 1)
#define VSPW			(9)
#define VBPD 			(14- 1)
#define VFPD 			(22 - 1)
/*使用下面的时序显示会不完全
#define HSPW 			(39)//thpw
#define HBPD			(46 - 1)//thb
#define HFPD 			(210- 1)//thfp
#define VSPW			(10)//tvpw
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
// 初始化LCD
void lcd_init(void)
{
	// 配置引脚用于LCD功能
	GPF0CON = 0x22222222;
	GPF1CON = 0x22222222;
	GPF2CON = 0x22222222;
	GPF3CON = 0x22222222;

	// 打开背光GPD0_0
	GPD0CON &= ~(0xf<<0);//先将[0:4]位清零
	GPD0CON |= (1<<0);
	GPD0DAT |= (1<<0);

	// 10: RGB=FIMD I80=FIMD ITU=FIMD
	DISPLAY_CONTROL = 2<<0;

	// bit[26~28]:使用RGB接口
	// bit[18]:RGB 并行
	// bit[2]:选择时钟源为HCLK_DSYS=166MHz
	VIDCON0 &= ~( (3<<26)|(1<<18)|(1<<2));

	// bit[1]:使能lcd控制器
	// bit[0]:当前帧结束后使能lcd控制器
	VIDCON0 |= ( (1<<0)|(1<<1) );

	// bit[6]:选择需要分频
	// bit[6~13]:分频系数为5，即VCLK = 166M/(4+1) = 33.2MHz
	VIDCON0 |= (5<<6) | (1<<4);


	// AT070TN92.pdf(p14) Timing：VSYNC和HSYNC都是低脉冲
	// s5pv210芯片手册(p1208) 时序图：VSYNC和HSYNC都是高脉冲有效，所以需要反转
	VIDCON1 &= ~(1<<7);
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
	//VIDCON0  |= 0x3; /* 开启总控制器 */
	//WINCON0 |= 1;     /* 开启窗口0 */
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
//画直线
void LCD_DrawLine(int x1, int y1, int x2, int y2,int RGB_Code)
{
	int t; 
	int xerr=0,yerr=0,delta_x,delta_y,distance; 
	int incx,incy,uRow,uCol; 

	delta_x=x2-x1; //计算坐标增量 
	delta_y=y2-y1; 
	uRow=x1; 
	uCol=y1; 
	if(delta_x>0)incx=1; //设置单步方向 
	else if(delta_x==0)incx=0;//垂直线 
	else {incx=-1;delta_x=-delta_x;} 
	if(delta_y>0)incy=1; 
	else if(delta_y==0)incy=0;//水平线 
	else{incy=-1;delta_y=-delta_y;} 
	if( delta_x>delta_y)distance=delta_x; //选取基本增量坐标轴 
	else distance=delta_y; 
	for(t=0;t<=distance+1;t++ )//画线输出 
	{  
		lcd_draw_pixel(uRow,uCol,RGB_Code);//画点 
		xerr+=delta_x ; 
		yerr+=delta_y ; 
		if(xerr>distance) 
		{ 
			xerr-=distance; 
			uRow+=incx; 
		} 
		if(yerr>distance) 
		{ 
			yerr-=distance; 
			uCol+=incy; 
		} 
	}  
}

// 画方框
void lcd_draw_Rectangle(int x1,int y1,int x2,int y2,int color)
{
    LCD_DrawLine(x1,y1,x2,y1,color);
    LCD_DrawLine(x2,y1,x2,y2,color);
    LCD_DrawLine(x1,y2,x2,y2,color);
    LCD_DrawLine(x1,y1,x1,y2,color);
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

