/****************************************************************
*    	CopyRight(C) 2010 by Rock-Chip Fuzhou
*     All Rights Reserved
*	�ļ���:ddr.c
*	����:sdram driver implement
*	����:hcy
*	��������:2008-11-08
*	���ļ�¼:
*	$Log: ddr.c,v $
*	��ǰ�汾:1.00   20100315 hyc �ύ��ʼ�汾
*	Revision 1.01 2010/06/15 01:46:27  wqq 
*	���Ӹ�λ����
****************************************************************/
#define  DRIVERS_DDRAM


#ifdef DRIVERS_DDRAM
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/irqflags.h>
#include <linux/string.h>
#include <linux/version.h>

#include <asm/tcm.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
#include <mach/rk2818_iomap.h>
#include <mach/memory.h>
#include <linux/clk.h>
#else
#include <asm/arch/hardware.h>
#include <asm/arch/rk28_debug.h>
#include <asm/arch/rk28_scu.h>
#endif
#include <asm/delay.h>
#include <asm/cacheflush.h>

/* 20100615,HSL@RK,FOR pwm3 change vdd core.*/
#include <asm/io.h>

/*APB reg file*/
typedef volatile struct tagGRF_REG
{
    unsigned int  CPU_APB_REG0;
    unsigned int  CPU_APB_REG1;
    unsigned int  CPU_APB_REG2;
    unsigned int  CPU_APB_REG3;
    unsigned int  CPU_APB_REG4;
    unsigned int  CPU_APB_REG5;
    unsigned int  CPU_APB_REG6;
    unsigned int  CPU_APB_REG7;
    unsigned int  IOMUX_A_CON;
    unsigned int  IOMUX_B_CON;
    unsigned int  GPIO0_AB_PU_CON;
    unsigned int  GPIO0_CD_PU_CON;
    unsigned int  GPIO1_AB_PU_CON;
    unsigned int  GPIO1_CD_PU_CON;
    unsigned int  OTGPHY_CON0;
    unsigned int  OTGPHY_CON1;
}GRF_REG, *pGRF_REG,*pAPB_REG;

/*scu*/
typedef volatile struct tagSCU_REG
{
    unsigned int SCU_APLL_CON;//[3];//0:arm 1:dsp 2:codec
    unsigned int SCU_DPLL_CON;
    unsigned int SCU_CPLL_CON;
    unsigned int SCU_MODE_CON;
    unsigned int SCU_PMU_CON;
    unsigned int SCU_CLKSEL0_CON;
    unsigned int SCU_CLKSEL1_CON;
    unsigned int SCU_CLKGATE0_CON;
    unsigned int SCU_CLKGATE1_CON;
    unsigned int SCU_CLKGATE2_CON;
    unsigned int SCU_SOFTRST_CON;
    unsigned int SCU_CHIPCFG_CON;
    unsigned int SCU_CPUPD;
	unsigned int SCU_CLKSEL2_CON;
}SCU_REG,*pSCU_REG;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
/*intc*/
typedef volatile struct tagINTC_REG
{	/*offset 0x00~0x30*/
	unsigned int IRQ_INTEN_L;	   //IRQ interrupt source enable register (low)
	unsigned int IRQ_INTEN_H;	   //IRQ interrupt source enable register (high)
	unsigned int IRQ_INTMASK_L;    //IRQ interrupt source mask register (low).
	unsigned int IRQ_INTMASK_H;    //IRQ interrupt source mask register (high).
	unsigned int IRQ_INTFORCE_L;   //IRQ interrupt force register
	unsigned int IRQ_INTFORCE_H;   //
	unsigned int IRQ_RAWSTATUS_L;  //IRQ raw status register
	unsigned int IRQ_RAWSTATUS_H;  //
	unsigned int IRQ_STATUS_L;	   //IRQ status register
	unsigned int IRQ_STATUS_H;	   //
	unsigned int IRQ_MASKSTATUS_L; //IRQ interrupt mask status register
	unsigned int IRQ_MASKSTATUS_H; //
	unsigned int IRQ_FINALSTATUS_L;//IRQ interrupt final status
	unsigned int IRQ_FINALSTATUS_H; 
	unsigned int reserved0[(0xC0-0x38)/4];
	
	/*offset 0xc0~0xd8*/
	unsigned int FIQ_INTEN; 	   //Fast interrupt enable register
	unsigned int FIQ_INTMASK;	   //Fast interrupt mask register
	unsigned int FIQ_INTFORCE;	   //Fast interrupt force register
	unsigned int FIQ_RAWSTATUS;    //Fast interrupt source raw status register
	unsigned int FIQ_STATUS;	   //Fast interrupt status register
	unsigned int FIQ_FINALSTATUS;  //Fast interrupt final status register
	unsigned int IRQ_PLEVEL;	   //IRQ System Priority Level Register
	unsigned int reserved1[(0xe8-0xdc)/4];
	
	/*offset 0xe8~0xe8+39*4*/
	unsigned int IRQ_PN_OFFSET[40];//Interrupt N priority level register(s),
							 // where N is from 0 to 15
	unsigned int reserved2[(0x3f0-0x188)/4];
	
	/*offset 0x3f0~0x3fc*/
	unsigned int ICTL_COMP_PARAMS_2;   //Component Parameter Register 2
	unsigned int ICTL_COMP_PARAMS_1;   //Component Parameter Register 1
	unsigned int AHB_ICTL_COMP_VERSION;//Version register
	unsigned int ICTL_COMP_TYPE;	   //Component Type Register
}INTC_REG, *pINTC_REG;

typedef u32 uint32;
#define SCU_BASE_ADDR_VA	RK2818_SCU_BASE
#define SDRAMC_BASE_ADDR_VA	RK2818_SDRAMC_BASE
#define REG_FILE_BASE_ADDR_VA	RK2818_REGFILE_BASE
#define INTC_BASE_ADDR_VA		RK2818_INTC_BASE

static inline int rockchip_clk_get_ahb(void)
{
	struct clk *arm_hclk = clk_get(NULL, "arm_hclk");
	int ahb = clk_get_rate(arm_hclk) / 1000;
	clk_put(arm_hclk);
	return ahb;
}

extern void clk_recalculate_root_clocks(void);

#define S_INFO(msg...)

#endif

#define SDRAM_REG_BASE     (SDRAMC_BASE_ADDR_VA)
#define DDR_REG_BASE       (SDRAMC_BASE_ADDR_VA)

/* CPU_APB_REG4 */
#define MSDR_1_8V_ENABLE  (0x1 << 24)
#define READ_PIPE_ENABLE  (0x1 << 22)
#define EXIT_SELF_REFRESH (0x1 << 21)

/* CPU_APB_REG5 */
#define MEMTYPEMASK   (0x3 << 11) 
#define SDRAM         (0x0 << 11)
#define Mobile_SDRAM  (0x1 << 11)
#define DDRII         (0x2 << 11)
#define Mobile_DDR    (0x3 << 11)

/* SDRAM Config Register */
#define DATA_WIDTH_16     (0x0 << 13)
#define DATA_WIDTH_32     (0x1 << 13)
#define DATA_WIDTH_64     (0x2 << 13)
#define DATA_WIDTH_128    (0x3 << 13)

#define COL(n)            ((n-1) << 9)
#define ROW(n)            ((n-1) << 5)

#define BANK_2            (0 << 3)
#define BANK_4            (1 << 3)
#define BANK_8            (2 << 3)
#define BANK_16           (3 << 3) 

/* SDRAM Timing Register0 */
#define T_RC_SHIFT        (22)
#define T_RC_MAX         (0xF)
#define T_XSR_MSB_SHIFT   (27)
#define T_XSR_MSB_MASK    (0x1F)
#define T_XSR_LSB_SHIFT   (18)
#define T_XSR_LSB_MASK    (0xF)
#define T_RCAR_SHIFT      (14)
#define T_RCAR_MAX       (0xF)
#define T_WR_SHIFT        (12)
#define T_WR_MAX         (0x3)
#define T_RP_SHIFT        (9)
#define T_RP_MAX         (0x7)
#define T_RCD_SHIFT       (6)
#define T_RCD_MAX        (0x7)
#define T_RAS_SHIFT       (2)
#define T_RAS_MASK        (0xF)

#define CL_1              (0)
#define CL_2              (1)
#define CL_3              (2)
#define CL_4              (3)

/* SDRAM Timing Register1 */
#define AR_COUNT_SHIFT    (16)

/* SDRAM Control Regitster */
#define MSD_DEEP_POWERDOWN (1 << 20)
#define UPDATE_EMRS    (1 << 18)
#define OPEN_BANK_COUNT_SHIFT  (12)
#define SR_MODE            (1 << 11)
#define UPDATE_MRS         (1 << 9)
#define READ_PIPE_SHIFT    (6)
#define REFRESH_ALL_ROW_A  (1 << 5)
#define REFRESH_ALL_ROW_B  (1 << 4)
#define DELAY_PRECHARGE    (1 << 3)
#define SDR_POWERDOWN      (1 << 2)
#define ENTER_SELF_REFRESH (1 << 1)
#define SDR_INIT           (1 << 0)

/* Extended Mode Register */
#define DS_FULL            (0 << 5)
#define DS_1_2             (1 << 5)
#define DS_1_4             (2 << 5)
#define DS_1_8             (3 << 5)

#define TCSR_70            (0 << 3)
#define TCSR_45            (1 << 3)
#define TCSR_15            (2 << 3)
#define TCSR_85            (3 << 3)

#define PASR_4_BANK        (0)
#define PASR_2_BANK        (1)
#define PASR_1_BANK        (2)
#define PASR_1_2_BANK      (5)
#define PASR_1_4_BANK      (6)

/* SDRAM Controller register struct */
typedef volatile struct TagSDRAMC_REG
{
    volatile uint32 MSDR_SCONR;         //SDRAM configuration register
    volatile uint32 MSDR_STMG0R;        //SDRAM timing register0
    volatile uint32 MSDR_STMG1R;        //SDRAM timing register1
    volatile uint32 MSDR_SCTLR;         //SDRAM control register
    volatile uint32 MSDR_SREFR;         //SDRAM refresh register
    volatile uint32 MSDR_SCSLR0_LOW;    //Chip select register0(lower 32bits)
    volatile uint32 MSDR_SCSLR1_LOW;    //Chip select register1(lower 32bits)
    volatile uint32 MSDR_SCSLR2_LOW;    //Chip select register2(lower 32bits)
    uint32 reserved0[(0x54-0x1c)/4 - 1];
    volatile uint32 MSDR_SMSKR0;        //Mask register 0
    volatile uint32 MSDR_SMSKR1;        //Mask register 1
    volatile uint32 MSDR_SMSKR2;        //Mask register 2
    uint32 reserved1[(0x84-0x5c)/4 - 1];
    volatile uint32 MSDR_CSREMAP0_LOW;  //Remap register for chip select0(lower 32 bits)
    uint32 reserved2[(0x94-0x84)/4 - 1];
    volatile uint32 MSDR_SMTMGR_SET0;   //Static memory timing register Set0
    volatile uint32 MSDR_SMTMGR_SET1;   //Static memory timing register Set1
    volatile uint32 MSDR_SMTMGR_SET2;   //Static memory timing register Set2
    volatile uint32 MSDR_FLASH_TRPDR;   //FLASH memory tRPD timing register
    volatile uint32 MSDR_SMCTLR;        //Static memory control register
    uint32 reserved4;
    volatile uint32 MSDR_EXN_MODE_REG;  //Extended Mode Register
}SDRAMC_REG_T,*pSDRAMC_REG_T;


#define pSDR_Reg       ((pSDRAMC_REG_T)SDRAM_REG_BASE)
#define pDDR_Reg       ((pDDRC_REG_T)DDR_REG_BASE)
#define pSCU_Reg       ((pSCU_REG)SCU_BASE_ADDR_VA)
#define pGRF_Reg       ((pGRF_REG)REG_FILE_BASE_ADDR_VA)
#define pUART_Reg       ((pUART_REG)UART0_BASE_ADDR_VA)


#define DDR_MEM_TYPE()	(pGRF_Reg->CPU_APB_REG0 & MEMTYPEMASK)
#define DDR_ENABLE_SLEEP()      // 20100521,NOT ENABLE SLEEP. do{pDDR_Reg->CTRL_REG_36 = 0x1F1F;}while(0)

#define ODT_DIS          (0x0)
#define ODT_75           (0x8)
#define ODT_150          (0x40)
#define ODT_50           (0x48)

/* CTRL_REG_01 */
#define EXP_BDW_OVFLOW   (0x1 << 24)  //port 3
#define LCDC_BDW_OVFLOW  (0x1 << 16)  //port 2

/* CTRL_REG_02 */
#define VIDEO_BDW_OVFLOW (0x1 << 24) //port 7
#define CEVA_BDW_OVFLOW  (0x1 << 16) //port 6
#define ARMI_BDW_OVFLOW  (0x1 << 8)  //port 5
#define ARMD_BDW_OVFLOW  (0x1 << 0)  //port 4

/* CTR_REG_03 */
#define CONCURRENTAP     (0x1 << 16)

/* CTRL_REG_04 */
#define SINGLE_ENDED_DQS  (0x0 << 24)
#define DIFFERENTIAL_DQS  (0x1 << 24)
#define DLL_BYPASS_EN     (0x1 << 16)

/* CTR_REG_05 */
#define CS1_BANK_4        (0x0 << 16)
#define CS1_BANK_8        (0x1 << 16)
#define CS0_BANK_4        (0x0 << 8)
#define CS0_BANK_8        (0x1 << 8)

/* CTR_REG_07 */
#define ODT_CL_3          (0x1 << 8)

/* CTR_REG_08 */
#define BUS_16BIT         (0x1 << 16)
#define BUS_32BIT         (0x0 << 16)

/* CTR_REG_10 */
#define EN_TRAS_LOCKOUT   (0x1 << 24)
#define DIS_TRAS_LOCKOUT  (0x0 << 24)

/* CTR_REG_12 */
#define AXI_MC_ASYNC      (0x0)
#define AXI_MC_2_1        (0x1)
#define AXI_MC_1_2        (0x2)
#define AXI_MC_SYNC       (0x3)

/* CTR_REG_13 */
#define LCDC_WR_PRIO(n)    ((n) << 24)
#define LCDC_RD_PRIO(n)    ((n) << 16)

/* CTR_REG_14 */
#define EXP_WR_PRIO(n)     ((n) << 16)
#define EXP_RD_PRIO(n)     ((n) << 8)

/* CTR_REG_15 */
#define ARMD_WR_PRIO(n)     ((n) << 8)
#define ARMD_RD_PRIO(n)     (n)
#define ARMI_RD_PRIO(n)     ((n) << 24)

/* CTR_REG_16 */
#define ARMI_WR_PRIO(n)     (n)
#define CEVA_WR_PRIO(n)     ((n) << 24)
#define CEVA_RD_PRIO(n)     ((n) << 16)

/* CTR_REG_17 */
#define VIDEO_WR_PRIO(n)     ((n) << 16)
#define VIDEO_RD_PRIO(n)     ((n) << 8)
#define CS_MAP(n)            ((n) << 24)

/* CTR_REG_18 */
#define CS0_RD_ODT_MASK      (0x3 << 24)
#define CS0_RD_ODT(n)        (0x1 << (24+(n)))
#define CS0_LOW_POWER_REF_EN (0x0 << 8)
#define CS0_LOW_POWER_REF_DIS (0x1 << 8)
#define CS1_LOW_POWER_REF_EN (0x0 << 9)
#define CS1_LOW_POWER_REF_DIS (0x1 << 9)

/* CTR_REG_19 */
#define CS0_ROW(n)           ((15-(n)) << 24)
#define CS1_WR_ODT_MASK      (0x3 << 16)
#define CS1_WR_ODT(n)        (0x1 << (16+(n)))
#define CS0_WR_ODT_MASK      (0x3 << 8)
#define CS0_WR_ODT(n)        (0x1 << (8+(n)))
#define CS1_RD_ODT_MASK      (0x3)
#define CS1_RD_ODT(n)        (0x1 << (n))

/* CTR_REG_20 */
#define CS1_ROW(n)           (15-(n))
#define CL(n)                (((n)&0x7) << 16)

/* CTR_REG_21 */
#define CS0_COL(n)           (13-(n))
#define CS1_COL(n)           ((13-(n)) << 8)

/* CTR_REG_23 */
#define TRRD(n)              ((n) << 24)
#define TCKE(n)              ((n) << 8)

/* CTR_REG_24 */
#define TRTP(n)              (n)

/* CTR_REG_29 */
//CAS latency linear value
#define CL_L_1_0             (2)
#define CL_L_1_5             (3)
#define CL_L_2_0             (4)
#define CL_L_2_5             (5)
#define CL_L_3_0             (6)
#define CL_L_3_5             (7)
#define CL_L_4_0             (8)
#define CL_L_4_5             (9)
#define CL_L_5_0             (0xA)
#define CL_L_5_5             (0xB)
#define CL_L_6_0             (0xC)
#define CL_L_6_5             (0xD)
#define CL_L_7_0             (0xE)
#define CL_L_7_5             (0xF)

/* CTR_REG_34 */
#define CS0_TRP_ALL(n)       ((n) << 24)
#define TRP(n)               ((n) << 16)

/* CTR_REG_35 */
#define WL(n)                ((((n)&0xF) << 16) | (((n)&0xF) << 24))
#define TWTR(n)              (((n)&0xF) << 8)
#define CS1_TRP_ALL(n)       ((n)&0xF)

/* CTR_REG_37 */
#define TMRD(n)              ((n) << 24)
#define TDAL(n)              ((n) << 16)
#define TCKESR(n)            ((n) << 8)
#define TCCD(n)              (n)

/* CTR_REG_38 */
#define TRC(n)               ((n) << 24)
#define TFAW(n)              ((n) << 16)
#define TWR(n)               (n)

/* CTR_REG_40 */
#define EXP_BW_PER(n)        ((n) << 16)
#define LCDC_BW_PER(n)       (n)

/* CTR_REG_41 */
#define ARMI_BW_PER(n)       ((n) << 16)
#define ARMD_BW_PER(n)       (n)

/* CTR_REG_42 */
#define VIDEO_BW_PER(n)      ((n) << 16)
#define CEVA_BW_PER(n)       (n)

/* CTR_REG_43 */
#define TMOD(n)              (((n)&0xFF) << 24)

/* CTR_REG_44 */
#define TRFC(n)              ((n) << 16)
#define TRCD(n)              ((n) << 8)
#define TRAS_MIN(n)          (n)

/* CTR_REG_48 */
#define TPHYUPD_RESP(n)      (((n)&0x3FFF) << 16)
#define TCTRLUPD_MAX(n)      ((n)&0x3FFF)

/* CTR_REG_51 */
#define CS0_MR(n)            ((((n) & 0xFEF0) | 0x2) << 16)
#define TREF(n)              (n)

/* CTR_REG_52 */
#define CS0_EMRS_1(n)        (((n)&0xFFC7) << 16)
#define CS1_MR(n)            (((n) & 0xFEF0) | 0x2)

/* CTR_REG_53 */
#define CS0_EMRS_2(n)        ((n) << 16)
#define CS0_EMR(n)           ((n) << 16)
#define CS1_EMRS_1(n)        ((n)&0xFFC7)

/* CTR_REG_54 */
#define CS0_EMRS_3(n)        ((n) << 16)
#define CS1_EMRS_2(n)        (n)
#define CS1_EMR(n)           (n)

/* CTR_REG_55 */
#define CS1_EMRS_3(n)        (n)

/* CTR_REG_59 */
#define CS_MSK_0(n)          (((n)&0xFFFF) << 16)

/* CTR_REG_60 */
#define CS_VAL_0(n)          (((n)&0xFFFF) << 16)
#define CS_MSK_1(n)          ((n)&0xFFFF)

/* CTR_REG_61 */
#define CS_VAL_1(n)          ((n)&0xFFFF)

/* CTR_REG_62 */
#define MODE5_CNT(n)         (((n)&0xFFFF) << 16)
#define MODE4_CNT(n)         ((n)&0xFFFF)

/* CTR_REG_63 */
#define MODE1_2_CNT(n)        ((n)&0xFFFF)

/* CTR_REG_64 */
#define TCPD(n)              ((n) << 16)
#define MODE3_CNT(n)         (n)

/* CTR_REG_65 */
#define TPDEX(n)             ((n) << 16)
#define TDLL(n)              (n)

/* CTR_REG_66 */
#define TXSNR(n)             ((n) << 16)
#define TRAS_MAX(n)          (n)

/* CTR_REG_67 */
#define TXSR(n)              (n)

/* CTR_REG_68 */
#define TINIT(n)             (n)


/* DDR Controller register struct */
typedef volatile struct TagDDRC_REG
{
    volatile uint32 CTRL_REG_00;
    volatile uint32 CTRL_REG_01;
    volatile uint32 CTRL_REG_02;
    volatile uint32 CTRL_REG_03;
    volatile uint32 CTRL_REG_04;
    volatile uint32 CTRL_REG_05;
    volatile uint32 CTRL_REG_06;
    volatile uint32 CTRL_REG_07;
    volatile uint32 CTRL_REG_08;
    volatile uint32 CTRL_REG_09;
    volatile uint32 CTRL_REG_10;
    volatile uint32 CTRL_REG_11;
    volatile uint32 CTRL_REG_12;
    volatile uint32 CTRL_REG_13;
    volatile uint32 CTRL_REG_14;
    volatile uint32 CTRL_REG_15;
    volatile uint32 CTRL_REG_16;
    volatile uint32 CTRL_REG_17;
    volatile uint32 CTRL_REG_18;
    volatile uint32 CTRL_REG_19;
    volatile uint32 CTRL_REG_20;
    volatile uint32 CTRL_REG_21;
    volatile uint32 CTRL_REG_22;
    volatile uint32 CTRL_REG_23;
    volatile uint32 CTRL_REG_24;
    volatile uint32 CTRL_REG_25;
    volatile uint32 CTRL_REG_26;
    volatile uint32 CTRL_REG_27;
    volatile uint32 CTRL_REG_28;
    volatile uint32 CTRL_REG_29;
    volatile uint32 CTRL_REG_30;
    volatile uint32 CTRL_REG_31;
    volatile uint32 CTRL_REG_32;
    volatile uint32 CTRL_REG_33;
    volatile uint32 CTRL_REG_34;
    volatile uint32 CTRL_REG_35;
    volatile uint32 CTRL_REG_36;
    volatile uint32 CTRL_REG_37;
    volatile uint32 CTRL_REG_38;
    volatile uint32 CTRL_REG_39;
    volatile uint32 CTRL_REG_40;
    volatile uint32 CTRL_REG_41;
    volatile uint32 CTRL_REG_42;
    volatile uint32 CTRL_REG_43;
    volatile uint32 CTRL_REG_44;
    volatile uint32 CTRL_REG_45;
    volatile uint32 CTRL_REG_46;
    volatile uint32 CTRL_REG_47;
    volatile uint32 CTRL_REG_48;
    volatile uint32 CTRL_REG_49;
    volatile uint32 CTRL_REG_50;
    volatile uint32 CTRL_REG_51;
    volatile uint32 CTRL_REG_52;
    volatile uint32 CTRL_REG_53;
    volatile uint32 CTRL_REG_54;
    volatile uint32 CTRL_REG_55;
    volatile uint32 CTRL_REG_56;
    volatile uint32 CTRL_REG_57;
    volatile uint32 CTRL_REG_58;
    volatile uint32 CTRL_REG_59;
    volatile uint32 CTRL_REG_60;
    volatile uint32 CTRL_REG_61;
    volatile uint32 CTRL_REG_62;
    volatile uint32 CTRL_REG_63;
    volatile uint32 CTRL_REG_64;
    volatile uint32 CTRL_REG_65;
    volatile uint32 CTRL_REG_66;
    volatile uint32 CTRL_REG_67;
    volatile uint32 CTRL_REG_68;
    volatile uint32 CTRL_REG_69;
    volatile uint32 CTRL_REG_70;
    volatile uint32 CTRL_REG_71;
    volatile uint32 CTRL_REG_72;
    volatile uint32 CTRL_REG_73;
    volatile uint32 CTRL_REG_74;
    volatile uint32 CTRL_REG_75;
    volatile uint32 CTRL_REG_76;
    volatile uint32 CTRL_REG_77;
    volatile uint32 CTRL_REG_78;
    volatile uint32 CTRL_REG_79;
    volatile uint32 CTRL_REG_80;
    volatile uint32 CTRL_REG_81;
    volatile uint32 CTRL_REG_82;
    volatile uint32 CTRL_REG_83;
    volatile uint32 CTRL_REG_84;
    volatile uint32 CTRL_REG_85;
    volatile uint32 CTRL_REG_86;
    volatile uint32 CTRL_REG_87;
    volatile uint32 CTRL_REG_88;
    volatile uint32 CTRL_REG_89;
    volatile uint32 CTRL_REG_90;
    volatile uint32 CTRL_REG_91;
    volatile uint32 CTRL_REG_92;
    volatile uint32 CTRL_REG_93;
    volatile uint32 CTRL_REG_94;
    volatile uint32 CTRL_REG_95;
    volatile uint32 CTRL_REG_96;
    volatile uint32 CTRL_REG_97;
    volatile uint32 CTRL_REG_98;
    volatile uint32 CTRL_REG_99;
    volatile uint32 CTRL_REG_100;
    volatile uint32 CTRL_REG_101;
    volatile uint32 CTRL_REG_102;
    volatile uint32 CTRL_REG_103;
    volatile uint32 CTRL_REG_104;
    volatile uint32 CTRL_REG_105;
    volatile uint32 CTRL_REG_106;
    volatile uint32 CTRL_REG_107;
    volatile uint32 CTRL_REG_108;
    volatile uint32 CTRL_REG_109;
    volatile uint32 CTRL_REG_110;
    volatile uint32 CTRL_REG_111;
    volatile uint32 CTRL_REG_112;
    volatile uint32 CTRL_REG_113;
    volatile uint32 CTRL_REG_114;
    volatile uint32 CTRL_REG_115;
    volatile uint32 CTRL_REG_116;
    volatile uint32 CTRL_REG_117;
    volatile uint32 CTRL_REG_118;
    volatile uint32 CTRL_REG_119;
    volatile uint32 CTRL_REG_120;
    volatile uint32 CTRL_REG_121;
    volatile uint32 CTRL_REG_122;
    volatile uint32 CTRL_REG_123;
    volatile uint32 CTRL_REG_124;
    volatile uint32 CTRL_REG_125;
    volatile uint32 CTRL_REG_126;
    volatile uint32 CTRL_REG_127;
    volatile uint32 CTRL_REG_128;
    volatile uint32 CTRL_REG_129;
    volatile uint32 CTRL_REG_130;
    volatile uint32 CTRL_REG_131;
    volatile uint32 CTRL_REG_132;
    volatile uint32 CTRL_REG_133;
    volatile uint32 CTRL_REG_134;
    volatile uint32 CTRL_REG_135;
    volatile uint32 CTRL_REG_136;
    volatile uint32 CTRL_REG_137;
    volatile uint32 CTRL_REG_138;
    volatile uint32 CTRL_REG_139;
    volatile uint32 CTRL_REG_140;
    volatile uint32 CTRL_REG_141;
    volatile uint32 CTRL_REG_142;
    volatile uint32 CTRL_REG_143;
    volatile uint32 CTRL_REG_144;
    volatile uint32 CTRL_REG_145;
    volatile uint32 CTRL_REG_146;
    volatile uint32 CTRL_REG_147;
    volatile uint32 CTRL_REG_148;
    volatile uint32 CTRL_REG_149;
    volatile uint32 CTRL_REG_150;
    volatile uint32 CTRL_REG_151;
    volatile uint32 CTRL_REG_152;
    volatile uint32 CTRL_REG_153;
    volatile uint32 CTRL_REG_154;
    volatile uint32 CTRL_REG_155;
    volatile uint32 CTRL_REG_156;
    volatile uint32 CTRL_REG_157;
    volatile uint32 CTRL_REG_158;
    volatile uint32 CTRL_REG_159;
}DDRC_REG_T,*pDDRC_REG_T;

typedef volatile struct tagUART_STRUCT
{
    unsigned int UART_RBR;
    unsigned int UART_DLH;
    unsigned int UART_IIR;
    unsigned int UART_LCR;
    unsigned int UART_MCR;
    unsigned int UART_LSR;
    unsigned int UART_MSR;
    unsigned int UART_SCR;
    unsigned int RESERVED1[(0x30-0x20)/4];
    unsigned int UART_SRBR[(0x70-0x30)/4];
    unsigned int UART_FAR;
    unsigned int UART_TFR;
    unsigned int UART_RFW;
    unsigned int UART_USR;
    unsigned int UART_TFL;
    unsigned int UART_RFL;
    unsigned int UART_SRR;
    unsigned int UART_SRTS;
    unsigned int UART_SBCR;
    unsigned int UART_SDMAM;
    unsigned int UART_SFE;
    unsigned int UART_SRT;
    unsigned int UART_STET;
    unsigned int UART_HTX;
    unsigned int UART_DMASA;
    unsigned int RESERVED2[(0xf4-0xac)/4];
    unsigned int UART_CPR;
    unsigned int UART_UCV;
    unsigned int UART_CTR;
} UART_REG, *pUART_REG;


#define USE_DETECT            (1)    //�����Ƿ�̽��
#define USE_DQS_DIFFERENTIAL  (1)    //����DQS�����ʽ���ж�����ΪDifferential DQS���޶�����ΪSingle-ended DQS
#define USE_TRAS_LOCKOUT      (0)    //�����Ƿ�֧��tRAS lockout
#define USE_LOW_POWER_MODE    (0)    //�Ƿ�ʹ��low power mode

//#define pSDR_Reg       ((pSDRAMC_REG_T)SDRAM_REG_BASE)
//#define pDDR_Reg       ((pDDRC_REG_T)DDR_REG_BASE)
//#define pSCU_Reg       ((pSCU_REG)SCU_BASE_ADDR_VA)
//#define pGRF_Reg       ((pGRF_REG)REG_FILE_BASE_ADDR_VA)

#define  read32(address)           (*((uint32 volatile*)(address)))
#define  write32(address, value)   (*((uint32 volatile*)(address)) = value)

static volatile int __tcmdata rk28_debugs = 1;
static uint32 __tcmdata save_sp;

static uint32 __tcmdata telement;
static uint32 __tcmdata capability;  //����CS������
static uint32 __tcmdata SDRAMnewKHz = 150000;
static uint32 __tcmdata DDRnewKHz = 400000 ; //266000;
static uint32 __tcmdata SDRAMoldKHz = 66000;
static uint32 __tcmdata DDRoldKHz = 200000;
static unsigned int __tcmdata ddr_reg[8] ;
 
static __tcmdata uint32 bFreqRaise;
static __tcmdata uint32 elementCnt;
static __tcmdata uint32 ddrSREF;
static __tcmdata uint32 ddrRASMAX;
static __tcmdata uint32 ddrCTRL_REG_23;
static __tcmdata uint32 ddrCTRL_REG_24;
static __tcmdata uint32 ddrCTRL_REG_37;
static __tcmdata uint32 ddrCTRL_REG_34;
static __tcmdata uint32 ddrCTRL_REG_35;
static __tcmdata uint32 ddrCTRL_REG_38;
static __tcmdata uint32 ddrCTRL_REG_43;
static __tcmdata uint32 ddrCTRL_REG_66;
static __tcmdata uint32 ddrCTRL_REG_44;
static __tcmdata uint32 ddrCTRL_REG_67;
static __tcmdata uint32 ddrCTRL_REG_64;
static __tcmdata uint32 ddrCTRL_REG_68;
static __tcmdata uint32 ddrCTRL_REG_44;
static __tcmdata uint32 ddrCTRL_REG_67;
static __tcmdata uint32 ddrCTRL_REG_64;
static __tcmdata uint32 ddrCTRL_REG_68;


static void __tcmfunc DLLBypass(uint32 KHz)
{

    volatile uint32 value = 0;
    
    value = pDDR_Reg->CTRL_REG_04;
    pDDR_Reg->CTRL_REG_04 = value | DLL_BYPASS_EN;

    
    pDDR_Reg->CTRL_REG_70 = 0x10002117 | (elementCnt << 15);
    pDDR_Reg->CTRL_REG_71 = 0x10002117 | (elementCnt << 15);
    pDDR_Reg->CTRL_REG_72 = 0x10002117 | (elementCnt << 15);
    pDDR_Reg->CTRL_REG_73 = 0x10002117 | (elementCnt << 15);
    pDDR_Reg->CTRL_REG_74 = 0x00002104 | (elementCnt << 15);
    pDDR_Reg->CTRL_REG_75 = 0x00002104 | (elementCnt << 15);
    pDDR_Reg->CTRL_REG_76 = 0x00002104 | (elementCnt << 15);
    pDDR_Reg->CTRL_REG_77 = 0x00002104 | (elementCnt << 15);
}

static void SDRAMUpdateRef(uint32 kHz)
{
    if(capability <= 0x2000000) // <= SDRAM_2x32x4
    {
        // 64Mb and 128Mb SDRAM's auto refresh cycle 15.6us or a burst of 4096 auto refresh cycles once in 64ms
        pSDR_Reg->MSDR_SREFR = (((125*kHz)/1000) >> 3) & 0xFFFF;  // 125/8 = 15.625us
    }
    else
    {
        // 256Mb and 512Mb SDRAM's auto refresh cycle 7.8us or a burst of 8192 auto refresh cycles once in 64ms
        pSDR_Reg->MSDR_SREFR = (((62*kHz)/1000) >> 3) & 0xFFFF;  // 62/8 = 7.75us
    }
}

static void SDRAMUpdateTiming(uint32 kHz)
{
    uint32 value =0;
    uint32 tmp = 0;
    
    value = pSDR_Reg->MSDR_STMG0R;
    value &= 0xFC3C003F;
    //t_rc =  15ns
    tmp = (15*kHz/1000000) + ((((15*kHz)%1000000) > 0) ? 1:0);
    tmp = (tmp > 0) ? (tmp - 1) : 0;
    tmp = (tmp > T_RC_MAX) ? T_RC_MAX : tmp;
    value |= tmp << T_RC_SHIFT;
    //t_rcar = 80ns
    tmp = (80*kHz/1000000) + ((((80*kHz)%1000000) > 0) ? 1:0);
    tmp = (tmp > 0) ? (tmp - 1) : 0;
    tmp = (tmp > T_RCAR_MAX) ? T_RCAR_MAX : tmp;
    value |= tmp << T_RCAR_SHIFT;
    //t_wr �̶�Ϊ2��clk
    value |= 1 << T_WR_SHIFT;
    //t_rp =  20ns
    tmp = (20*kHz/1000000) + ((((20*kHz)%1000000) > 0) ? 1:0);
    tmp = (tmp > 0) ? (tmp - 1) : 0;
    tmp = (tmp > T_RP_MAX) ? T_RP_MAX : tmp;
    value |= tmp << T_RP_SHIFT;
    //t_rcd = 20ns
    tmp = (20*kHz/1000000) + ((((20*kHz)%1000000) > 0) ? 1:0);
    tmp = (tmp > 0) ? (tmp - 1) : 0;
    tmp = (tmp > T_RCD_MAX) ? T_RCD_MAX : tmp;
    value |= tmp << T_RCD_SHIFT;
    pSDR_Reg->MSDR_STMG0R = value;
    #if 0
    if(newKHz >= 90000)  //����90M������read pipe
    {
        value = *pSDR_SUB_Reg;
        value |= READ_PIPE_ENABLE;
        *pSDR_SUB_Reg = value;
        value = pSDR_Reg->MSDR_SCTLR;
        value &= ~(0x7 << READ_PIPE_SHIFT);
        value |= (0x1 << READ_PIPE_SHIFT);
        pSDR_Reg->MSDR_SCTLR = value;
    }
    else
    {
        value = *pSDR_SUB_Reg;
        value &= ~(READ_PIPE_ENABLE);
        *pSDR_SUB_Reg = value;
    }
    #endif
}
 
#if 0
static uint32 __tcmfunc DDR_debug_byte(char byte)
{
	uint32 uartTimeOut;
#define UART_THR            UART_RBR
	pUART_REG g_UART =((pUART_REG)(( UART1_BASE_ADDR_VA)));
    if( byte == '\n' )
	  	DDR_debug_byte(0x0d);          
	uartTimeOut = 0xffff;
	while((g_UART->UART_USR & (1<<1)) != (1<<1))
	{
		if(uartTimeOut == 0)
		{
			return (1);
		}
		uartTimeOut--;
	}
	g_UART->UART_THR = byte;         
	return (0);
}
static uint32 __tcmfunc DDR_debug_string(char *s)
{
	 while(*s){
                DDR_debug_byte(*s);
                s++;
        }
        return 0;
}
#else
#define DDR_debug_string( a ) do{ }while(0)
#endif

static void DDRPreUpdateRef(uint32 kHz)
{
    ddrSREF = (((62*kHz)/1000) >> 3) & 0x3FFF;  // 62/8 = 7.75us
    
    ddrRASMAX = (70000*kHz/1000000) + ((((70000*kHz)%1000000) > 0) ? 1:0);
    ddrRASMAX = (ddrRASMAX > 0xFFFF) ? 0xFFFF : ddrRASMAX;
}

static void __tcmfunc DDRUpdateRef(void)
{
    volatile uint32 value = 0;
	
    value = pDDR_Reg->CTRL_REG_51;
    value &= 0x3FFF;
    value |= ddrSREF;
    pDDR_Reg->CTRL_REG_51 = value;
    pDDR_Reg->CTRL_REG_48 = TPHYUPD_RESP(value) | TCTRLUPD_MAX(value);

    //tXSNR =û������ֵ�ʱ�䣬��DDRII��Ҳ������� =  tRFC + 10ns, tRAS_max = 70000ns
    value = pDDR_Reg->CTRL_REG_66;
    value &= ~(0xFFFF);
    pDDR_Reg->CTRL_REG_66 = value | TRAS_MAX(ddrRASMAX);
}

 
static  uint32 PLLGetDDRFreq(void)
{

#define DIV1  (0x00 << 30)
#define DIV2  (0x01 << 30)
#define DIV4  (0x02 << 30)
#define DIV8  (0x03 << 30)
#define EXT_OSC_CLK		24000
#define LOADER_DDR_FREQ_KHZ  133000
	uint32 codecfrqkHz,ddrfrqkHz,codecpll_nf,codecpll_nr,codecpll_od,ddrclk_div;
	codecpll_nr = ((pSCU_Reg->SCU_CPLL_CON & 0x003f0000) >> 16 ) + 1; /*NR = CLKR[5:0] + 1*/
	codecpll_nf = ((pSCU_Reg->SCU_CPLL_CON & 0x0000fff0) >> 4 ) + 1;	/*NF = CLKF[11:0] + 1*/
	codecpll_od = ((pSCU_Reg->SCU_CPLL_CON & 0x0000000e) >> 1 ) + 1;	/*OD = CLKOD[2:0] + 1*/
	ddrclk_div =(pSCU_Reg->SCU_CLKSEL0_CON & 0xc0000000);
	switch(ddrclk_div)
	{
		case DIV1 :
			ddrclk_div = 1;
			//printk("%s-ddr clk div =%d->%d\n",__FUNCTION__,ddrclk_div,__LINE__);
			break;
		case DIV2 :
			ddrclk_div = 2;
			//printk("%s-ddr clk div =%d->%d\n",__FUNCTION__,ddrclk_div,__LINE__);
			break;
		case DIV4 :
			ddrclk_div = 4;
			//printk("%s-ddr clk div =%d->%d\n",__FUNCTION__,ddrclk_div,__LINE__);
			break;
		case DIV8 :
			ddrclk_div = 8;
			//printk("%s-ddr clk div =%d->%d\n",__FUNCTION__,ddrclk_div,__LINE__);
			break;
		default :
			ddrclk_div = 1;
			break;
	}
	#if 0
	if((armpll_nr == 0)||(armpll_od == 0))
		//printk("codec current clk error !! flag armpll_nr ==%d armpll_od==%d\n",armpll_nr,armpll_od);
	if(((EXT_OSC_CLK/armpll_nr) < 9 ) && ((EXT_OSC_CLK/armpll_nr) > 800))
		//printk(" codec current freq/nr range error\n");
	if(((EXT_OSC_CLK/armpll_nr)*armpll_nf < 160 ) && ((EXT_OSC_CLK/armpll_nr)*armpll_nf > 800))
		//printk(" codec current freq/nr*nf range error\n");
	#endif
	/*the output frequency  Fout = EXT_OSC_CLK * armpll_nf/armpll_nr/armpll_od */
	codecfrqkHz = EXT_OSC_CLK * codecpll_nf;
	codecfrqkHz = codecfrqkHz/codecpll_nr;
	codecfrqkHz = codecfrqkHz/codecpll_od;
	ddrfrqkHz = codecfrqkHz/ddrclk_div;	/*calculate DDR current frquency*/
	//printk("SCU_CPLL_CON ==%x codecpll_nr == %d codecpll_nf == %d codecpll_od ==%d\n",pSCU_Reg->SCU_CPLL_CON,armpll_nr,armpll_nf,armpll_od);
	//printk("get current freq codecfrqkhz==%d ddfrqkHz==%d\n",armfrqkHz,ddrfrqkHz);
	if(ddrfrqkHz == 0)
		return LOADER_DDR_FREQ_KHZ;
	return ddrfrqkHz;
}
static void DDRPreUpdateTiming(uint32 kHz)
{
    uint32 tmp;
    uint32 tmp2;
    uint32 tmp3;
    uint32 memType = DDR_MEM_TYPE();// (read32(CPU_APB_REG0) >> 11) & 0x3;
    
    //ʱ��
    if(memType == DDRII)
    {
        // tRRD = 10ns, tCKE = 3 tCK
        tmp = (10*kHz/1000000) + ((((10*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 7) ? 7 : tmp;
        ddrCTRL_REG_23 = TRRD(tmp) | TCKE(3) | 0x2;
        // tRTP = 7.5ns
        tmp = (8*kHz/1000000) + ((((8*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 7) ? 7 : tmp;
        ddrCTRL_REG_24 = TRTP(tmp) | 0x1010100;
        // tMRD = 2 tCK, tDAL = tWR + tRP, tCKESR=û�ҵ�=tCKE, tCCD = 2 tCK
        tmp = (30*kHz/1000000) + ((((30*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 0x1F) ? 0x1F : tmp;
        ddrCTRL_REG_37 = TMRD(2) | TDAL(tmp) | TCKESR(3) | TCCD(2);
        //tRP_ALL = û�ҵ� = tRP + 1 tCK, tRP = 15ns
        tmp = (15*kHz/1000000) + ((((15*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 0xF) ? 0xF : tmp;
        ddrCTRL_REG_34 = CS0_TRP_ALL(((tmp == 0xF) ? 0xF : (tmp+1))) | TRP(tmp) | 0x200;
        //tWTR = 10ns
        tmp2 = (10*kHz/1000000) + ((((10*kHz)%1000000) > 0) ? 1:0);
        tmp2 = (tmp2 > 0xF) ? 0xF : tmp2;
        ddrCTRL_REG_35 = CS1_TRP_ALL(tmp) | TWTR(tmp2);
        //tRC = 65ns, tFAW = 37.5ns, tWR = 15ns
        tmp = (65*kHz/1000000) + ((((65*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 0x3F) ? 0x3F : tmp;
        tmp2 = (38*kHz/1000000) + ((((38*kHz)%1000000) > 0) ? 1:0);
        tmp2 = (tmp2 > 0x3F) ? 0x3F : tmp2;
        tmp3 = (15*kHz/1000000) + ((((15*kHz)%1000000) > 0) ? 1:0);
        tmp3 = (tmp3 > 0x1F) ? 0x1F : tmp3;
        ddrCTRL_REG_38 = TRC(tmp) | TFAW(tmp2) | TWR(tmp3);
        //tMOD = 12ns
        tmp = (12*kHz/1000000) + ((((12*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 0xFF) ? 0xFF : tmp;
        ddrCTRL_REG_43 = TMOD(tmp);
        if(capability <= 0x2000000)  // 256Mb
        {
            tmp = (75*kHz/1000000) + ((((75*kHz)%1000000) > 0) ? 1:0);
            tmp = (tmp > 0xFF) ? 0xFF : tmp;
            //tXSNR = tRFC + 10ns, tRAS_max = 70000ns
            tmp3 = (85*kHz/1000000) + ((((85*kHz)%1000000) > 0) ? 1:0);
            tmp3 = (tmp3 > 0xFFFF) ? 0xFFFF : tmp3;
            ddrCTRL_REG_66 = TXSNR(tmp3);
        }
        else if(capability <= 0x4000000) // 512Mb
        {
            tmp = (105*kHz/1000000) + ((((105*kHz)%1000000) > 0) ? 1:0);
            tmp = (tmp > 0xFF) ? 0xFF : tmp;
            //tXSNR = tRFC + 10ns, tRAS_max = 70000ns
            tmp3 = (115*kHz/1000000) + ((((115*kHz)%1000000) > 0) ? 1:0);
            tmp3 = (tmp3 > 0xFFFF) ? 0xFFFF : tmp3;
            ddrCTRL_REG_66 = TXSNR(tmp3);
        }
        else if(capability <= 0x8000000)  // 1Gb
        {
            tmp = (128*kHz/1000000) + ((((128*kHz)%1000000) > 0) ? 1:0);
            tmp = (tmp > 0xFF) ? 0xFF : tmp;
            //tXSNR = tRFC + 10ns, tRAS_max = 70000ns
            tmp3 = (138*kHz/1000000) + ((((138*kHz)%1000000) > 0) ? 1:0);
            tmp3 = (tmp3 > 0xFFFF) ? 0xFFFF : tmp3;
            ddrCTRL_REG_66 = TXSNR(tmp3);
        }
        else  // 4Gb
        {
            tmp = (328*kHz/1000000) + ((((328*kHz)%1000000) > 0) ? 1:0);
            tmp = (tmp > 0xFF) ? 0xFF : tmp;
            //tXSNR = tRFC + 10ns, tRAS_max = 70000ns
            tmp3 = (338*kHz/1000000) + ((((338*kHz)%1000000) > 0) ? 1:0);
            tmp3 = (tmp3 > 0xFFFF) ? 0xFFFF : tmp3;
            ddrCTRL_REG_66 = TXSNR(tmp3);
        }
        //tRFC = 75ns(256Mb)/105ns(512Mb)/127.5ns(1Gb)/327.5ns(4Gb), tRCD = 20ns, tRAS_min = 40ns
        tmp2 = (20*kHz/1000000) + ((((20*kHz)%1000000) > 0) ? 1:0);
        tmp2 = (tmp2 > 0xFF) ? 0xFF : tmp2;
        tmp3 = (40*kHz/1000000) + ((((40*kHz)%1000000) > 0) ? 1:0);
        tmp3 = (tmp3 > 0xFF) ? 0xFF : tmp3;
        ddrCTRL_REG_44 = TRFC(tmp) | TRCD(tmp2) | TRAS_MIN(tmp3);
    }
    else
    {
        // tRRD = 15ns, tCKE = 2 tCK
        tmp = (15*kHz/1000000) + ((((15*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 7) ? 7 : tmp;
        ddrCTRL_REG_23 = TRRD(tmp) | TCKE(2) | 0x2;
        // tRTP = û�ҵ�,��DDRII��7.5ns
        tmp = (8*kHz/1000000) + ((((8*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 7) ? 7 : tmp;
        ddrCTRL_REG_24 = TRTP(tmp) | 0x1000100;
        // tMRD = 2 tCK, tDAL = tWR + tRP, tCKESR=û�ҵ�=tCKE, tCCD = û�ҵ�
        tmp = (45*kHz/1000000) + ((((45*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 0x1F) ? 0x1F : tmp;
        ddrCTRL_REG_37 = TMRD(2) | TDAL(tmp) | TCKESR(2) | TCCD(2);
        //tRP_ALL = û�ҵ� = tRP + 1 tCK, tRP = 30ns
        tmp = (30*kHz/1000000) + ((((30*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 0xF) ? 0xF : tmp;
        ddrCTRL_REG_34 = CS0_TRP_ALL(((tmp == 0xF) ? 0xF : (tmp+1))) | TRP(tmp) | 0x200;
        //tWTR = 1 tCK
        ddrCTRL_REG_35 = CS1_TRP_ALL(tmp) | TWTR(1);
        //tRC = 80ns, tFAW = û�����ʱ��, tWR = 15ns
        tmp = (80*kHz/1000000) + ((((80*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 0x3F) ? 0x3F : tmp;
        tmp2 = (38*kHz/1000000) + ((((38*kHz)%1000000) > 0) ? 1:0);
        tmp2 = (tmp2 > 0x3F) ? 0x3F : tmp2;
        tmp3 = (15*kHz/1000000) + ((((15*kHz)%1000000) > 0) ? 1:0);
        tmp3 = (tmp3 > 0x1F) ? 0x1F : tmp3;
        ddrCTRL_REG_38 = TRC(tmp) | TFAW(tmp2) | TWR(tmp3);
        //tMOD = û�����ʱ��
        tmp = (12*kHz/1000000) + ((((12*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 0xFF) ? 0xFF : tmp;
        ddrCTRL_REG_43 = TMOD(tmp);
        if(capability <= 0x2000000)  // 128Mb,256Mb
        {
            tmp = (80*kHz/1000000) + ((((80*kHz)%1000000) > 0) ? 1:0);
            tmp = (tmp > 0xFF) ? 0xFF : tmp;
            //tXSNR = tRFC + 10ns, tRAS_max = 70000ns
            tmp3 = (90*kHz/1000000) + ((((90*kHz)%1000000) > 0) ? 1:0);
            tmp3 = (tmp3 > 0xFFFF) ? 0xFFFF : tmp3;
            ddrCTRL_REG_66 = TXSNR(tmp3);
        }
        else if(capability <= 0x4000000) // 512Mb
        {
            tmp = (110*kHz/1000000) + ((((110*kHz)%1000000) > 0) ? 1:0);
            tmp = (tmp > 0xFF) ? 0xFF : tmp;
            //tXSNR = tRFC + 10ns, tRAS_max = 70000ns
            tmp3 = (120*kHz/1000000) + ((((120*kHz)%1000000) > 0) ? 1:0);
            tmp3 = (tmp3 > 0xFFFF) ? 0xFFFF : tmp3;
            ddrCTRL_REG_66 = TXSNR(tmp3);
        }
        else if(capability <= 0x8000000)  // 1Gb
        {
            tmp = (140*kHz/1000000) + ((((140*kHz)%1000000) > 0) ? 1:0);
            tmp = (tmp > 0xFF) ? 0xFF : tmp;
            //tXSNR = tRFC + 10ns, tRAS_max = 70000ns
            tmp3 = (150*kHz/1000000) + ((((150*kHz)%1000000) > 0) ? 1:0);
            tmp3 = (tmp3 > 0xFFFF) ? 0xFFFF : tmp3;
            ddrCTRL_REG_66 = TXSNR(tmp3);
        }
        else  // 4Gb
        {
            tmp = (328*kHz/1000000) + ((((328*kHz)%1000000) > 0) ? 1:0);
            tmp = (tmp > 0xFF) ? 0xFF : tmp;
            //tXSNR = tRFC + 10ns, tRAS_max = 70000ns
            tmp3 = (338*kHz/1000000) + ((((338*kHz)%1000000) > 0) ? 1:0);
            tmp3 = (tmp3 > 0xFFFF) ? 0xFFFF : tmp3;
            ddrCTRL_REG_66 = TXSNR(tmp3);
        }
        //tRFC = 80ns(128Mb,256Mb)/110ns(512Mb)/140ns(1Gb), tRCD = 30ns, tRAS_min = 45ns
        tmp2 = (30*kHz/1000000) + ((((30*kHz)%1000000) > 0) ? 1:0);
        tmp2 = (tmp2 > 0xFF) ? 0xFF : tmp2;
        tmp3 = (45*kHz/1000000) + ((((45*kHz)%1000000) > 0) ? 1:0);
        tmp3 = (tmp3 > 0xFF) ? 0xFF : tmp3;
        ddrCTRL_REG_44 = TRFC(tmp) | TRCD(tmp2) | TRAS_MIN(tmp3);
        //tXSR = 200 ns
        tmp = (200*kHz/1000000) + ((((200*kHz)%1000000) > 0) ? 1:0);
        tmp = (tmp > 0xFFFF) ? 0xFFFF : tmp;
        ddrCTRL_REG_67 = TXSR(tmp);
    }
    //tCPD = 400ns
    tmp = (400*kHz/1000000) + ((((400*kHz)%1000000) > 0) ? 1:0);
    tmp = (tmp > 0xFFFF) ? 0xFFFF : tmp;
    ddrCTRL_REG_64 = TCPD(tmp) | MODE3_CNT(0x3FFF);
    //tINIT = 200us
    tmp = (200000*kHz/1000000) + ((((200000*kHz)%1000000) > 0) ? 1:0);
    tmp = (tmp > 0xFFFFFF) ? 0xFFFFFF : tmp;
    ddrCTRL_REG_68 = TINIT(tmp);
}

static void __tcmfunc DDRUpdateTiming(void)
{
    uint32 value;
    uint32 memType = DDR_MEM_TYPE();// (read32(CPU_APB_REG0) >> 11) & 0x3;
    
    //ʱ��
    if(memType == DDRII)
    {
        value = pDDR_Reg->CTRL_REG_35;
        value &= ~(0xF0F);
        pDDR_Reg->CTRL_REG_35 = value | ddrCTRL_REG_35;
    }
    else
    {
        //tWTR = 1 tCK
        value = pDDR_Reg->CTRL_REG_35;
        value &= ~(0xF);
        pDDR_Reg->CTRL_REG_35 = value | ddrCTRL_REG_35;
        pDDR_Reg->CTRL_REG_67 = ddrCTRL_REG_67;
    }
    pDDR_Reg->CTRL_REG_23 = ddrCTRL_REG_23;
    pDDR_Reg->CTRL_REG_24 = ddrCTRL_REG_24;
    pDDR_Reg->CTRL_REG_37 = ddrCTRL_REG_37;
    pDDR_Reg->CTRL_REG_34 = ddrCTRL_REG_34;
    pDDR_Reg->CTRL_REG_38 = ddrCTRL_REG_38;
    pDDR_Reg->CTRL_REG_43 = ddrCTRL_REG_43;
    value = pDDR_Reg->CTRL_REG_66;
    value &= ~(0xFFFF0000);
    pDDR_Reg->CTRL_REG_66 = ddrCTRL_REG_66 | value;
    pDDR_Reg->CTRL_REG_44 = ddrCTRL_REG_44;
    pDDR_Reg->CTRL_REG_64 = ddrCTRL_REG_64;
    pDDR_Reg->CTRL_REG_68 = ddrCTRL_REG_68;
}
/****************************************************************/
//������:SDRAM_BeforeUpdateFreq
//����:����SDRAM/DDRƵ��ǰ���õĺ��������ڵ���SDRAM/DDRʱ�����
//����˵��:SDRAMnewKHz   �������   SDRAM��Ҫ��������Ƶ�ʣ���λKHz
//         DDRnewKHz     �������   DDR��Ҫ��������Ƶ�ʣ���λKHz
//����ֵ:
//���ȫ�ֱ���:
//ע��:�������ֻ�޸�MSDR_STMG0R��MSDR_SREFR��MSDR_SCTLR��ֵ
/****************************************************************/
static void __tcmfunc SDRAM_BeforeUpdateFreq(uint32 SDRAMnewKHz, uint32 DDRnewKHz)
{
    uint32 KHz;
    uint32 memType = DDR_MEM_TYPE();// (pGRF_Reg->CPU_APB_REG0) & MEMTYPEMASK;
    uint32 tmp;
	volatile uint32 *p_ddr = (volatile uint32 *)0xc0080000;
	//uint32 *ddr_reg = 0xff401c00;
	ddr_reg[0] = pGRF_Reg->CPU_APB_REG0;
	ddr_reg[1] = pGRF_Reg->CPU_APB_REG1;
	
	
	ddr_reg[4] = pDDR_Reg->CTRL_REG_10;
	
	ddr_reg[6] = pDDR_Reg->CTRL_REG_78;
    switch(memType)
    {
        case Mobile_SDRAM:
        case SDRAM:
			printk("%s:erroe memtype=0x%lx\n" ,__func__, memType);
			#if 0
            KHz = PLLGetAHBFreq();
            if(KHz < SDRAMnewKHz)  //��Ƶ
            {
                SDRAMUpdateTiming(SDRAMnewKHz);
                bFreqRaise = 1;
            }
            else //��Ƶ
            {
                SDRAMUpdateRef(SDRAMnewKHz);
                bFreqRaise = 0;
            }
			#endif
            break;
        case DDRII:
        case Mobile_DDR:
            KHz = PLLGetDDRFreq();
            if(telement)
            {
                elementCnt = 250000000/(KHz*telement);
                if(elementCnt > 156)
                {
                    elementCnt = 156;
                }
            }
            else
            {
                elementCnt = 2778000/KHz;  // 90ps��
                if(elementCnt > 156)
                {
                    elementCnt = 156;
                }
            }
            DDRPreUpdateRef(DDRnewKHz);
            DDRPreUpdateTiming(DDRnewKHz);
            
            while(pGRF_Reg->CPU_APB_REG1 & 0x100);
			tmp = *p_ddr;  //read to wakeup
                pDDR_Reg->CTRL_REG_36 &= ~(0x1F << 8);
            while(pDDR_Reg->CTRL_REG_03 & 0x100)
            {         	
                tmp = *p_ddr;  //read to wakeup
            }					
            while(pGRF_Reg->CPU_APB_REG1 & 0x100);
            printk("%s::just befor ddr refresh.ahb=%ld,new ddr=%ld\n" , __func__ , SDRAMnewKHz , DDRnewKHz);
            //WAIT_ME();
            pDDR_Reg->CTRL_REG_09 |= (0x1 << 24);	// after this on,ddr enter refresh,no access valid!!
            //WAIT_ME();
            while(!(pDDR_Reg->CTRL_REG_03 & 0x100));
            pDDR_Reg->CTRL_REG_10 &= ~(0x1);
            if(memType == DDRII)
            {
                if(333000 < KHz)
                {
                    pDDR_Reg->CTRL_REG_82 = 0x00885555;
                }
                else
                {
                    pDDR_Reg->CTRL_REG_82 = 0x00685555;
                }
            }
            else
            {
                if(133000 < KHz)
                {
                    pDDR_Reg->CTRL_REG_82 = 0x00840000; 
                }
                else
                {
                    pDDR_Reg->CTRL_REG_82 = 0x00640000; 
                }
            }
            if(110000 > KHz)
            {
                DLLBypass(KHz);
            }
            if(KHz < DDRnewKHz)  //��Ƶ
            {
                DDRUpdateTiming();
                bFreqRaise = 1;
            }
            else //��Ƶ
            {
                DDRUpdateRef();
                bFreqRaise = 0;
            }
            break;
    }
}

/****************************************************************/
//������:SDRAM_AfterUpdateFreq
//����:SDRAM/DDRƵ�ʵ����󣬵��������������ɵ�Ƶ����ƺ���
//����˵��:SDRAMoldKHz   �������   SDRAM����ǰ��Ƶ�ʣ���λKHz
//         DDRoldKHz     �������   SDRAM����ǰ��Ƶ�ʣ���λKHz
//����ֵ:
//���ȫ�ֱ���:
//ע��:�������ֻ�޸�MSDR_STMG0R��MSDR_SREFR��MSDR_SCTLR��ֵ
/****************************************************************/

static void __tcmfunc SDRAM_AfterUpdateFreq(uint32 SDRAMoldKHz, uint32 DDRnewKHz)
{
    uint32 value =0;
    //uint32 tmp = 0;
    uint32 ddrKHz;
    //uint32 ahbKHz;
    uint32 memType = DDR_MEM_TYPE();// (pGRF_Reg->CPU_APB_REG0) & MEMTYPEMASK;
	DDR_debug_string("21\n");
    switch(memType)
    {
        case Mobile_SDRAM:
        case SDRAM:
			printk("%s:erroe memtype=0x%lx\n" ,__func__, memType);
			#if 0
           // ahbKHz = PLLGetAHBFreq();
            if(ahbKHz > SDRAMoldKHz)  //��Ƶ
            {
               // SDRAMUpdateRef(ahbKHz);
            }
            else //��Ƶ
            {
              //  SDRAMUpdateTiming(ahbKHz);
            }
			#endif
            break;
        case DDRII:
        case Mobile_DDR:
            if(bFreqRaise)  //��Ƶ
            {
                DDRUpdateRef();
            }
            else //��Ƶ
            {
                DDRUpdateTiming();
            }
			ddrKHz = DDRnewKHz;
            pDDR_Reg->CTRL_REG_10 |= 0x1;
            value = 1000;
            while(value)
            {
                if(pDDR_Reg->CTRL_REG_04 & 0x100)
                {
                    break;
                }
                value--;
            }
            if(!(value))
            {
                DLLBypass(ddrKHz);
                pDDR_Reg->CTRL_REG_10 |= 0x1;
                while(!(pDDR_Reg->CTRL_REG_04 & 0x100));
            }
            pDDR_Reg->CTRL_REG_09 &= ~(0x1 << 24);
            while(pDDR_Reg->CTRL_REG_03 & 0x100); // exit 
                printk("exit ddr refresh,");
            //�˳���ˢ�º�����element��ֵ
            ddrKHz = PLLGetDDRFreq();
			printk("new ddr kHz=%ld\n" , ddrKHz);
            if(110000 < ddrKHz)
            {
                value = pDDR_Reg->CTRL_REG_78;
                if(value & 0x1)
                {
                    value = value >> 1;
                    telement = 1000000000/(ddrKHz*value);
                }
            }
            pDDR_Reg->CTRL_REG_36 = (0x1F1F); // 20100608,YK@RK.
            DDR_ENABLE_SLEEP();
            break;
    }
}

static void __tcmlocalfunc ddr_pll_delay( int loops ) 
{
        volatile int i;
        for( i = loops ; i > 0 ; i-- ){
                ;
        }
}

static unsigned long ddr_save_sp( unsigned long new_sp );
asm(	
"	.section \".tcm.text\",\"ax\"\n"	
"	.align\n"
"	.type	ddr_save_sp, #function\n"
"ddr_save_sp:\n"
"	mov r1,sp\n"	
"	mov sp,r0\n"	
"	mov r0,r1\n"	
"	mov pc,lr\n"
"	.previous"
);
/*SCU PLL CON , 20100518,copy from rk28_scu_hw.c
*/
#define PLL_TEST        (0x01u<<25)
#define PLL_SAT         (0x01u<<24)
#define PLL_FAST        (0x01u<<23)
#define PLL_PD          (0x01u<<22)
#define PLL_CLKR(i)     (((i)&0x3f)<<16)
#define PLL_CLKF(i)     (((i)&0x0fff)<<4)
#define PLL_CLKOD(i)    (((i)&0x07)<<1)
#define PLL_BYPASS      (0X01)

#define DDR_CLK_NORMAL          300
#define DDR_CLK_LOWPW            128
/* 20100609,HSL@RK, dtcm --> sram,when dsp close,sram clk will be close,so 
 *  we use itcm as tmp stack.
*/
#define DDR_SAVE_SP     do{save_sp = ddr_save_sp((DTCM_END&(~7)));}while(0)
#define DDR_RESTORE_SP do{ ddr_save_sp(save_sp); }while(0)
static void __tcmfunc  ddr_change_freq(int freq_MHZ)
{
	int arm_clk = rockchip_clk_get_ahb()/1000;
	uint32 DDRnewKHz = freq_MHZ*1000;
	//printk("%s::ahb clk=%d KHZ\n",__func__ , ahb );
	
	SDRAM_BeforeUpdateFreq( arm_clk,  DDRnewKHz);
	pSCU_Reg->SCU_CLKSEL1_CON &= ~(0X03);
        	if(  freq_MHZ >= 200 ) {
        		pSCU_Reg->SCU_CPLL_CON = PLL_SAT|PLL_FAST
        		|(PLL_CLKR(6-1))|(PLL_CLKF(freq_MHZ/4-1));
        	 } else {
        		pSCU_Reg->SCU_CPLL_CON = PLL_SAT|PLL_FAST
        		|(PLL_CLKR(6-1))|(PLL_CLKF(freq_MHZ/2-1))
        		|(PLL_CLKOD(2 - 1));
        	}
                ddr_pll_delay( arm_clk );
	pSCU_Reg->SCU_CLKSEL1_CON |= 0X01;
	SDRAM_AfterUpdateFreq( arm_clk,  DDRnewKHz);
}


/****************************************************************/
//������:SDRAM_EnterSelfRefresh
//����:SDRAM������ˢ��ģʽ
//����˵��:
//����ֵ:
//���ȫ�ֱ���:
//ע��:(1)ϵͳ��ȫidle����ܽ�����ˢ��ģʽ��������ˢ�º����ٷ���SDRAM
//     (2)Ҫ������ˢ��ģʽ�����뱣֤����ʱ������������õ������д��벻��SDRAM��
/****************************************************************/
static void __tcmfunc rk28_ddr_enter_self_refresh(void)
{
    uint32 memType = DDR_MEM_TYPE();
    
    switch(memType)
    {
        case DDRII:
        case Mobile_DDR:
    
            pDDR_Reg->CTRL_REG_62 = (pDDR_Reg->CTRL_REG_62 &
                (~(0xFFFF<<16))) | MODE5_CNT(0x1);
            ddr_pll_delay( 100 );
            pSCU_Reg->SCU_CLKGATE1_CON |= 0x00c00001;
            ddr_pll_delay( 10 );
            pSCU_Reg->SCU_CPLL_CON |= ((1<<22)|1);
            ddr_pll_delay( 10 );
            break;
        default:
                break;
    }
}

/****************************************************************/
//������:SDRAM_ExitSelfRefresh
//����:SDRAM�˳���ˢ��ģʽ
//����˵��:
//����ֵ:
//���ȫ�ֱ���:
//ע��:(1)SDRAM����ˢ��ģʽ���ܱ����ʣ��������˳���ˢ��ģʽ
//     (2)���뱣֤����ʱ��������Ĵ��벻��SDRAM��
/****************************************************************/
static void __tcmfunc rk28_ddr_exit_self_refresh(void)
{
    uint32 memType = DDR_MEM_TYPE();

    switch(memType)
    {
        case DDRII:
        case Mobile_DDR:
                pSCU_Reg->SCU_CPLL_CON &= ~((1<<22)|1);
            ddr_pll_delay( 3000 );
                pSCU_Reg->SCU_CLKGATE1_CON &= ~0x00c00000;
            ddr_pll_delay( 100 );
            pDDR_Reg->CTRL_REG_62 = (pDDR_Reg->CTRL_REG_62 & (~(0xFFFF<<16))
                )| MODE5_CNT(0xFFFF);
            ddr_pll_delay( 1000 );
            break;
        default:
                break;
    }
    
}

static uint32 scu_clk_gate[3];
static uint32 scu_pmu_con;

static void scu_reg_save(void)
{
    // bit 31: 1.1host phy. bit 2: 2.0host.
    //pGRF_Reg->OTGPHY_CON1 = 0x80000004;
    pGRF_Reg->OTGPHY_CON1 |= 0x80000000;
    scu_clk_gate[0] = pSCU_Reg->SCU_CLKGATE0_CON;
    scu_clk_gate[1] = pSCU_Reg->SCU_CLKGATE1_CON;
    scu_clk_gate[2] = pSCU_Reg->SCU_CLKGATE2_CON;
    // open uart0,1(bit 18,19,for debug), open gpio0,1(bit16,17) and arm,arm core.
    // XXX:open pwm(bit24) for ctrl vdd core??.
    // open timer (bit 25) for usb detect and mono timer.
    pSCU_Reg->SCU_CLKGATE0_CON = 0xfdf0fff6; //0xfefcfff6 ;// arm core clock
    // 
    pSCU_Reg->SCU_CLKGATE1_CON = 0xff3f8001;    //sdram clock 0xe7fff
    pSCU_Reg->SCU_CLKGATE2_CON = 0x24;       //ahb, apb clock

    scu_pmu_con = pSCU_Reg->SCU_PMU_CON;
    
    // XXX:close LCDC power domain(bit 3) need to reset lcdc ctrl regs when wake up.
    // about 1ma vdd core.
    //pSCU_Reg->SCU_PMU_CON = 0x09;//power down lcdc & dsp power domain
    pSCU_Reg->SCU_PMU_CON = 0x01;//power downdsp power domain
}
static void scu_reg_restore(void)
{
    pSCU_Reg->SCU_PMU_CON = scu_pmu_con;//power down lcdc & dsp power domain
    
    pSCU_Reg->SCU_CLKGATE0_CON = scu_clk_gate[0]; 
    pSCU_Reg->SCU_CLKGATE1_CON = scu_clk_gate[1]; 
    pSCU_Reg->SCU_CLKGATE2_CON = scu_clk_gate[2]; 
    
}
/****************************************************************/
//������:SDRAM_Init
//����:SDRAM��ʼ��
//����˵��:
//����ֵ:
//���ȫ�ֱ���:
//ע��:
/****************************************************************/
static void  SDRAM_DDR_Init(void)
{
	uint32 			value;
    uint32          KHz;
    uint32          memType = DDR_MEM_TYPE();// (pGRF_Reg->CPU_APB_REG0) & MEMTYPEMASK;
    unsigned long flags;
    
    telement = 0;
    if(memType == DDRII)
    {
        pDDR_Reg->CTRL_REG_82 = 0x00685555;
                //tXSR = tXSRD = 200 tCK
        pDDR_Reg->CTRL_REG_67 = TXSR(200);
     }
     else
     {
        pDDR_Reg->CTRL_REG_11 = 0x101;
        pDDR_Reg->CTRL_REG_82 = 0x00640000; 
     }
            /* 20100609,HSL@RK,disable ddr interrupt .*/
            pDDR_Reg->CTRL_REG_45 = 0x07ff03ff; 
            pDDR_Reg->CTRL_REG_63 &= 0x0000ffff; 
            //tPDEX=û�ҵ���Ӧ�õ���=tXP=25ns 
            pDDR_Reg->CTRL_REG_65 = TPDEX(0x2) | TDLL(200);
            //������ʱ�˿����ȼ���:VIDEO(port 7) > CEVA(port 6) > ARMI(port 5) > ARMD(port 4) > LCDC(port 2) > EXP(port3)
            //д����ʱ�˿����ȼ���:VIDEO(port 7) > CEVA(port 6) > ARMD(port 4) > EXP(port3) > LCDC(port 2) > ARMI(port 5)
            pDDR_Reg->CTRL_REG_13 = (AXI_MC_ASYNC << 8) | LCDC_WR_PRIO(3) | LCDC_RD_PRIO(3);
            pDDR_Reg->CTRL_REG_14 = (AXI_MC_ASYNC << 24) | (AXI_MC_ASYNC) | EXP_WR_PRIO(3) | EXP_RD_PRIO(3);
            pDDR_Reg->CTRL_REG_15 = (AXI_MC_ASYNC << 16) | ARMD_WR_PRIO(2) | ARMD_RD_PRIO(3) | ARMI_RD_PRIO(2);
            pDDR_Reg->CTRL_REG_16 = (AXI_MC_ASYNC << 8) | CEVA_WR_PRIO(1) | CEVA_RD_PRIO(1) | ARMI_WR_PRIO(3);
            capability = (0x1 << ((0x2 >> ((pDDR_Reg->CTRL_REG_08 >> 16) & 0x1))  //bus width
                                    + (13 - (pDDR_Reg->CTRL_REG_21 & 0x7))  //col
                                    + (2 + ((pDDR_Reg->CTRL_REG_05 >> 8) & 0x1))  //bank
                                    + (15 - ((pDDR_Reg->CTRL_REG_19 >> 24) & 0x7))));  //row
            if(((pDDR_Reg->CTRL_REG_17 >> 24) & 0x3) == 0x3)
            {
                if(capability < (0x1 << ((0x2 >> ((pDDR_Reg->CTRL_REG_08 >> 16) & 0x1))  //bus width
                                    + (13 - ((pDDR_Reg->CTRL_REG_21 >> 8) & 0x7))  //col
                                    + (2 + ((pDDR_Reg->CTRL_REG_05 >> 16) & 0x1))  //bank
                                    + (15 - (pDDR_Reg->CTRL_REG_20 & 0x7))))) //row
                {
                    capability = (0x1 << ((0x2 >> ((pDDR_Reg->CTRL_REG_08 >> 16) & 0x1))  //bus width
                                    + (13 - ((pDDR_Reg->CTRL_REG_21 >> 8) & 0x7))  //col
                                    + (2 + ((pDDR_Reg->CTRL_REG_05 >> 16) & 0x1))  //bank
                                    + (15 - (pDDR_Reg->CTRL_REG_20 & 0x7))));  //row
                }
            }
            KHz = PLLGetDDRFreq();
                printk("PLLGetDDRFreq=%ld KHZ\n" , KHz);
            if(110000 > KHz)
            {
                DLLBypass(KHz);
            }
            else
            {
                value = pDDR_Reg->CTRL_REG_78;
                if(value & 0x1)
                {
                    value = value >> 1;
                    telement = 1000000000/(KHz*value);
                }
            }
            DDRPreUpdateRef(KHz);
            DDRPreUpdateTiming(KHz);
            DDRUpdateRef();
            DDRUpdateTiming();
            DDR_ENABLE_SLEEP();
			
	//printk("..%s -->capability ==%ld telement==%ld -->%d\n",__FUNCTION__,capability,telement,__LINE__);
        local_irq_save(flags);
        DDR_SAVE_SP;
        ddr_change_freq( memType == DDRII ? 300 : 166 );  // DDR CLK!
        DDR_RESTORE_SP;
        local_irq_restore(flags);
}




/****************************************************************
* ������:disable_DDR_Sleep
* ����:��ֹ�Զ�sleepģʽ
* ����˵��:
* ����ֵ:
* ���ȫ�ֱ���:
****************************************************************/
static void __tcmlocalfunc  disable_DDR_Sleep(void)
{
	volatile uint32 *p_ddr = (volatile uint32 *)0xc0080000;
	unsigned int tmp;
	while(pGRF_Reg->CPU_APB_REG1 & 0x100);
		tmp = *p_ddr;  //read to wakeup
		pDDR_Reg->CTRL_REG_36 &= ~(0x1F << 8);
        while(pDDR_Reg->CTRL_REG_03 & 0x100)
        {         	
            tmp = *p_ddr;  //read to wakeup
        }					
        while(pGRF_Reg->CPU_APB_REG1 & 0x100);
}

static void __tcmlocalfunc rk2818_reduce_armfrq(void)
{
#define pSCU_Reg	   ((pSCU_REG)SCU_BASE_ADDR_VA)
#define ARM_FRQ_48MHz    0x018502F6
#define ARM_PLL_PWD	(0x1<<22)
	pSCU_Reg->SCU_MODE_CON &= (~(0x3<<2)); // arm slow mod
	udelay(100);
	pSCU_Reg->SCU_CLKSEL0_CON &= (~(0xf<<0));	//arm:ahb:apb=1:1:1
	udelay(100);
	pSCU_Reg->SCU_APLL_CON =  ARM_FRQ_48MHz;
	udelay(100);
	pSCU_Reg->SCU_APLL_CON |=ARM_PLL_PWD;
	udelay(100);
}
static void __tcmfunc rk281x_reboot( void )
{
#define pSCU_Reg	((pSCU_REG)SCU_BASE_ADDR_VA)
#define g_intcReg 	((pINTC_REG)(INTC_BASE_ADDR_VA))
#if 1
		g_intcReg->FIQ_INTEN &= 0x0;
		g_intcReg->IRQ_INTEN_H &= 0x0;
		g_intcReg->IRQ_INTEN_L &= 0x0;
		g_intcReg->IRQ_INTMASK_L &= 0x0;
		g_intcReg->IRQ_INTMASK_H &= 0x0;	
#endif		
		rk2818_reduce_armfrq();
		disable_DDR_Sleep();
		printk("start reboot!!!\n");
		asm( "MRC p15,0,r0,c1,c0,0\n"
				"BIC r0,r0,#(1<<0)	   @disable mmu\n"
				"BIC r0,r0,#(1<<13)    @set vector to 0x00000000\n"
				"MCR p15,0,r0,c1,c0,0\n"
				"mov r1,r1\n"
				"mov r1,r1\n" 
							

				"ldr r2,=0x18018000        @ set dsp power domain on\n"
  				"ldr r3,[r2,#0x10]\n"
  				"bic r3,r3,#(1<<0)\n"
  				"str r3,[r2,#0x10]\n"
  
 				" ldr r3,[r2,#0x24]     @ enable dsp ahb clk\n"
  				"bic r3,r3,#(1<<2)\n"
  				"str r3,[r2,#0x24]\n"
  
  				"ldr r3,[r2,#0x1c]     @ gate 0 ,enable dsp clk\n"
  				"bic r3,r3,#(1<<1)\n"
  				"str r3,[r2,#0x1c]\n"
  
  				"ldr r3,[r2,#0x28]     @dsp soft reset , for dsp l2 cache as maskrom stack.\n"
  				"ORR r3,r3,#((1<<5))\n"
  				"str r3,[r2,#0x28]\n"
  				"BIC r3,r3,#((1<<5))\n"
  				"str r3,[r2,#0x28]\n"

				"ldr r2,=0x18018000      @ enable sram arm dsp clock\n"
  				"ldr r3,[r2,#0x1c]\n"
  				"bic r3,r3,#(1<<3)\n"
  				"bic r3,r3,#(1<<4)\n"
  				"str r3,[r2,#0x1c]\n"
				
				
				 "ldr r2,=0x10040804        @usb soft disconnect.\n"
                 		" mov r3,#2\n"
               		 "str r3,[r2,#0]\n"

				

               		 "ldr r2,=0x100AE00C       @ BCH reset.\n"
                		" mov r3,#1\n"
                		"str r3,[r2,#0]\n"

				"MRC p15,0,r0,c1,c0,0\n"
				"BIC r0,r0,#(1<<2)	@ disable IDcache \n"
				"MCR p15,0,r0,c1,c0,0\n"
				"mov r0,#0\n"
				"MCR  p15, 0, r0, c7, c7, 0    @flush I-cache & D-cache\n"

				"ldr r2,=0x18019000        @ DisableRemap\n"
  				"ldr r3,[r2,#0x14]\n"
  				"bic r3,r3,#(1<<0)\n"
  				"bic r3,r3,#(1<<1)\n"
  				"str r3,[r2,#0x14]\n"

                #if 1
				"ldr r2,=0x18009000       @rk2818_reduce_corevoltage\n"
				"ldr r3,[r2,#0x24]\n"
				"bic r3,#(1<<6)\n"
				"str r3,[r2,#0x24]\n"
				"ldr r3,[r2,#0x28]\n"
				"bic r3,#(1<<6)\n"
				"str r3,[r2,#0x28]\n"
                #endif
				"mov r12,#0\n"
				"mov pc ,r12\n"
				);

}

void(*rk2818_reboot)(void )= (void(*)(void ))rk281x_reboot;

static int __init update_frq(void)
{
	
	//printk(">>>>>%s-->%d\n",__FUNCTION__,__LINE__);
	int *reg = (int *)(SCU_BASE_ADDR_VA);
	#if 0
	ddr_reg[0] = pGRF_Reg->CPU_APB_REG0;
	ddr_reg[1] = pGRF_Reg->CPU_APB_REG1;
	ddr_reg[2] = pDDR_Reg->CTRL_REG_03;
	ddr_reg[3] = pDDR_Reg->CTRL_REG_09;
	ddr_reg[4] = pDDR_Reg->CTRL_REG_10;
	ddr_reg[5] = pDDR_Reg->CTRL_REG_36;
	ddr_reg[6] = pDDR_Reg->CTRL_REG_78;
	printk(" before 0x%08x 0x%08x 0x%08x 0x%08x\n"
                            "0x%08x 0x%08x 0x%08x \n"
                          //  "0x%08x 0x%08x 0x%08x 0x%08x\n" ,
                            ,ddr_reg[0],ddr_reg[1],ddr_reg[2],ddr_reg[3],
                            ddr_reg[4],ddr_reg[5],ddr_reg[6]);
                 #endif
	//local_irq_disable();
	//printk("wait for jtag...\n");
	//WAIT_ME();
	SDRAM_DDR_Init();
	#if 0
	printk("after SDRAM_DDR_Init\n");
	//local_irq_enable();		
	ddr_reg[0] = pGRF_Reg->CPU_APB_REG0;
	ddr_reg[1] = pGRF_Reg->CPU_APB_REG1;
	ddr_reg[2] = pDDR_Reg->CTRL_REG_03;
	ddr_reg[3] = pDDR_Reg->CTRL_REG_09;
	ddr_reg[4] = pDDR_Reg->CTRL_REG_10;
	ddr_reg[5] = pDDR_Reg->CTRL_REG_36;
	ddr_reg[6] = pDDR_Reg->CTRL_REG_78;
	printk("after 0x%08x 0x%08x 0x%08x 0x%08x\n"
                            "0x%08x 0x%08x 0x%08x \n"
                          //  "0x%08x 0x%08x 0x%08x 0x%08x\n" ,
                            ,ddr_reg[0],ddr_reg[1],ddr_reg[2],ddr_reg[3],
                            ddr_reg[4],ddr_reg[5],ddr_reg[6]);
                #endif
	S_INFO("scu after frq%s::\n0x%08x 0x%08x 0x%08x 0x%08x\n"
                            "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n"
                            "0x%08x 0x%08x 0x%08x 0x%08x\n"
                          //  "0x%08x 0x%08x 0x%08x 0x%08x\n" ,
                            ,__func__,
                            reg[0],reg[1],reg[2],reg[3],
                            reg[4],reg[5],reg[6],reg[7], reg[8],
                            reg[9], reg[10],reg[11], reg[12]);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
	clk_recalculate_root_clocks();
#endif
	return 0;	
}
//core_initcall_sync(update_frq);

#endif //endi of #ifdef DRIVERS_SDRAM
