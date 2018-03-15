#include "Diff_Update.h"

#define oldFileAddress 0x4000
#define diffAddress 0x010800
#define newFileAddress 0x013800

uint16 oldFileLength, diffFileLength, newFileLength;
//uint8 oldFileBuf[512], diffFIleBuf[512];
uint16 newFileCRCCode;
uint32 diffsCount = 0;
uint16 diff = 0;
uint32 lastBasePosition = 0;
uint16 flashFresh[256][4];//地址，a/b，aa，bb
uint32 flashFreshCount = 0;
uint16 afi = 0;
uint16 bfi = 0;

int oldPosition = 0;
int newPosition = 0;

  /************************************************************************************************
* Function Name: transArrayToNumber
* Decription   : 把byte（buf）数组转成数字,低位在前
* Input        : 数组buf，需要转换位数dataLength
* Output       : 得到的数字
* Others       : None
************************************************************************************************/
uint32 transArrayToNumber(uint8 *buf, uint8 dataLength)//把byte（buf）数组转成数字,低位在前
{
  uint32 number = 0;
  for(int i = 0; i < dataLength; i++)
    number |= ((buf[i] & 0xff) << (8*i));
  return number;
}

/************************************************************************************************
* Function Name: addDataToNewFile
* Decription   : 把位于偏移地址dataAddress的长度为dataLength的旧文件flash段粘到新文件结尾
* Input        : 被转录旧文件偏移地址dataAddress，旧文件转录长度dataLength
* Output       : None
* Others       : None
************************************************************************************************/
void addDataToNewFile(uint32 dataAddress, uint32 dataLength)
{
  uint8 tempBuf[512];
  uint32 tempAdd = 0;
  
  int count = dataLength / 512;
  int last = dataLength % 512;
  while(count > 0)
  {
    Flash_Read(tempBuf, 512, dataAddress + tempAdd);
    Flash_Write(tempBuf, 512, newFileAddress + tempAdd + newPosition);
    tempAdd += 512;
    count--;
  }
  Flash_Read(tempBuf, last, dataAddress + tempAdd);
  Flash_Write(tempBuf, last, newFileAddress + tempAdd + newPosition);
  newPosition += dataLength;
}

/************************************************************************************************
* Function Name: freshFlashOperation
* Decription   : 替换刷写新文件flash中的值
* Input        : 需要修改的新文件偏移地址dataAddress，要替换的值数组element[]，是否到结尾finishTip
* Output       : 0x00表示成功
* Others       : None
************************************************************************************************/
uint8 freshFlashOperation(uint32 dataAddress, uint8 element[], bool finishTip)
{
  uint8 flashBuf[1024];
  
  flashFresh[afi][0] = dataAddress;
  flashFresh[afi][1] = element[0];
  flashFresh[afi][2] = element[1];
  flashFresh[afi][3] = element[2];
  if(sizeof(element) > 3)
    return 0x01;//传入元素个数非法（大于1）
  
  afi++;
  int maxLine;
  int fi = afi;
  
  if(finishTip == FALSE)
    maxLine = 256 - 1;
  else
    maxLine = fi;
  
  if(fi >= maxLine)//缓存数组存满，开始向flash中转录;
  {
    uint16 oldFi = 0;
    flashFreshCount += fi;
    fi = 0;
    afi = 0;
    while(fi < maxLine)
    {
      uint16 page = (flashFresh[oldFi][0] + newFileAddress) / 1024;
      while(fi++ < maxLine)
      {
        if(page != (flashFresh[fi][0] + newFileAddress) / 1024)//如果页码发生变化
          break;
      }
        
      //先转录
      Flash_Read(flashBuf, 1024, page*1024);
      //再替换
      for(uint16 i = oldFi; i < fi; i++)
      {
        flashBuf[flashFresh[i][0] + newFileAddress - page*1024] = flashFresh[i][2];//aa
        if(flashFresh[i][1] == 1)
          flashBuf[flashFresh[i][0] + newFileAddress - page*1024 + 1] = flashFresh[i][3];//bb
      }
      //再擦除写入
      Flash_Erase(flashFresh[oldFi][0]+ newFileAddress);
      Flash_Write(flashBuf, 1024, page*1024);
      oldFi = fi;
    }
    if(fi > maxLine)
      return 0x02;
  }
  return 0x00;
}

/************************************************************************************************
* Function Name: diffTrans
* Decription   : 解压差异包存到新文件flash
* Input        : 
* Output       : 0x00表示正确
* Others       : None
************************************************************************************************/
uint8 diffTrans()//4,4,2,4,4,14,1;分别是差异升级校验头（格式）;厂商代码;目标版本;升级包校验码;升级后校验码;保留（旧文件长度2，新文件长度2+10）;差异包文件地址描述长度
{
  uint8 readBuf[4];
  uint8 addressLength;
  uint16 diffs[512][2];
  
  Flash_Read(readBuf, 2, diffAddress + 1);//读取diff第一个字节，判断是不是0xfa
  if(transArrayToNumber(readBuf, 2) != 0xaaff)
    return 0x01;
  
  Flash_Read(readBuf, 2, diffAddress + 13);//读取新文件CRC（升级后）
  newFileCRCCode = (uint16)transArrayToNumber(readBuf, 2);
  
  Flash_Read(readBuf, 2, diffAddress + 17);//读取旧文件长度
  oldFileLength = (uint16)transArrayToNumber(readBuf, 2);
  Flash_Read(readBuf, 2, diffAddress + 19);//读取新文件长度
  newFileLength = (uint16)transArrayToNumber(readBuf, 2);
  Flash_Read(readBuf, 1, diffAddress + 19);//读取差异包文件地址描述长度
  addressLength = readBuf[0];
  
  //开始读取差异包中内容并与旧文件合并
  //擦除新文件缓存flash//未做
  //初始化
  for(int i = 0; i < 512; i++)
    for(int j = 0; j < 2; j++)
      diffs[i][j] = 0;//差异化二维数组
  for(int i = 0; i < 256; i++)
    for(int j = 0; j < 3; j++)
      flashFresh[i][j] = 0;//差异化flash刷新数组
  
  bool loop = TRUE;
  int add = 33;//差异包文件地址描述后正文开始
  while(loop)//开始第一轮循环
  {
    int ii = 0;
    Flash_Read(readBuf, 1, diffAddress + add);//读取差异包文件地址描述长度
    add++;
    if(readBuf[0] == 0xff)
    {
      Flash_Read(readBuf, 1, diffAddress + add);
      add++;
      switch(readBuf[0])
      {
      case 0x01://替换
        {
          uint32 basePosition, baseLength, resultLentgh;
        
          Flash_Read(readBuf, addressLength, diffAddress + add);//被替换地址
          add += addressLength;
          basePosition = transArrayToNumber(readBuf, addressLength);
        
          Flash_Read(readBuf, addressLength, diffAddress + add);//被替换长度
          add += addressLength;
          baseLength = transArrayToNumber(readBuf, addressLength);
        
          Flash_Read(readBuf, addressLength, diffAddress + add);//替换长度
          add += addressLength;
          resultLentgh = transArrayToNumber(readBuf, addressLength);
        
          //校正偏移量
          diff += (resultLentgh - baseLength);
          diffs[ii][0] = basePosition;//第一位记录偏移地址
          diffs[ii][1] = diff;//第二位记录偏移数量
          diffsCount++;
        
          addDataToNewFile(lastBasePosition + oldPosition, basePosition - lastBasePosition);//转录不用修改的部分
          oldPosition = basePosition - lastBasePosition;
          oldPosition += baseLength;//跳过被替换的长度，old读取到此为止
          addDataToNewFile(diffAddress + add, resultLentgh);//转录增加的部分，数据长度为resultLentgh
          add += resultLentgh;
        }
        break;
        
      case 0x02://删除
        {
          uint32 basePosition, baseLength;
          
          Flash_Read(readBuf, addressLength, diffAddress + add);//被删除地址
          add += addressLength;
          basePosition = transArrayToNumber(readBuf, addressLength);
          
          Flash_Read(readBuf, addressLength, diffAddress + add);//被删除长度
          add += addressLength;
          baseLength = transArrayToNumber(readBuf, addressLength);
          
          //校正偏移量
          diff -= baseLength;
          diffs[ii][0] = basePosition;//第一位记录偏移地址
          diffs[ii][1] = diff;//第二位记录偏移数量
          diffsCount++;
          
          addDataToNewFile(lastBasePosition + oldPosition, basePosition - lastBasePosition);//转录不用修改的部分
          oldPosition = basePosition - lastBasePosition;
          oldPosition += baseLength;//跳过被替换的长度，old读取到此为止
      }
      break;
      
      case 0x03://增加
        {
          uint32 basePosition, resultLentgh;
          
          Flash_Read(readBuf, addressLength, diffAddress + add);//被增加地址
          add += addressLength;
          basePosition = transArrayToNumber(readBuf, addressLength);
        
          Flash_Read(readBuf, addressLength, diffAddress + add);//增加长度
          add += addressLength;
          resultLentgh = transArrayToNumber(readBuf, addressLength);
          
          //校正偏移量
          diff += resultLentgh;
          diffs[ii][0] = basePosition;//第一位记录偏移地址
          diffs[ii][1] = diff;//第二位记录偏移数量
          diffsCount++;
        
          addDataToNewFile(lastBasePosition + oldPosition, basePosition - lastBasePosition);//转录不用修改的部分
          oldPosition = basePosition - lastBasePosition;//转录完毕，old读取到此为止
          addDataToNewFile(diffAddress + add, resultLentgh);//转录增加的部分，数据长度为resultLentgh
          add += resultLentgh;
        }
        break;
        
      default:
        return 0x03;
      }
    }
    else if(readBuf[0] == 0xaa)
    {
      uint8 mainElement, positionCount, position;
      
      Flash_Read(readBuf, 1, diffAddress + add);//取主元素
      add++;
      mainElement = readBuf[0];
      
      Flash_Read(readBuf, addressLength, diffAddress + add);//取主元素对应地址个数
      add += addressLength;
      positionCount = transArrayToNumber(readBuf, addressLength);
      
      for(uint32 i = 0; i < positionCount; i++)//取每个主元素并替换处理
      {
        Flash_Read(readBuf, addressLength, diffAddress + add);//取主元素对应地址个数
        add += addressLength;
        position = transArrayToNumber(readBuf, addressLength);//取出地址
        uint32 positionDiff = 0;
        //处理偏移量
        for(uint32 i = 0; i < diffsCount; i++)
        {
          if(diffs[i][0] < position)
            positionDiff += diffs[i][1];//地址小于position的diff全部相加得到position的总偏移量
          else if(diffs[i][0] == position)
            return 0x04;//按理说之前存储的偏移量所在地址是不会等于aa或bb中得到的地址的
        }
        position += positionDiff;
        //开始替换
        uint8 tempArr[3];
        tempArr[0] = 0;
        tempArr[1] = mainElement;
        tempArr[2] = 0;
        Flash_Read(readBuf, 1, diffAddress + add + 1);//判断是否为最后一条替换命令
        bool ifEnd = FALSE;
        if(readBuf[0] == 0xfa)
          ifEnd = TRUE;
        uint8 result = freshFlashOperation(position, tempArr, ifEnd);//存与更改数据
        if(result != 0x00)
          return (result |= 0xa0);
      }
    }
    else if(readBuf[0] == 0xbb)
    {
      uint8 mainElement1, mainElement2, positionCount, position;
      
      Flash_Read(readBuf, 2, diffAddress + add);//取主元素
      add += 2;
      mainElement1 = readBuf[0];
      mainElement2 = readBuf[1];
      
      Flash_Read(readBuf, addressLength, diffAddress + add);//取主元素对应地址个数
      add += addressLength;
      positionCount = transArrayToNumber(readBuf, addressLength);
      
      for(uint32 i = 0; i < positionCount; i++)//取每个主元素并替换处理
      {
        Flash_Read(readBuf, addressLength, diffAddress + add);//取主元素对应地址个数
        add += addressLength;
        position = transArrayToNumber(readBuf, addressLength);//取出地址
        uint32 positionDiff = 0;
        //处理偏移量
        for(uint32 i = 0; i < diffsCount; i++)
        {
          if(diffs[i][0] < position)
            positionDiff += diffs[i][1];//地址小于position的diff全部相加得到position的总偏移量
          else if(diffs[i][0] == position)
            return 0x04;//按理说之前存储的偏移量所在地址是不会等于aa或bb中得到的地址的
        }
        position += positionDiff;
        //开始替换
        uint8 tempArr[3];
        tempArr[0] = 1;
        tempArr[1] = mainElement1;
        tempArr[2] = mainElement2;
        Flash_Read(readBuf, 1, diffAddress + add + 1);//判断是否为最后一条替换命令
        bool ifEnd = FALSE;
        if(readBuf[0] == 0xfa)
          ifEnd = TRUE;
        uint8 result = freshFlashOperation(position, tempArr, ifEnd);//存与更改数据
        if(result != 0x00)
          return (result |= 0xb0);
      }
    }
    else if(readBuf[0] == 0xfa)
    {
      loop = FALSE;
    }
    else
      return 0x02;
  }
  return 0x00;
}

/************************************************************************************************
* Function Name: Cal_Crc16
* Decription   : CRC校验函数
* Input        : 被校验数据的BufPtr指针,被校验数据长度Len
* Output       : 计算出来的校验值
* Others       : None
************************************************************************************************/
uint16 Cal_Crc16(uint8 *BufPtr, uint16 Len)
{
    uint16 crc16 = 0xFFFF;
    uint8 i;

    Clear_Wdt();
    while (Len--)
    {
        crc16 ^= *BufPtr++;
        for (i = 0; i < 8; i++)
        {
            if (crc16 & 0x0001)
            {
                crc16 >>= 1;
                crc16 ^= CRC_SEED;
            }
            else
            {
                crc16 >>= 1;
            }
        }
    }
    crc16 ^= 0xFFFF;

    return crc16;
}

/************************************************************************************************
* Function Name: dealWhitTheDiffFile
* Decription   : 差异转换主函数
* Input        : None
* Output       : 成功则为0x00
* Others       : None
************************************************************************************************/
uint8 dealWhitTheDiffFile()
{
  {
    uint32 newAdd = newFileAddress;//擦除新文件缓存区
    while(newAdd < 0x20000)
    {
      Flash_Erase(newAdd);
      newAdd += 1024;
    }
  }
  
  uint8 result = diffTrans();//再翻译
  if(result != 0x00)
    return result;
  
  if(Cal_Crc16((uint8 *)newFileAddress, newFileLength) != newFileCRCCode)//再校验
    return 0x20;
  
  //再刷到旧文件区域
  //擦除
  {
    uint32 oldAdd = oldFileAddress;//擦除旧文件缓存区
    while(oldAdd < 0x13800)
    {
      Flash_Erase(oldAdd);
      oldAdd += 1024;
    }
  }
  
  //转写
  uint8 tempBuf[512];
  uint32 tempAdd = 0;
   
  int count = newFileLength / 512;
  int last = newFileLength % 512;
  while(count > 0)
  {
    Flash_Read(tempBuf, 512, newFileAddress + tempAdd);
    Flash_Write(tempBuf, 512, oldFileAddress + tempAdd);
    tempAdd += 512;
    count--;
  }
  Flash_Read(tempBuf, last, newFileAddress + tempAdd);
  Flash_Write(tempBuf, last, oldFileAddress + tempAdd);
  
  return 0x00;
}

/**************************************End of file**********************************************/