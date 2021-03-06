#ifndef _AV7110_HW_H_
#define _AV7110_HW_H_

#include "av7110.h"

/* DEBI transfer mode defs */

#define DEBINOSWAP 0x000e0000
#define DEBISWAB   0x001e0000
#define DEBISWAP   0x002e0000

#define ARM_WAIT_FREE  (HZ)
#define ARM_WAIT_SHAKE (HZ/5)
#define ARM_WAIT_OSD (HZ)


enum av7110_bootstate
{
	BOOTSTATE_BUFFER_EMPTY	= 0,
	BOOTSTATE_BUFFER_FULL	= 1,
	BOOTSTATE_BOOT_COMPLETE	= 2
};

enum av7110_type_rec_play_format
{	RP_None,
	AudioPES,
	AudioMp2,
	AudioPCM,
	VideoPES,
	AV_PES
};

enum av7110_osd_palette_type
{
	NoPalet =  0,	   /* No palette */
	Pal1Bit =  2,	   /* 2 colors for 1 Bit Palette    */
	Pal2Bit =  4,	   /* 4 colors for 2 bit palette    */
	Pal4Bit =  16,	   /* 16 colors for 4 bit palette   */
	Pal8Bit =  256	   /* 256 colors for 16 bit palette */
};

enum av7110_window_display_type {
	BITMAP1,	   /* 1 bit bitmap */
	BITMAP2,	   /* 2 bit bitmap */
	BITMAP4,	   /* 4 bit bitmap */
	BITMAP8,	   /* 8 bit bitmap */
	BITMAP1HR,	   /* 1 Bit bitmap half resolution */
	BITMAP2HR,	   /* 2 bit bitmap half resolution */
	BITMAP4HR,	   /* 4 bit bitmap half resolution */
	BITMAP8HR,	   /* 8 bit bitmap half resolution */
	YCRCB422,	   /* 4:2:2 YCRCB Graphic Display */
	YCRCB444,	   /* 4:4:4 YCRCB Graphic Display */
	YCRCB444HR,	   /* 4:4:4 YCRCB graphic half resolution */
	VIDEOTSIZE,	   /* True Size Normal MPEG Video Display */
	VIDEOHSIZE,	   /* MPEG Video Display Half Resolution */
	VIDEOQSIZE,	   /* MPEG Video Display Quarter Resolution */
	VIDEODSIZE,	   /* MPEG Video Display Double Resolution */
	VIDEOTHSIZE,	   /* True Size MPEG Video Display Half Resolution */
	VIDEOTQSIZE,	   /* True Size MPEG Video Display Quarter Resolution*/
	VIDEOTDSIZE,	   /* True Size MPEG Video Display Double Resolution */
	VIDEONSIZE,	   /* Full Size MPEG Video Display */
	CURSOR		   /* Cursor */
};

/* switch defines */
#define SB_GPIO 3
#define SB_OFF	SAA7146_GPIO_OUTLO  /* SlowBlank off (TV-Mode) */
#define SB_ON	SAA7146_GPIO_INPUT  /* SlowBlank on  (AV-Mode) */
#define SB_WIDE SAA7146_GPIO_OUTHI  /* SlowBlank 6V  (16/9-Mode) (not implemented) */

#define FB_GPIO 1
#define FB_OFF	SAA7146_GPIO_LO     /* FastBlank off (CVBS-Mode) */
#define FB_ON	SAA7146_GPIO_OUTHI  /* FastBlank on  (RGB-Mode) */
#define FB_LOOP	SAA7146_GPIO_INPUT  /* FastBlank loop-through (PC graphics ???) */

enum av7110_video_output_mode
{
	NO_OUT	     = 0,		/* disable analog output */
	CVBS_RGB_OUT = 1,
	CVBS_YC_OUT  = 2,
	YC_OUT	     = 3
};

/* firmware internal msg q status: */
#define GPMQFull	0x0001		/* Main Message Queue Full */
#define GPMQOver	0x0002		/* Main Message Queue Overflow */
#define HPQFull		0x0004		/* High Priority Msg Queue Full */
#define HPQOver		0x0008
#define OSDQFull	0x0010		/* OSD Queue Full */
#define OSDQOver	0x0020

/* hw section filter flags */
#define	SECTION_EIT		0x01
#define	SECTION_SINGLE		0x00
#define	SECTION_CYCLE		0x02
#define	SECTION_CONTINUOS	0x04
#define	SECTION_MODE		0x06
#define SECTION_IPMPE		0x0C	/* size up to 4k */
#define SECTION_HIGH_SPEED	0x1C	/* larger buffer */
#define DATA_PIPING_FLAG	0x20	/* for Data Piping Filter */

#define	PBUFSIZE_NONE 0x0000
#define	PBUFSIZE_1P   0x0100
#define	PBUFSIZE_2P   0x0200
#define	PBUFSIZE_1K   0x0300
#define	PBUFSIZE_2K   0x0400
#define	PBUFSIZE_4K   0x0500
#define	PBUFSIZE_8K   0x0600
#define PBUFSIZE_16K  0x0700
#define PBUFSIZE_32K  0x0800


/* firmware command codes */
enum av7110_osd_command {
	WCreate,
	WDestroy,
	WMoveD,
	WMoveA,
	WHide,
	WTop,
	DBox,
	DLine,
	DText,
	Set_Font,
	SetColor,
	SetBlend,
	SetWBlend,
	SetCBlend,
	SetNonBlend,
	LoadBmp,
	BlitBmp,
	ReleaseBmp,
	SetWTrans,
	SetWNoTrans,
	Set_Palette
};

enum av7110_pid_command {
	MultiPID,
	VideoPID,
	AudioPID,
	InitFilt,
	FiltError,
	NewVersion,
	CacheError,
	AddPIDFilter,
	DelPIDFilter,
	Scan,
	SetDescr,
	SetIR,
	FlushTSQueue
};

enum av7110_mpeg_command {
	SelAudChannels
};

enum av7110_audio_command {
	AudioDAC,
	CabADAC,
	ON22K,
	OFF22K,
	MainSwitch,
	ADSwitch,
	SendDiSEqC,
	SetRegister
};

enum av7110_request_command {
	AudioState,
	AudioBuffState,
	VideoState1,
	VideoState2,
	VideoState3,
	CrashCounter,
	ReqVersion,
	ReqVCXO,
	ReqRegister,
	ReqSecFilterError,
	ReqSTC
};

enum av7110_encoder_command {
	SetVidMode,
	SetTestMode,
	LoadVidCode,
	SetMonitorType,
	SetPanScanType,
	SetFreezeMode
};

enum av7110_rec_play_state {
	__Record,
	__Stop,
	__Play,
	__Pause,
	__Slow,
	__FF_IP,
	__Scan_I,
	__Continue
};

enum av7110_command_type {
	COMTYPE_NOCOM,
	COMTYPE_PIDFILTER,
	COMTYPE_MPEGDECODER,
	COMTYPE_OSD,
	COMTYPE_BMP,
	COMTYPE_ENCODER,
	COMTYPE_AUDIODAC,
	COMTYPE_REQUEST,
	COMTYPE_SYSTEM,
	COMTYPE_REC_PLAY,
	COMTYPE_COMMON_IF,
	COMTYPE_PID_FILTER,
	COMTYPE_PES,
	COMTYPE_TS,
	COMTYPE_VIDEO,
	COMTYPE_AUDIO,
	COMTYPE_CI_LL,
};

#define VID_NONE_PREF		0x00	/* No aspect ration processing preferred */
#define VID_PAN_SCAN_PREF	0x01	/* Pan and Scan Display preferred */
#define VID_VERT_COMP_PREF	0x02	/* Vertical compression display preferred */
#define VID_VC_AND_PS_PREF	0x03	/* PanScan and vertical Compression if allowed */
#define VID_CENTRE_CUT_PREF	0x05	/* PanScan with zero vector */

/* firmware data interface codes */
#define DATA_NONE		 0x00
#define DATA_FSECTION		 0x01
#define DATA_IPMPE		 0x02
#define DATA_MPEG_RECORD	 0x03
#define DATA_DEBUG_MESSAGE	 0x04
#define DATA_COMMON_INTERFACE	 0x05
#define DATA_MPEG_PLAY		 0x06
#define DATA_BMP_LOAD		 0x07
#define DATA_IRCOMMAND		 0x08
#define DATA_PIPING		 0x09
#define DATA_STREAMING		 0x0a
#define DATA_CI_GET		 0x0b
#define DATA_CI_PUT		 0x0c
#define DATA_MPEG_VIDEO_EVENT	 0x0d

#define DATA_PES_RECORD		 0x10
#define DATA_PES_PLAY		 0x11
#define DATA_TS_RECORD		 0x12
#define DATA_TS_PLAY		 0x13

/* ancient CI command codes, only two are actually still used
 * by the link level CI firmware */
#define CI_CMD_ERROR		 0x00
#define CI_CMD_ACK		 0x01
#define CI_CMD_SYSTEM_READY	 0x02
#define CI_CMD_KEYPRESS		 0x03
#define CI_CMD_ON_TUNED		 0x04
#define CI_CMD_ON_SWITCH_PROGRAM 0x05
#define CI_CMD_SECTION_ARRIVED	 0x06
#define CI_CMD_SECTION_TIMEOUT	 0x07
#define CI_CMD_TIME		 0x08
#define CI_CMD_ENTER_MENU	 0x09
#define CI_CMD_FAST_PSI		 0x0a
#define CI_CMD_GET_SLOT_INFO	 0x0b

#define CI_MSG_NONE		 0x00
#define CI_MSG_CI_INFO		 0x01
#define CI_MSG_MENU		 0x02
#define CI_MSG_LIST		 0x03
#define CI_MSG_TEXT		 0x04
#define CI_MSG_REQUEST_INPUT	 0x05
#define CI_MSG_INPUT_COMPLETE	 0x06
#define CI_MSG_LIST_MORE	 0x07
#define CI_MSG_MENU_MORE	 0x08
#define CI_MSG_CLOSE_MMI_IMM	 0x09
#define CI_MSG_SECTION_REQUEST	 0x0a
#define CI_MSG_CLOSE_FILTER	 0x0b
#define CI_PSI_COMPLETE		 0x0c
#define CI_MODULE_READY		 0x0d
#define CI_SWITCH_PRG_REPLY	 0x0e
#define CI_MSG_TEXT_MORE	 0x0f

#define CI_MSG_CA_PMT		 0xe0
#define CI_MSG_ERROR		 0xf0


/* base address of the dual ported RAM which serves as communication
 * area between PCI bus and av7110,
 * as seen by the DEBI bus of the saa7146 */
#define	DPRAM_BASE 0x4000

/* boot protocol area */
#define BOOT_STATE	(DPRAM_BASE + 0x3F8)
#define BOOT_SIZE	(DPRAM_BASE + 0x3FA)
#define BOOT_BASE	(DPRAM_BASE + 0x3FC)
#define BOOT_BLOCK	(DPRAM_BASE + 0x400)
#define BOOT_MAX_SIZE	0xc00

/* firmware command protocol area */
#define IRQ_STATE	(DPRAM_BASE + 0x0F4)
#define IRQ_STATE_EXT	(DPRAM_BASE + 0x0F6)
#define MSGSTATE	(DPRAM_BASE + 0x0F8)
#define FILT_STATE	(DPRAM_BASE + 0x0FA)
#define COMMAND		(DPRAM_BASE + 0x0FC)
#define COM_BUFF	(DPRAM_BASE + 0x100)
#define COM_BUFF_SIZE	0x20

/* various data buffers */
#define BUFF1_BASE	(DPRAM_BASE + 0x120)
#define BUFF1_SIZE	0xE0

#define DATA_BUFF0_BASE	(DPRAM_BASE + 0x200)
#define DATA_BUFF0_SIZE	0x0800

#define DATA_BUFF1_BASE	(DATA_BUFF0_BASE+DATA_BUFF0_SIZE)
#define DATA_BUFF1_SIZE	0x0800

#define DATA_BUFF2_BASE	(DATA_BUFF1_BASE+DATA_BUFF1_SIZE)
#define DATA_BUFF2_SIZE	0x0800

#define DATA_BUFF3_BASE (DATA_BUFF2_BASE+DATA_BUFF2_SIZE)
#define DATA_BUFF3_SIZE 0x0400

#define Reserved	(DPRAM_BASE + 0x1E00)
#define Reserved_SIZE	0x1C0


/* firmware status area */
#define STATUS_BASE	(DPRAM_BASE + 0x1FC0)
#define STATUS_SCR	(STATUS_BASE + 0x00)
#define STATUS_MODES	(STATUS_BASE + 0x04)
#define STATUS_LOOPS	(STATUS_BASE + 0x08)

#define STATUS_MPEG_WIDTH     (STATUS_BASE + 0x0C)
/* ((aspect_ratio & 0xf) << 12) | (height & 0xfff) */
#define STATUS_MPEG_HEIGHT_AR (STATUS_BASE + 0x0E)

/* firmware data protocol area */
#define RX_TYPE		(DPRAM_BASE + 0x1FE8)
#define RX_LEN		(DPRAM_BASE + 0x1FEA)
#define TX_TYPE		(DPRAM_BASE + 0x1FEC)
#define TX_LEN		(DPRAM_BASE + 0x1FEE)

#define RX_BUFF		(DPRAM_BASE + 0x1FF4)
#define TX_BUFF		(DPRAM_BASE + 0x1FF6)

#define HANDSHAKE_REG	(DPRAM_BASE + 0x1FF8)
#define COM_IF_LOCK	(DPRAM_BASE + 0x1FFA)

#define IRQ_RX		(DPRAM_BASE + 0x1FFC)
#define IRQ_TX		(DPRAM_BASE + 0x1FFE)

/* used by boot protocol to load firmware into av7110 DRAM */
#define DRAM_START_CODE		0x2e000404
#define DRAM_MAX_CODE_SIZE	0x00100000

/* saa7146 gpio lines */
#define RESET_LINE		2
#define DEBI_DONE_LINE		1
#define ARM_IRQ_LINE		0



extern void av7110_reset_arm(struct av7110 *av7110);
extern int av7110_bootarm(struct av7110 *av7110);
extern int av7110_firmversion(struct av7110 *av7110);
#define FW_CI_LL_SUPPORT(arm_app) ((arm_app) & 0x80000000)
#define FW_VERSION(arm_app)	  ((arm_app) & 0x0000FFFF)

extern int av7110_fw_cmd(struct av7110 *av7110, int type, int com, int num, ...);
extern int __av7110_send_fw_cmd(struct av7110 *av7110, u16* buf, int length);
extern int av7110_send_fw_cmd(struct av7110 *av7110, u16* buf, int length);
extern int av7110_send_ci_cmd(struct av7110 *av7110, u8 subcom, u8 *buf, u8 len);
extern int av7110_fw_request(struct av7110 *av7110, u16 *request_buf,
			     int request_buf_len, u16 *reply_buf, int reply_buf_len);
extern int av7110_fw_query(struct av7110 *av7110, u16 tag, u16* Buff, s16 length);


/* DEBI (saa7146 data extension bus interface) access */
extern int av7110_debiwrite(struct av7110 *av7110, u32 config,
			    int addr, u32 val, int count);
extern u32 av7110_debiread(struct av7110 *av7110, u32 config,
			   int addr, int count);


/* DEBI during interrupt */
/* single word writes */
static inline void iwdebi(struct av7110 *av7110, u32 config, int addr, u32 val, int count)
{
	av7110_debiwrite(av7110, config, addr, val, count);
}

/* buffer writes */
static inline void mwdebi(struct av7110 *av7110, u32 config, int addr, char *val, int count)
{
	memcpy(av7110->debi_virt, val, count);
	av7110_debiwrite(av7110, config, addr, 0, count);
}

static inline u32 irdebi(struct av7110 *av7110, u32 config, int addr, u32 val, int count)
{
	u32 res;

	res=av7110_debiread(av7110, config, addr, count);
	if (count<=4)
		memcpy(av7110->debi_virt, (char *) &res, count);
	return res;
}

/* DEBI outside interrupts, only for count <= 4! */
static inline void wdebi(struct av7110 *av7110, u32 config, int addr, u32 val, int count)
{
	unsigned long flags;

	spin_lock_irqsave(&av7110->debilock, flags);
	av7110_debiwrite(av7110, config, addr, val, count);
	spin_unlock_irqrestore(&av7110->debilock, flags);
}

static inline u32 rdebi(struct av7110 *av7110, u32 config, int addr, u32 val, int count)
{
	unsigned long flags;
	u32 res;

	spin_lock_irqsave(&av7110->debilock, flags);
	res=av7110_debiread(av7110, config, addr, count);
	spin_unlock_irqrestore(&av7110->debilock, flags);
	return res;
}

/* handle mailbox registers of the dual ported RAM */
static inline void ARM_ResetMailBox(struct av7110 *av7110)
{
	unsigned long flags;

	spin_lock_irqsave(&av7110->debilock, flags);
	av7110_debiread(av7110, DEBINOSWAP, IRQ_RX, 2);
	av7110_debiwrite(av7110, DEBINOSWAP, IRQ_RX, 0, 2);
	spin_unlock_irqrestore(&av7110->debilock, flags);
}

static inline void ARM_ClearMailBox(struct av7110 *av7110)
{
	iwdebi(av7110, DEBINOSWAP, IRQ_RX, 0, 2);
}

static inline void ARM_ClearIrq(struct av7110 *av7110)
{
	irdebi(av7110, DEBINOSWAP, IRQ_RX, 0, 2);
}

/****************************************************************************
 * Firmware commands
 ****************************************************************************/

static inline int SendDAC(struct av7110 *av7110, u8 addr, u8 data)
{
	return av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, AudioDAC, 2, addr, data);
}

static inline void VidMode(struct av7110 *av7110, int mode)
{
	av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetVidMode, 1, mode);
}

static int inline vidcom(struct av7110 *av7110, u32 com, u32 arg)
{
	return av7110_fw_cmd(av7110, 0x80, 0x02, 4,
			     (com>>16), (com&0xffff),
			     (arg>>16), (arg&0xffff));
}

static int inline audcom(struct av7110 *av7110, u32 com)
{
	return av7110_fw_cmd(av7110, 0x80, 0x03, 4,
			     (com>>16), (com&0xffff));
}

static inline void Set22K(struct av7110 *av7110, int state)
{
	av7110_fw_cmd(av7110, COMTYPE_AUDIODAC, (state ? ON22K : OFF22K), 0);
}


extern int av7110_diseqc_send(struct av7110 *av7110, int len, u8 *msg, unsigned long burst);


#ifdef CONFIG_DVB_AV7110_OSD
extern int av7110_osd_cmd(struct av7110 *av7110, osd_cmd_t *dc);
#endif /* CONFIG_DVB_AV7110_OSD */



#endif /* _AV7110_HW_H_ */
