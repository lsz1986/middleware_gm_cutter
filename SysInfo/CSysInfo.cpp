#include "StdAfx.h"

#include "CSysInfo.h"
#include "Iphlpapi.h"


#pragma comment(lib, "IPHLPAPI.LIB")

char  m_buffer[256];
WORD  m_serial[256];
DWORD m_OldInterruptAddress;
DWORDLONG m_IDTR;



// 等待硬盘空闲
static unsigned int WaitHardDiskIdle() 
{ 
	BYTE byTemp;

Waiting:
	_asm
	{
		mov dx, 0x1f7
		in al, dx
		cmp al, 0x80
		jb Endwaiting
		jmp Waiting
	}
Endwaiting:
	_asm
	{
		mov byTemp, al
	}
	return byTemp; 
} 

//中断服务程序
void  _declspec( naked )InterruptProcess(void)
{
	 int   byTemp;
     int   i;
	 WORD temp;
	 //保存寄存器值
     _asm
     {
         push eax
         push ebx
         push ecx
         push edx
         push esi
     }
      
     WaitHardDiskIdle();//等待硬盘空闲状态
     _asm
	 {
	     mov dx, 0x1f6
		 mov al, 0xa0
		 out dx, al
	 }
	 byTemp = WaitHardDiskIdle(); //若直接在Ring3级执行等待命令，会进入死循环
	 if ((byTemp&0x50)!=0x50) 
	 {
		_asm  // 恢复中断现场并退出中断服务程序
		{
            pop esi
            pop edx
			pop ecx
			pop ebx
			pop eax
			iretd
		}
	 }
		
	 _asm
	 {
		 mov dx, 0x1f6 //命令端口1f6,选择驱动器0
		 mov al, 0xa0
	 	 out dx, al
		 inc dx
		 mov al, 0xec
		 out dx, al //发送读驱动器参数命令
	 }	
	 byTemp = WaitHardDiskIdle(); 
	 if ((byTemp&0x58)!=0x58) 
     {
		_asm  // 恢复中断现场并退出中断服务程序
		{
			 pop esi
			 pop edx
			 pop ecx
			 pop ebx
			 pop eax
			 iretd
		}
	 }
     //读取硬盘控制器的全部信息
	 for (i=0;i<256;i++) 
	 {
		 _asm
		 {
			 mov dx, 0x1f0
			 in ax, dx
	 		 mov temp, ax
		 }
		 m_serial[i] = temp; 
	 }                                
 	 _asm
	 {
         pop esi
	     pop edx
         pop ecx
         pop ebx
         pop eax
		 iretd
     }	
}

CSysInfo::CSysInfo()
{
	GetCpuID();
	GetHDSerial();
	GetMacAddr();
}

CSysInfo::~CSysInfo()
{

}

void CSysInfo::GetMacAddr()
{
	DWORD             err;
    DWORD             adapterinfosize=0;
    PIP_ADAPTER_INFO    padapterinfo;
    PIP_ADAPTER_INFO    pAdapter = NULL;
    PIP_ADAPTER_INFO    pNextAdapter = NULL;
	
    if((err=GetAdaptersInfo(NULL,&adapterinfosize))!=0)
    {
        if(err!=ERROR_BUFFER_OVERFLOW)
        {
            return;
        }
    }
    if((padapterinfo=(PIP_ADAPTER_INFO)GlobalAlloc(GPTR,adapterinfosize))==NULL)
    {
        return;
    }
	
    if((err=GetAdaptersInfo(padapterinfo,&adapterinfosize))!=0)
    {
        GlobalFree(padapterinfo);
        return;
    }
	
    pNextAdapter = padapterinfo;
    pAdapter = pNextAdapter;
	
	if (pAdapter)
	{
		BYTE* hostmac = pAdapter->Address;
		
		m_sEthMacAddr.Format("%.2X-%.2X-%.2X-%.2X-%.2X-%.2X",
			hostmac[0],hostmac[1],hostmac[2],hostmac[3],hostmac[4],hostmac[5]);
		
		GlobalFree(padapterinfo);
	}
}

void CSysInfo::GetHDSerial()
{
	m_buffer[0] = '\n';
	char m_HDSerial[256];
	m_HDSerial[0] = '\n';
	
	// Windows NT/2000/XP下读取硬盘序列号
	DWORD m_wWinNTHDSerial[256];
	// 判断是否有SCSI硬盘
	if ( ! WinNTReadIDEHDSerial(m_wWinNTHDSerial)) 
		WinNTReadSCSIHDSerial(m_wWinNTHDSerial);
	
	strcpy (m_buffer, DWORDToChar (m_wWinNTHDSerial, 10, 19));
	strcpy (m_HDSerial, DWORDToChar (m_wWinNTHDSerial, 27, 46));
	m_sHdSerial = m_HDSerial;
	//m_sHdSerial.Format(_T("%s"),m_HDSerial);
}

char* CSysInfo::DWORDToChar(const DWORD diskdata[256], const int firstIndex, const int lastIndex)
{
	static char string [1024];
	int index = 0;
	int position = 0;
	
	// 按照高字节在前，低字节在后的顺序将双字中的低字存入到字符串string中 
	for (index = firstIndex; index <= lastIndex; index++)
	{
		// 存入低字中的高字节
		string [position] = (char) (diskdata [index] / 256);
		position++;
		// 存入低字中的低字节
		string [position] = (char) (diskdata [index] % 256);
		position++;
	}
	//  添加字符串结束标志
	string [position] = '\0';
	
	//  删除字符串中空格
	for (index = position - 1; index > 0 && ' ' == string [index]; index--)
		string [index] = '\0';
	
   return string;
}

// Windows NT/2000/XP下读取IDE硬盘序列号
BOOL CSysInfo::WinNTReadIDEHDSerial(DWORD * buffer)
{
   BYTE IdOutCmd [sizeof (SENDCMDOUTPARAMS) + IDENTIFY_BUFFER_SIZE - 1];
   BOOL bFlag = FALSE;
   int  drive = 0;
   char driveName [256];
   HANDLE hPhysicalDriveIOCTL = 0;    
      
   sprintf (driveName, "\\\\.\\PhysicalDrive%d", drive);
   //  Windows NT/2000/XP下创建文件需要管理员权限
   hPhysicalDriveIOCTL = CreateFile (driveName,
                            GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, 0, NULL);

   if (hPhysicalDriveIOCTL != INVALID_HANDLE_VALUE)
   {
       GETVERSIONOUTPARAMS VersionParams;
       DWORD               cbBytesReturned = 0;

       // 得到驱动器的IO控制器版本
       memset ((void*) &VersionParams, 0, sizeof(VersionParams));
       if(DeviceIoControl (hPhysicalDriveIOCTL, IOCTL_GET_VERSION,
                               NULL, 0, &VersionParams,
                               sizeof(VersionParams),
                               &cbBytesReturned, NULL) )
	   {        
          if (VersionParams.bIDEDeviceMap > 0)
		  {
              BYTE             bIDCmd = 0;   // IDE或者ATAPI识别命令
              SENDCMDINPARAMS  scip;
 
              // 如果驱动器是光驱，采用命令IDE_ATAPI_IDENTIFY， command,
              // 否则采用命令IDE_ATA_IDENTIFY读取驱动器信息
              bIDCmd = (VersionParams.bIDEDeviceMap >> drive & 0x10)?
                      IDE_ATAPI_IDENTIFY : IDE_ATA_IDENTIFY;

              memset (&scip, 0, sizeof(scip));
              memset (IdOutCmd, 0, sizeof(IdOutCmd));
              // 获取驱动器信息
              if (WinNTGetIDEHDInfo (hPhysicalDriveIOCTL, 
                                      &scip, 
                                      (PSENDCMDOUTPARAMS)&IdOutCmd, 
                                      (BYTE) bIDCmd,
                                      (BYTE) drive,
                                      &cbBytesReturned))
			  {
                  int m = 0;
                  USHORT *pIdSector = (USHORT *)
                             ((PSENDCMDOUTPARAMS) IdOutCmd) -> bBuffer;

                  for (m = 0; m < 256; m++)
                       buffer[m] = pIdSector [m];
                  bFlag = TRUE;  // 读取硬盘信息成功
			  }
		  }
	   }
       CloseHandle (hPhysicalDriveIOCTL);  // 关闭句柄
   }
   return bFlag;
}

// WindowsNT/2000/XP系统下读取SCSI硬盘序列号
BOOL CSysInfo::WinNTReadSCSIHDSerial (DWORD * buffer)
{	
      buffer[0]='\n';
      int controller = 0;
      HANDLE hScsiDriveIOCTL = 0;
      char   driveName [256];
      sprintf (driveName, "\\\\.\\Scsi%d:", controller);
     //  Windows NT/2000/XP下任何权限都可以进行
      hScsiDriveIOCTL = CreateFile (driveName,
                               GENERIC_READ | GENERIC_WRITE, 
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, 0, NULL);
 
      if (hScsiDriveIOCTL != INVALID_HANDLE_VALUE)
      {
         int drive = 0;
         DWORD dummy;
         for (drive = 0; drive < 2; drive++)
         {
            char buffer [sizeof (SRB_IO_CONTROL) + SENDIDLENGTH];
            SRB_IO_CONTROL *p = (SRB_IO_CONTROL *) buffer;
            SENDCMDINPARAMS *pin =
                   (SENDCMDINPARAMS *) (buffer + sizeof (SRB_IO_CONTROL));           
            // 准备参数
            memset (buffer, 0, sizeof (buffer));
            p -> HeaderLength = sizeof (SRB_IO_CONTROL);
            p -> Timeout = 10000;
            p -> Length = SENDIDLENGTH;
            p -> ControlCode = IOCTL_SCSI_MINIPORT_IDENTIFY;
            strncpy ((char *) p -> Signature, "SCSIDISK", 8);  
            pin -> irDriveRegs.bCommandReg = IDE_ATA_IDENTIFY;
            pin -> bDriveNumber = drive;
            // 得到SCSI硬盘信息
            if (DeviceIoControl (hScsiDriveIOCTL, IOCTL_SCSI_MINIPORT, 
                                 buffer,
                                 sizeof (SRB_IO_CONTROL) +
                                         sizeof (SENDCMDINPARAMS) - 1,
                                 buffer,
                                 sizeof (SRB_IO_CONTROL) + SENDIDLENGTH,
                                 &dummy, NULL))
            {
               SENDCMDOUTPARAMS *pOut =
                    (SENDCMDOUTPARAMS *) (buffer + sizeof (SRB_IO_CONTROL));
               IDSECTOR *pId = (IDSECTOR *) (pOut -> bBuffer);
               if (pId -> sModelNumber [0])
               {
                  int n = 0;
                  USHORT *pIdSector = (USHORT *) pId;
          
                  for (n = 0; n < 256; n++)
                      buffer[n] = (unsigned char)pIdSector [n]; //add (unsigned char) by YHZ
                  return TRUE;  // 读取成功                               
			   }
            }
		 }
         CloseHandle (hScsiDriveIOCTL);  // 关闭句柄
	  }
   return FALSE;   // 读取失败
}

// Windows NT/2000/XP下读取IDE设备信息
BOOL CSysInfo::WinNTGetIDEHDInfo (HANDLE hPhysicalDriveIOCTL, PSENDCMDINPARAMS pSCIP,
                 PSENDCMDOUTPARAMS pSCOP, BYTE bIDCmd, BYTE bDriveNum,
                 PDWORD lpcbBytesReturned)
{
   // 为读取设备信息准备参数
   pSCIP -> cBufferSize = IDENTIFY_BUFFER_SIZE;
   pSCIP -> irDriveRegs.bFeaturesReg = 0;
   pSCIP -> irDriveRegs.bSectorCountReg = 1;
   pSCIP -> irDriveRegs.bSectorNumberReg = 1;
   pSCIP -> irDriveRegs.bCylLowReg = 0;
   pSCIP -> irDriveRegs.bCylHighReg = 0;

   // 计算驱动器位置
   pSCIP -> irDriveRegs.bDriveHeadReg = 0xA0 | ((bDriveNum & 1) << 4);

   // 设置读取命令
   pSCIP -> irDriveRegs.bCommandReg = bIDCmd;
   pSCIP -> bDriveNumber = bDriveNum;
   pSCIP -> cBufferSize = IDENTIFY_BUFFER_SIZE;
   
   // 读取驱动器信息
   return ( DeviceIoControl (hPhysicalDriveIOCTL, IOCTL_GET_DRIVE_INFO,
               (LPVOID) pSCIP,
               sizeof(SENDCMDINPARAMS) - 1,
               (LPVOID) pSCOP,
               sizeof(SENDCMDOUTPARAMS) + IDENTIFY_BUFFER_SIZE - 1,
               lpcbBytesReturned, NULL) );
}

void CSysInfo::GetCpuID()
{
	CString CPUID;
	unsigned long s1,s2;
	unsigned char vendor_id[]="------------";
	char sel;
	sel='1';
	CString VernderID;
	CString MyCpuID,CPUID1,CPUID2;
	switch(sel)
	{
	case '1':
		__asm{
			xor eax,eax      //eax=0:取Vendor信息
				cpuid    //取cpu id指令，可在Ring3级使用
				mov dword ptr vendor_id,ebx
				mov dword ptr vendor_id[+4],edx
				mov dword ptr vendor_id[+8],ecx
		}
		VernderID.Format("%s-",vendor_id);
		__asm{
			mov eax,01h   //eax=1:取CPU序列号
				xor edx,edx
				cpuid
				mov s1,edx
				mov s2,eax
		}
		CPUID1.Format("%08X%08X",s1,s2);
		__asm{
			mov eax,03h
				xor ecx,ecx
				xor edx,edx
				cpuid
				mov s1,edx
				mov s2,ecx
		}
		CPUID2.Format("%08X%08X",s1,s2);
		break;
		
	case '2':
		{
			__asm{
				mov ecx,119h
					rdmsr
					or eax,00200000h
					wrmsr
			}
		}
		CPUID1.Format("YHZ");
		CPUID2.Format("YHZ");
		break;
	}
	
	m_sCPUID = CPUID1+CPUID2;
	return;
}

