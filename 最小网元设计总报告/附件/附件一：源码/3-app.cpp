// apptester.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include <winsock2.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <Windows.h>
#include <iostream>
#include <conio.h>
#include "winsock.h"
#include "stdio.h"
#include "CfgFileParms.h"
#pragma comment (lib,"wsock32.lib")

#define MAX_BUFFER_SIZE 5000 //缓冲的最大大小

//定义全局变量
int my_work_mode = 0;// 1:发方 or 0:收方
int data_form = 0;//0（11）:自动发/收数据（NO_MAX 组）；1（10）：自动发/收字符串（hello）；2（1）：手动发/收字符串；3（0）：发/收图片；
char auto_ch[16] = { 'h','e','l','l','o' };//form1
char hand_ch[64];//form2
char* image_ch = NULL;//form3

bool Initial_Flag = true;

int Source_Device_number = 0;
int Des_Device_number = 0;
int Max_Device = 6;

unsigned int image_size = 100000;

char* words = NULL;//接收字符串
int words_index = 0;//接收字符串索引

unsigned int NO = 0;//循环次数
unsigned int NO_MAX = 0;//最大循环次数

//差错控制
bool if_ack(char* s);

using namespace std;
//基于select的定时器结构，目的是把数据的收发和定时都统一到一个事件驱动框架下
//可以有多个定时器，本设计实现了一个基准定时器，为周期性10ms定时，也可以当作是一种心跳计时器
//其余的定时器可以在这个基础上完成，可行的方案存在多种
//看懂设计思路后，自行扩展以满足需要
//基准定时器一开启就会立即触发一次

//

struct threadTimer_t {
	int iType; //为0表示周期性定时器，定时达到后，会自动启动下一次定时
	ULONG ulInterval;
	LARGE_INTEGER llStopTime;
}sBasicTimer;

void code(unsigned long x, char A[], int length)
{
	unsigned long test;
	int i;
	//高位在前
	test = 1;
	test = test << (length - 1);
	for (i = 0; i < length; i++) {
		if (test & x) {
			A[i] = '1';
		}
		else {
			A[i] = '0';
		}
		A[i] = '\0';
		test = test >> 1; //本算法利用了移位操作和"与"计算，逐位测出x的每一位是0还是1.
	}
}
unsigned long decode(char A[], int length)
{
	unsigned long x;
	int i;

	x = 0;
	for (i = 0; i < length; i++) {
		if (A[i] == '0') {
			x = x << 1;;
		}
		else {
			x = x << 1;
			x = x | 1;
		}
	}
	return x;
}
//返回值是转出来有多少位
int ByteArrayToBitArray(char* bitA, int iBitLen, char* byteA, int iByteLen)
{
	int i;
	int len;

	len = min(iByteLen, iBitLen / 8);
	for (i = 0; i < len; i++) {
		//每次编码8位
		code(byteA[i], &(bitA[i * 8]), 8);
	}
	return len * 8;
}
//返回值是转出来有多少个字节，如果位流长度不是8位整数倍，则最后1字节不满
int BitArrayToByteArray(char* bitA, int iBitLen, char* byteA, int iByteLen)
{
	int i;
	int len;
	int retLen;

	len = min(iByteLen * 8, iBitLen);
	if (iBitLen > iByteLen * 8) {
		//截断转换
		retLen = iByteLen;
	}
	else {
		if (iBitLen % 8 != 0)
			retLen = iBitLen / 8 + 1;
		else
			retLen = iBitLen / 8;
	}

	for (i = 0; i < len; i += 8) {
		byteA[i / 8] = (char)decode(bitA + i, 8);
	}
	return retLen;
}
void initTimer()
{
	sBasicTimer.iType = 0;
	sBasicTimer.ulInterval = 10 * 1000;//10ms,单位是微秒，10ms相对误差较小
	QueryPerformanceCounter(&sBasicTimer.llStopTime);
}
//根据系统当前时间设置select函数要用的超时时间——to，每次在select前使用
void setSelectTimeOut(timeval* to, struct threadTimer_t* sT)
{
	LARGE_INTEGER llCurrentTime;
	LARGE_INTEGER llFreq;
	LONGLONG next;
	//取系统当前时间
	QueryPerformanceFrequency(&llFreq);
	QueryPerformanceCounter(&llCurrentTime);
	if (llCurrentTime.QuadPart >= sT->llStopTime.QuadPart) {
		to->tv_sec = 0;
		to->tv_usec = 0;
		//		sT->llStopTime.QuadPart += llFreq.QuadPart * sT->ulInterval / 1000000;
	}
	else {
		next = sT->llStopTime.QuadPart - llCurrentTime.QuadPart;
		next = next * 1000000 / llFreq.QuadPart;
		to->tv_sec = (long)(next / 1000000);
		to->tv_usec = long(next % 1000000);
	}

}
//根据系统当前时间判断定时器sT是否超时，可每次在select后使用，返回值true表示超时，false表示没有超时
bool isTimeOut(struct threadTimer_t* sT)
{
	LARGE_INTEGER llCurrentTime;
	LARGE_INTEGER llFreq;
	//取系统当前时间
	QueryPerformanceFrequency(&llFreq);
	QueryPerformanceCounter(&llCurrentTime);

	if (llCurrentTime.QuadPart >= sT->llStopTime.QuadPart) {
		if (sT->iType == 0) {
			//定时器是周期性的，重置定时器
			sT->llStopTime.QuadPart += llFreq.QuadPart * sT->ulInterval / 1000000;
		}
		return true;
	}
	else {
		return false;
	}
}
void print_data(char* buf, int length, int mode)
{
	int i;
	int linecount = 0;
	if (mode == 0) {
		length = BitArrayToByteArray(buf, length, buf, length);
	}
	for (i = 0; i < length; i++) {
		linecount++;
		printf("%c ", buf[i]);
		if (linecount >= 40) {
			printf("\n");
			linecount = 0;
		}
	}
	printf("\n");
	linecount = 0;
	for (i = 0; i < length; i++) {
		linecount++;
		printf("%02x ", (unsigned char)buf[i]);
		if (linecount >= 40) {
			printf("\n");
			linecount = 0;
		}
	}
	printf("\n");
}

//my_functions
char* get_eight_bites();
void print_array(int* a);
char* ascll_code(char ch);
char ascll_decode(char* p);

//转换
char* ten2two(int s);
int two2ten(char* p);

//image
const char* base64char = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
char* base64_encode(const unsigned char* bindata, char* base64, int binlength);
int base64_decode(const char* base64, unsigned char* bindata);
char* code_image(char* file_path);
unsigned int get_imagesize(char* file_path);
void decode_image(char* imageBase64, unsigned int imageSize);
int main(int argc, char* argv[])
{
	printf("***********************************3号网元 应用层************************************\n\n\n");
	printf("---------------------------------------相关参数--------------------------------------\n");
	srand((unsigned)time(NULL));
	SOCKET sock;
	struct sockaddr_in ser_addr, remote_addr;
	int len;
	char* sendByteBuf = NULL; //字节数组测试数据
	sendByteBuf = (char*)malloc(8 * sizeof(char));
	char* sendBitBuf = NULL;//位数组试数据
	sendBitBuf = (char*)malloc(8 * sizeof(char));
	char* recvBuf; //用来接收数据

	WSAData wsa;
	int retval;
	fd_set readfds;//非阻塞
	timeval timeout;
	int i;
	unsigned long arg;
	int linecount = 0;
	int port;
	string s1, s2, s3; //设备号，层次号（不传），实体号
	int count = 0;
	int iWorkMode = 0;
	int iSndTotal = 0;
	int iSndTotalCount = 0;
	int iSndErrorCount = 0;
	int iRcvTotal = 0;
	int iRcvTotalCount = 0;
	int spin = 0;
	int autoSendTime = 10;
	int autoSendSize = 100;
	int lowerMode = 1;

	//带外命令接口
	SOCKET iCmdSock = 0;

	recvBuf = (char*)malloc(MAX_BUFFER_SIZE);
	if (sendBitBuf == NULL || sendByteBuf == NULL || recvBuf == NULL) {
		if (sendBitBuf != NULL) {
			free(sendBitBuf);
		}
		if (sendByteBuf != NULL) {
			free(sendByteBuf);
		}
		if (recvBuf != NULL) {
			free(recvBuf);
		}
		cout << "内存不够" << endl;
		return 0;
	}

	CCfgFileParms cfgParms;

	if (argc == 4) {
		s1 = argv[1];
		s2 = argv[2];
		s3 = argv[3];
	}
	else if (argc == 3) {
		s1 = argv[1];
		s2 = "APP";
		s3 = argv[2];
	}
	else {
		//从命令行读取
		/*cout << "请输入设备号：";
		cin >> s1;*/
		s1 = '3';
		//cout << "请输入层次名（大写）：";
		//cin >> s2;
		s2 = "APP";
		/*cout << "请输入实体号：";
		cin >> s3;*/
		s3 = '0';
	}
	Source_Device_number = s1[0] - 48;

	WSAStartup(0x101, &wsa);
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == SOCKET_ERROR)
		return 0;


	cfgParms.setDeviceID(s1);
	cfgParms.setLayer(s2);
	cfgParms.setEntityID(s3);
	cfgParms.read();
	cfgParms.print();
	/////my_change
	cfgParms.isConfigExist = true;
	/////
	if (!cfgParms.isConfigExist) {
		//从键盘输入，需要连接的API端口号
		printf("Please input this Layer port: ");
		scanf_s("%d", &port);

		ser_addr.sin_family = AF_INET;
		ser_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
		ser_addr.sin_port = htons(port);
		if (bind(sock, (sockaddr*)& ser_addr, sizeof(ser_addr)) == SOCKET_ERROR) {
			return 0;
		}

		remote_addr.sin_family = AF_INET;
		remote_addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK); //假设物理层模拟软件在本地

		//从键盘输入，需要连接的物理层模拟软件的端口号
		printf("Please input Lower Layer port: ");
		scanf_s("%d", &port);
		remote_addr.sin_port = htons(port);

		//从键盘输入，下层接口类型，除了物理层，默认都是1，物理层也要与模拟软件的upperMode一致
		printf("Please input Lower Layer mode: ");
		scanf_s("%d", &lowerMode);

		//从键盘输入，工作方式
		printf("Please input Working Mode: ");
		scanf_s("%d", &iWorkMode);
		if (iWorkMode / 10 == 1) {
			//自动发送
			//从键盘输入，发送间隔和发送大小
			printf("Please input send time interval（ms）: ");
			scanf_s("%d", &autoSendTime);
			printf("Please input send size（bit）: ");
			scanf_s("%d", &autoSendSize);
		}
	}
	else {
		retval = cfgParms.getValueInt(&port, CCfgFileParms::BASIC, (char*)"myPort", 0);
		if (retval == -1) {
			//默认参数
			return 0;
		}
		ser_addr.sin_family = AF_INET;
		ser_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
		ser_addr.sin_port = htons(port);
		if (bind(sock, (sockaddr*)& ser_addr, sizeof(ser_addr)) == SOCKET_ERROR) {
			//绑定错误，退出
			return 0;
		}
		remote_addr.sin_family = AF_INET;
		remote_addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK); //假设物理层模拟软件在本地
		retval = cfgParms.getValueInt(&port, CCfgFileParms::LOWER, (char*)"lowerPort", 0);
		if (retval == -1) {
			return 0;
		}
		remote_addr.sin_port = htons(port);

		retval = cfgParms.getValueInt(&lowerMode, CCfgFileParms::LOWER, (char*)"lowerMode", 0);
		if (retval == -1) {
			lowerMode = 1;
		}

		retval = cfgParms.getValueInt(&iWorkMode, CCfgFileParms::BASIC, (char*)"workMode", 0);
		if (retval == -1) {
			iWorkMode = 0;
		}
		if (iWorkMode / 10 == 1) {
			retval = cfgParms.getValueInt(&autoSendTime, CCfgFileParms::BASIC, (char*)"autoSendTime", 0);
			if (retval == -1) {
				autoSendTime = 10;
			}
			retval = cfgParms.getValueInt(&autoSendSize, CCfgFileParms::BASIC, (char*)"autoSendSize", 0);
			if (retval == -1) {
				autoSendSize = 800;
			}
		}
		retval = cfgParms.getValueInt(&port, CCfgFileParms::BASIC, (char*)"myCmdPort", 0);
		if (retval == -1) {
			//默认参数，不接受命令
			iCmdSock = 0;
		}
		else {
			iCmdSock = socket(AF_INET, SOCK_DGRAM, 0);
			if (iCmdSock == SOCKET_ERROR)
				iCmdSock = 0;
			else {
				ser_addr.sin_family = AF_INET;
				ser_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
				ser_addr.sin_port = htons(port);
				if (bind(iCmdSock, (sockaddr*)& ser_addr, sizeof(ser_addr)) == SOCKET_ERROR) {
					closesocket(iCmdSock);
					iCmdSock = 0;
				}
			}
		}
	}

	//	listen(s,5);
	
	//设置套接字为非阻塞态
	arg = 1;
	ioctlsocket(sock, FIONBIO, &arg);
	if (iCmdSock > 0) {
		arg = 1;
		ioctlsocket(iCmdSock, FIONBIO, &arg);
	}

	printf("\n---------------------------------------收发窗口--------------------------------------\n");

	//初始化

	if (iWorkMode == 11) data_form = 0;
	if (iWorkMode == 10) data_form = 1;
	if (iWorkMode == 1) data_form = 2;
	if (iWorkMode == 0) data_form = 3;

	if (my_work_mode == 1)
	{
		printf("\n当前设备号为 3 ，请输入目的设备号: ");
		scanf_s("%d", &Des_Device_number);
		getchar();
	}
	//模式选择

	if (my_work_mode == 1)
	{
		if (data_form == 0)
		{
			NO_MAX = 20;
		}

		if (data_form == 1)
		{
			//char auto_ch[16] = { 'h','e','l','l','o' };//form1
			NO_MAX = strlen(auto_ch);
		}

		if (data_form == 2)
		{
			printf("\nPlease input your words:");
			gets_s(hand_ch, 64);
			NO_MAX = strlen(hand_ch);
		}

		if (data_form == 3)
		{

			char* imageOutput;
			FILE* fp = NULL;

			char* file_path = NULL;
			file_path = (char*)malloc(64 * sizeof(char));
			printf("Please input your file path:");
			gets_s(file_path, 64);
			image_ch = code_image(file_path);

			//printf("\nlen_image:%d\n", strlen(image_ch));

			NO_MAX = strlen(image_ch);
		}
	}
	else
	{
		words = (char*)malloc(60000 * sizeof(char));
		NO_MAX = 100000;
	}

	timeout.tv_sec = 0; //时间间隔200毫秒
	timeout.tv_usec = 200000;

	initTimer();

	while (1) {

		FD_ZERO(&readfds);

		if (sock > 0) {
			FD_SET(sock, &readfds);
		}
		if (iCmdSock > 0) {
			FD_SET(iCmdSock, &readfds);
		}

		//采用了基于select机制，不断发送测试数据，和接收测试数据，也可以采用多线程，一线专发送，一线专接收的方案
		//设定超时时间
		//setSelectTimeOut(&timeout, &sBasicTimer);
		retval = select(0, &readfds, NULL, NULL, &timeout);//select机制，牛逼
		//if (true == isTimeOut(&sBasicTimer)) {
		if (!retval) {
			//printf("\nretval:%d\n", retval);
			if (!my_work_mode)
			{
				continue;
			}

			count++;

			Sleep(500);

			if (Initial_Flag && my_work_mode)
			{
				//向下层传递参数
				char* Des_Device = ten2two(Des_Device_number);
				int Device_len = 4;
				printf("\n向下层传递参数：%s\n", Des_Device);
				int intial_retval = sendto(sock, Des_Device, Device_len, 0, (sockaddr*)& remote_addr, sizeof(sockaddr_in));
				Initial_Flag = false;
				continue;
			}

			len = 8;
			if (NO < NO_MAX) {

				if (data_form == 0)
					sendBitBuf = get_eight_bites();

				if (data_form == 1)
					sendBitBuf = ascll_code(auto_ch[NO]);

				if (data_form == 2)
					sendBitBuf = ascll_code(hand_ch[NO]);

				if (data_form == 3)
					sendBitBuf = ascll_code(image_ch[NO]);

				retval = sendto(sock, sendBitBuf, len, 0, (sockaddr*)& remote_addr, sizeof(sockaddr_in));

				printf("\n发送数据：%s\n", sendBitBuf);

				int reflect_retval = 0;
				int loop_1;
				if (Des_Device_number > Max_Device || Des_Device_number == 15)
				{
					goto loop_1;
				}
				//重传

				timeval timeout_reflect;



				int loop;

				// setSelectTimeOut(&timeout_reflect, &sBasicTimer);
				//Sleep(5);

			loop:FD_ZERO(&readfds);
				if (sock > 0) {
					FD_SET(sock, &readfds);
				}
				if (iCmdSock > 0) {
					FD_SET(iCmdSock, &readfds);
				}

				timeout_reflect.tv_sec = 3; //时间间隔3秒
				timeout_reflect.tv_usec = 0;
				//printf("\nhere\n");
				reflect_retval = select(0, &readfds, NULL, NULL, &timeout_reflect);//select机制，牛逼
				//printf("\n2-here\n");
				if (!reflect_retval)
				{
					printf("\nreflect_retval:%d\n", reflect_retval);
					printf("\n失帧！！重传：%s\n", sendBitBuf);
					retval = sendto(sock, sendBitBuf, len, 0, (sockaddr*)& remote_addr, sizeof(sockaddr_in));
					iSndErrorCount++;
					goto loop;
				}
				if (FD_ISSET(sock, &readfds))
				{
					char* reflect_recvBuf = NULL;
					reflect_recvBuf = (char*)malloc(MAX_BUFFER_SIZE);
					int reflect_retval = recv(sock, reflect_recvBuf, MAX_BUFFER_SIZE, 0); //超过这个大小就不能愉快地玩耍了，因为缓冲不够大
					reflect_recvBuf[reflect_retval] = '\0';
					printf("\n接收反馈数据：%s\n", reflect_recvBuf);
					if (!if_ack(reflect_recvBuf))
					{
						//retval = sendto(sock, sendBitBuf, len, 0, (sockaddr*)& remote_addr, sizeof(sockaddr_in));
						iSndErrorCount++;
					}
				}

				//重传结束

			loop_1:sendBitBuf = NULL;

				NO++;
				if (retval <= 0) {
					iSndErrorCount++;
				}
				else {
					iSndTotal += retval;
					iSndTotalCount++;
				}

				if (NO % 5 == 0) {
					spin++;
					switch (spin) {
					case 1:
						printf("\r-");
						break;
					case 2:
						printf("\r\\");
						break;
					case 3:
						printf("\r|");
						break;
					case 4:
						printf("\r/");
						spin = 0;
						break;
					}
					if (lowerMode == 0) {
						printf("\n");
						cout << "共发送 " << iSndTotal << " 字节," << iSndTotalCount << " 次," << "发生 " << iSndErrorCount << " 次错误;";
						cout << " 共接收 " << iRcvTotal << " 字节," << iRcvTotalCount << " 次";
						printf("\n");
					}
					else {
						printf("\n");
						cout << "共发送 " << iSndTotal << " 位," << iSndTotalCount << " 次," << "发生 " << iSndErrorCount << " 次错误;";
						cout << " 共接收 " << iRcvTotal << " 位," << iRcvTotalCount << " 次";
						printf("\n");
					}
				}
			}
		}
		if (FD_ISSET(sock, &readfds) && !my_work_mode) {
			//printf("\n%d\n", sock);
			retval = recv(sock, recvBuf, MAX_BUFFER_SIZE, 0); //超过这个大小就不能愉快地玩耍了，因为缓冲不够大
			recvBuf[retval] = '\0';
			printf("\n接收数据：%s\n", recvBuf);
			if ((data_form == 1) || (data_form == 2))
			{
				char ch = ascll_decode(recvBuf);

				words[words_index] = ch;
				words_index++;
				words[words_index] = '\0';
				printf("\n译码：%s\n", words);
			}
			if (data_form == 3)
			{
				char ch = ascll_decode(recvBuf);

				words[words_index] = ch;
				words_index++;
				words[words_index] = '\0';
				//printf("\n译码：%s\n", words);
				if (words_index >= 972)
				{
					//printf("\nwords_index:%d\n", words_index);
					decode_image(words, image_size);
					return 0;
				}
			}
			//处理不正常的接收
			if (retval == 0) {
				closesocket(sock);
				sock = 0;
				printf("close a socket\n");
				continue;
			}
			else if (retval == -1) {
				retval = WSAGetLastError();
				if (retval == WSAEWOULDBLOCK || retval == WSAECONNRESET)
					continue;
				closesocket(sock);
				sock = 0;
				printf("close a socket\n");
				continue;
			}
			iRcvTotal += retval;
			iRcvTotalCount++;
			int iwk = 11;
			switch (iwk % 10) {
			case 1:
				if (lowerMode == 0) {
					//打印数据
					//收到数据后，打印
					cout << endl;
					cout << "共发送 " << iSndTotal << " 字节," << iSndTotalCount << " 次," << "发生 " << iSndErrorCount << " 次错误;";
					cout << " 共接收 " << iRcvTotal << " 字节," << iRcvTotalCount << " 次" << endl;
				}
				else {
					cout << endl;
					cout << "共发送 " << iSndTotal << " 位," << iSndTotalCount << " 次," << "发生 " << iSndErrorCount << " 次错误;";
					cout << " 共接收 " << iRcvTotal << " 位," << iRcvTotalCount << " 次" << endl;

				}
				break;
			case 0:
				break;
			}
		}
		if (iCmdSock == 0)
			continue;
		if (FD_ISSET(iCmdSock, &readfds)) {
			retval = recv(iCmdSock, recvBuf, MAX_BUFFER_SIZE, 0);
			if (retval <= 0) {
				continue;
			}
			if (strncmp(recvBuf, "exit", 5) == 0) {
				break;
			}
		}
		//NO++;
	}
	free(sendBitBuf);
	free(sendByteBuf);
	free(recvBuf);
	if (sock > 0)
		closesocket(sock);
	if (iCmdSock)
		closesocket(iCmdSock);
	WSACleanup();
	return 0;
}

char* get_eight_bites()
{
	//srand((unsigned)time(NULL));
	char* p;
	p = (char*)malloc(8 * sizeof(char));
	int i;
	for (i = 0; i < 8; i++)
	{
		if (rand() % 2 == 1)
			p[i] = '1';
		else
			p[i] = '0';
	}
	p[i] = '\0';
	return p;
}
void print_array(int* a)
{
	for (int i = 0; i < 8; i++)
		printf("%d", a[i]);
	printf("\n");
}
char* ascll_code(char ch)
{
	int cn = ch;
	char* p = NULL;
	p = (char*)malloc(8 * sizeof(char));
	p[0] = '0'; p[1] = '0'; p[2] = '0'; p[3] = '0'; p[4] = '0';
	p[5] = '0'; p[6] = '0'; p[7] = '0'; p[8] = '\0';
	int i;
	int t;
	for (i = 0; i < 8; i++)
	{
		if (cn % 2 == 1)
		{
			p[7 - i] = '1';
			cn = (cn - 1) / 2;
		}
		else
		{
			p[7 - i] = '0';
			cn = cn / 2;
		}
		if (cn == 0)
			break;
	}
	return p;
}
char ascll_decode(char* p)
{
	int s = 0;
	int i;
	if (p[0] == '1') s += 128;
	if (p[1] == '1') s += 64;
	if (p[2] == '1') s += 32;
	if (p[3] == '1') s += 16;
	if (p[4] == '1') s += 8;
	if (p[5] == '1') s += 4;
	if (p[6] == '1') s += 2;
	if (p[7] == '1') s += 1;
	char ch = s;
	return ch;
}
//image
char* base64_encode(const unsigned char* bindata, char* base64, int binlength)
{
	int i, j;
	unsigned char current;

	for (i = 0, j = 0; i < binlength; i += 3)
	{
		current = (bindata[i] >> 2);
		current &= (unsigned char)0x3F;
		base64[j++] = base64char[(int)current];

		current = ((unsigned char)(bindata[i] << 4)) & ((unsigned char)0x30);
		if (i + 1 >= binlength)
		{
			base64[j++] = base64char[(int)current];
			base64[j++] = '=';
			base64[j++] = '=';
			break;
		}
		current |= ((unsigned char)(bindata[i + 1] >> 4)) & ((unsigned char)0x0F);
		base64[j++] = base64char[(int)current];

		current = ((unsigned char)(bindata[i + 1] << 2)) & ((unsigned char)0x3C);
		if (i + 2 >= binlength)
		{
			base64[j++] = base64char[(int)current];
			base64[j++] = '=';
			break;
		}
		current |= ((unsigned char)(bindata[i + 2] >> 6)) & ((unsigned char)0x03);
		base64[j++] = base64char[(int)current];

		current = ((unsigned char)bindata[i + 2]) & ((unsigned char)0x3F);
		base64[j++] = base64char[(int)current];
	}
	base64[j] = '\0';
	return base64;
}
int base64_decode(const char* base64, unsigned char* bindata)
{
	int i, j;
	unsigned char k;
	unsigned char temp[4];
	for (i = 0, j = 0; base64[i] != '\0'; i += 4)
	{
		memset(temp, 0xFF, sizeof(temp));
		for (k = 0; k < 64; k++)
		{
			if (base64char[k] == base64[i])
				temp[0] = k;
		}
		for (k = 0; k < 64; k++)
		{
			if (base64char[k] == base64[i + 1])
				temp[1] = k;
		}
		for (k = 0; k < 64; k++)
		{
			if (base64char[k] == base64[i + 2])
				temp[2] = k;
		}
		for (k = 0; k < 64; k++)
		{
			if (base64char[k] == base64[i + 3])
				temp[3] = k;
		}

		bindata[j++] = ((unsigned char)(((unsigned char)(temp[0] << 2)) & 0xFC)) |
			((unsigned char)((unsigned char)(temp[1] >> 4) & 0x03));
		if (base64[i + 2] == '=')
			break;

		bindata[j++] = ((unsigned char)(((unsigned char)(temp[1] << 4)) & 0xF0)) |
			((unsigned char)((unsigned char)(temp[2] >> 2) & 0x0F));
		if (base64[i + 3] == '=')
			break;

		bindata[j++] = ((unsigned char)(((unsigned char)(temp[2] << 6)) & 0xF0)) |
			((unsigned char)(temp[3] & 0x3F));
	}
	return j;
}
char* code_image(char* file_path)
{
	FILE* fp = NULL;
	unsigned int imageSize;        //图片字节数
	char* imageBin;
	char* imageBase64;

	size_t result;
	char* ret;
	unsigned int base64StrLength;
	errno_t err;

	err = fopen_s(&fp, file_path, "rb");   //待编码图片
	if (NULL == fp)
	{
		printf("file open file");
		return 0;
	}
	//获取图片大小
	fseek(fp, 0L, SEEK_END);
	imageSize = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	//分配内存存储整个图片
	imageBin = (char*)malloc(sizeof(char) * imageSize);
	if (NULL == imageBin)
	{
		printf("malloc failed");
		return 0;
	}

	//读取图片
	result = fread(imageBin, 1, imageSize, fp);
	if (result != imageSize)
	{
		printf("file read failed");
		return 0;
	}
	fclose(fp);

	//分配编码后图片所在buffer
	imageBase64 = (char*)malloc(sizeof(char) * imageSize * 2);//因为编码一版会比源数据大1/3的样子，这里直接申请源文件一倍的空间
	if (NULL == imageBase64)
	{
		printf("malloc failed");
		return 0;
	}

	//base64编码
	base64_encode((const unsigned char*)imageBin, imageBase64, imageSize);
	base64StrLength = strlen(imageBase64);
	printf("base64 str length:%d\n", base64StrLength);
	printf("\ncode_pic->\n%s\n", imageBase64);
	return imageBase64;
}
unsigned int get_imagesize(char* file_path)
{
	FILE* fp = NULL;
	unsigned int imageSize;        //图片字节数

	errno_t err;

	err = fopen_s(&fp, file_path, "rb");   //待编码图片
	if (NULL == fp)
	{
		printf("file open file");
		return 0;
	}
	//获取图片大小
	fseek(fp, 0L, SEEK_END);
	imageSize = ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	printf("\n%d\n", imageSize);
	return imageSize;
}
void decode_image(char* imageBase64, unsigned int imageSize)
{

	FILE* fp = NULL;
	//图片字节数
	char* imageBin;

	char* imageOutput;
	size_t result;
	char* ret;
	unsigned int base64StrLength;





	//分配存储解码数据buffer
	imageOutput = (char*)malloc(sizeof(char) * imageSize);//解码后应该和源图片大小一致
	if (NULL == imageBase64)
	{
		printf("malloc failed");
		//return -1;
	}
	base64_decode(imageBase64, (unsigned char*)imageOutput);
	errno_t err_1;
	err_1 = fopen_s(&fp, "output.jpg", "wb");
	if (NULL == fp)
	{
		printf("file open file");
		//return -1;
	}
	fwrite(imageOutput, 1, imageSize, fp);
	fclose(fp);

	//free(imageBin);
	free(imageBase64);
	free(imageOutput);


}
char* ten2two(int s)
{
	//s = s % 16;
	char* p = NULL;
	p = (char*)malloc(4 * sizeof(char));
	p[0] = '0'; p[1] = '0'; p[2] = '0'; p[3] = '0'; p[4] = '\0';
	int i;
	int t;
	for (i = 0; i < 4; i++)
	{
		if (s % 2 == 1)
		{
			p[3 - i] = '1';
			s = (s - 1) / 2;
		}
		else
		{
			p[3 - i] = '0';
			s = s / 2;
		}
		if (s == 0)
			break;
	}
	return p;
}
int two2ten(char* p)
{
	int s = 0;
	int i;
	if (p[0] == '1') s += 8;
	if (p[1] == '1') s += 4;
	if (p[2] == '1') s += 2;
	if (p[3] == '1') s += 1;
	return s;
}
bool if_ack(char* s)
{
	int len = strlen(s);
	int i;
	int sum = 0;
	for (i = 0; i < len; i++)
	{
		if (s[i] == '1')
			sum++;

	}
	if (sum >= 3)
		return true;
	else
		return false;
}