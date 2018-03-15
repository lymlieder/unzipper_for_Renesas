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
uint16 flashFresh[256][4];//��ַ��a/b��aa��bb
uint32 flashFreshCount = 0;
uint16 afi = 0;
uint16 bfi = 0;

int oldPosition = 0;
int newPosition = 0;

  /************************************************************************************************
* Function Name: transArrayToNumber
* Decription   : ��byte��buf������ת������,��λ��ǰ
* Input        : ����buf����Ҫת��λ��dataLength
* Output       : �õ�������
* Others       : None
************************************************************************************************/
uint32 transArrayToNumber(uint8 *buf, uint8 dataLength)//��byte��buf������ת������,��λ��ǰ
{
  uint32 number = 0;
  for(int i = 0; i < dataLength; i++)
    number |= ((buf[i] & 0xff) << (8*i));
  return number;
}

/************************************************************************************************
* Function Name: addDataToNewFile
* Decription   : ��λ��ƫ�Ƶ�ַdataAddress�ĳ���ΪdataLength�ľ��ļ�flash��ճ�����ļ���β
* Input        : ��ת¼���ļ�ƫ�Ƶ�ַdataAddress�����ļ�ת¼����dataLength
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
* Decription   : �滻ˢд���ļ�flash�е�ֵ
* Input        : ��Ҫ�޸ĵ����ļ�ƫ�Ƶ�ַdataAddress��Ҫ�滻��ֵ����element[]���Ƿ񵽽�βfinishTip
* Output       : 0x00��ʾ�ɹ�
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
    return 0x01;//����Ԫ�ظ����Ƿ�������1��
  
  afi++;
  int maxLine;
  int fi = afi;
  
  if(finishTip == FALSE)
    maxLine = 256 - 1;
  else
    maxLine = fi;
  
  if(fi >= maxLine)//���������������ʼ��flash��ת¼;
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
        if(page != (flashFresh[fi][0] + newFileAddress) / 1024)//���ҳ�뷢���仯
          break;
      }
        
      //��ת¼
      Flash_Read(flashBuf, 1024, page*1024);
      //���滻
      for(uint16 i = oldFi; i < fi; i++)
      {
        flashBuf[flashFresh[i][0] + newFileAddress - page*1024] = flashFresh[i][2];//aa
        if(flashFresh[i][1] == 1)
          flashBuf[flashFresh[i][0] + newFileAddress - page*1024 + 1] = flashFresh[i][3];//bb
      }
      //�ٲ���д��
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
* Decription   : ��ѹ������浽���ļ�flash
* Input        : 
* Output       : 0x00��ʾ��ȷ
* Others       : None
************************************************************************************************/
uint8 diffTrans()//4,4,2,4,4,14,1;�ֱ��ǲ�������У��ͷ����ʽ��;���̴���;Ŀ��汾;������У����;������У����;���������ļ�����2�����ļ�����2+10��;������ļ���ַ��������
{
  uint8 readBuf[4];
  uint8 addressLength;
  uint16 diffs[512][2];
  
  Flash_Read(readBuf, 2, diffAddress + 1);//��ȡdiff��һ���ֽڣ��ж��ǲ���0xfa
  if(transArrayToNumber(readBuf, 2) != 0xaaff)
    return 0x01;
  
  Flash_Read(readBuf, 2, diffAddress + 13);//��ȡ���ļ�CRC��������
  newFileCRCCode = (uint16)transArrayToNumber(readBuf, 2);
  
  Flash_Read(readBuf, 2, diffAddress + 17);//��ȡ���ļ�����
  oldFileLength = (uint16)transArrayToNumber(readBuf, 2);
  Flash_Read(readBuf, 2, diffAddress + 19);//��ȡ���ļ�����
  newFileLength = (uint16)transArrayToNumber(readBuf, 2);
  Flash_Read(readBuf, 1, diffAddress + 19);//��ȡ������ļ���ַ��������
  addressLength = readBuf[0];
  
  //��ʼ��ȡ����������ݲ�����ļ��ϲ�
  //�������ļ�����flash//δ��
  //��ʼ��
  for(int i = 0; i < 512; i++)
    for(int j = 0; j < 2; j++)
      diffs[i][j] = 0;//���컯��ά����
  for(int i = 0; i < 256; i++)
    for(int j = 0; j < 3; j++)
      flashFresh[i][j] = 0;//���컯flashˢ������
  
  bool loop = TRUE;
  int add = 33;//������ļ���ַ���������Ŀ�ʼ
  while(loop)//��ʼ��һ��ѭ��
  {
    int ii = 0;
    Flash_Read(readBuf, 1, diffAddress + add);//��ȡ������ļ���ַ��������
    add++;
    if(readBuf[0] == 0xff)
    {
      Flash_Read(readBuf, 1, diffAddress + add);
      add++;
      switch(readBuf[0])
      {
      case 0x01://�滻
        {
          uint32 basePosition, baseLength, resultLentgh;
        
          Flash_Read(readBuf, addressLength, diffAddress + add);//���滻��ַ
          add += addressLength;
          basePosition = transArrayToNumber(readBuf, addressLength);
        
          Flash_Read(readBuf, addressLength, diffAddress + add);//���滻����
          add += addressLength;
          baseLength = transArrayToNumber(readBuf, addressLength);
        
          Flash_Read(readBuf, addressLength, diffAddress + add);//�滻����
          add += addressLength;
          resultLentgh = transArrayToNumber(readBuf, addressLength);
        
          //У��ƫ����
          diff += (resultLentgh - baseLength);
          diffs[ii][0] = basePosition;//��һλ��¼ƫ�Ƶ�ַ
          diffs[ii][1] = diff;//�ڶ�λ��¼ƫ������
          diffsCount++;
        
          addDataToNewFile(lastBasePosition + oldPosition, basePosition - lastBasePosition);//ת¼�����޸ĵĲ���
          oldPosition = basePosition - lastBasePosition;
          oldPosition += baseLength;//�������滻�ĳ��ȣ�old��ȡ����Ϊֹ
          addDataToNewFile(diffAddress + add, resultLentgh);//ת¼���ӵĲ��֣����ݳ���ΪresultLentgh
          add += resultLentgh;
        }
        break;
        
      case 0x02://ɾ��
        {
          uint32 basePosition, baseLength;
          
          Flash_Read(readBuf, addressLength, diffAddress + add);//��ɾ����ַ
          add += addressLength;
          basePosition = transArrayToNumber(readBuf, addressLength);
          
          Flash_Read(readBuf, addressLength, diffAddress + add);//��ɾ������
          add += addressLength;
          baseLength = transArrayToNumber(readBuf, addressLength);
          
          //У��ƫ����
          diff -= baseLength;
          diffs[ii][0] = basePosition;//��һλ��¼ƫ�Ƶ�ַ
          diffs[ii][1] = diff;//�ڶ�λ��¼ƫ������
          diffsCount++;
          
          addDataToNewFile(lastBasePosition + oldPosition, basePosition - lastBasePosition);//ת¼�����޸ĵĲ���
          oldPosition = basePosition - lastBasePosition;
          oldPosition += baseLength;//�������滻�ĳ��ȣ�old��ȡ����Ϊֹ
      }
      break;
      
      case 0x03://����
        {
          uint32 basePosition, resultLentgh;
          
          Flash_Read(readBuf, addressLength, diffAddress + add);//�����ӵ�ַ
          add += addressLength;
          basePosition = transArrayToNumber(readBuf, addressLength);
        
          Flash_Read(readBuf, addressLength, diffAddress + add);//���ӳ���
          add += addressLength;
          resultLentgh = transArrayToNumber(readBuf, addressLength);
          
          //У��ƫ����
          diff += resultLentgh;
          diffs[ii][0] = basePosition;//��һλ��¼ƫ�Ƶ�ַ
          diffs[ii][1] = diff;//�ڶ�λ��¼ƫ������
          diffsCount++;
        
          addDataToNewFile(lastBasePosition + oldPosition, basePosition - lastBasePosition);//ת¼�����޸ĵĲ���
          oldPosition = basePosition - lastBasePosition;//ת¼��ϣ�old��ȡ����Ϊֹ
          addDataToNewFile(diffAddress + add, resultLentgh);//ת¼���ӵĲ��֣����ݳ���ΪresultLentgh
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
      
      Flash_Read(readBuf, 1, diffAddress + add);//ȡ��Ԫ��
      add++;
      mainElement = readBuf[0];
      
      Flash_Read(readBuf, addressLength, diffAddress + add);//ȡ��Ԫ�ض�Ӧ��ַ����
      add += addressLength;
      positionCount = transArrayToNumber(readBuf, addressLength);
      
      for(uint32 i = 0; i < positionCount; i++)//ȡÿ����Ԫ�ز��滻����
      {
        Flash_Read(readBuf, addressLength, diffAddress + add);//ȡ��Ԫ�ض�Ӧ��ַ����
        add += addressLength;
        position = transArrayToNumber(readBuf, addressLength);//ȡ����ַ
        uint32 positionDiff = 0;
        //����ƫ����
        for(uint32 i = 0; i < diffsCount; i++)
        {
          if(diffs[i][0] < position)
            positionDiff += diffs[i][1];//��ַС��position��diffȫ����ӵõ�position����ƫ����
          else if(diffs[i][0] == position)
            return 0x04;//����˵֮ǰ�洢��ƫ�������ڵ�ַ�ǲ������aa��bb�еõ��ĵ�ַ��
        }
        position += positionDiff;
        //��ʼ�滻
        uint8 tempArr[3];
        tempArr[0] = 0;
        tempArr[1] = mainElement;
        tempArr[2] = 0;
        Flash_Read(readBuf, 1, diffAddress + add + 1);//�ж��Ƿ�Ϊ���һ���滻����
        bool ifEnd = FALSE;
        if(readBuf[0] == 0xfa)
          ifEnd = TRUE;
        uint8 result = freshFlashOperation(position, tempArr, ifEnd);//�����������
        if(result != 0x00)
          return (result |= 0xa0);
      }
    }
    else if(readBuf[0] == 0xbb)
    {
      uint8 mainElement1, mainElement2, positionCount, position;
      
      Flash_Read(readBuf, 2, diffAddress + add);//ȡ��Ԫ��
      add += 2;
      mainElement1 = readBuf[0];
      mainElement2 = readBuf[1];
      
      Flash_Read(readBuf, addressLength, diffAddress + add);//ȡ��Ԫ�ض�Ӧ��ַ����
      add += addressLength;
      positionCount = transArrayToNumber(readBuf, addressLength);
      
      for(uint32 i = 0; i < positionCount; i++)//ȡÿ����Ԫ�ز��滻����
      {
        Flash_Read(readBuf, addressLength, diffAddress + add);//ȡ��Ԫ�ض�Ӧ��ַ����
        add += addressLength;
        position = transArrayToNumber(readBuf, addressLength);//ȡ����ַ
        uint32 positionDiff = 0;
        //����ƫ����
        for(uint32 i = 0; i < diffsCount; i++)
        {
          if(diffs[i][0] < position)
            positionDiff += diffs[i][1];//��ַС��position��diffȫ����ӵõ�position����ƫ����
          else if(diffs[i][0] == position)
            return 0x04;//����˵֮ǰ�洢��ƫ�������ڵ�ַ�ǲ������aa��bb�еõ��ĵ�ַ��
        }
        position += positionDiff;
        //��ʼ�滻
        uint8 tempArr[3];
        tempArr[0] = 1;
        tempArr[1] = mainElement1;
        tempArr[2] = mainElement2;
        Flash_Read(readBuf, 1, diffAddress + add + 1);//�ж��Ƿ�Ϊ���һ���滻����
        bool ifEnd = FALSE;
        if(readBuf[0] == 0xfa)
          ifEnd = TRUE;
        uint8 result = freshFlashOperation(position, tempArr, ifEnd);//�����������
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
* Decription   : CRCУ�麯��
* Input        : ��У�����ݵ�BufPtrָ��,��У�����ݳ���Len
* Output       : ���������У��ֵ
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
* Decription   : ����ת��������
* Input        : None
* Output       : �ɹ���Ϊ0x00
* Others       : None
************************************************************************************************/
uint8 dealWhitTheDiffFile()
{
  {
    uint32 newAdd = newFileAddress;//�������ļ�������
    while(newAdd < 0x20000)
    {
      Flash_Erase(newAdd);
      newAdd += 1024;
    }
  }
  
  uint8 result = diffTrans();//�ٷ���
  if(result != 0x00)
    return result;
  
  if(Cal_Crc16((uint8 *)newFileAddress, newFileLength) != newFileCRCCode)//��У��
    return 0x20;
  
  //��ˢ�����ļ�����
  //����
  {
    uint32 oldAdd = oldFileAddress;//�������ļ�������
    while(oldAdd < 0x13800)
    {
      Flash_Erase(oldAdd);
      oldAdd += 1024;
    }
  }
  
  //תд
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