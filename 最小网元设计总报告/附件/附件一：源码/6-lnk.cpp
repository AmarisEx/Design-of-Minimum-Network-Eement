// NetTester.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include <winsock2.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <Windows.h>
#include <iostream>
#include <conio.h>
#include "winsock.h"
#include <stdio.h>
#include "CfgFileParms.h"
#pragma comment (lib,"wsock32.lib")

#define MAX_BUFFER_SIZE 40000 //缓冲的最大大小

//定义全局变量
int my_work_mode = 2;//交换机:2;路由器：3
int serial_num = 0;//发
bool ERROR_FLAG = false;
int Frame_No = -2;//收
int my_Device_number = 0;
int Source_Device_number = 0;
int Des_Device_number = 0;
bool Initial_Flag = true;
bool Retrans_Flag = false;
bool Retrans_Flag_s = false;
int Max_Device = 6;

using namespace std;
//基于select的定时器结构，目的是把数据的收发和定时都统一到一个事件驱动框架下
//可以有多个定时器，本设计实现了一个基准定时器，为周期性10ms定时，也可以当作是一种心跳计时器
//其余的定时器可以在这个基础上完成，可行的方案存在多种
//看懂设计思路后，自行扩展以满足需要
//基准定时器一开启就会立即触发一次


struct threadTimer_t {
	int iType; //为0表示周期性定时器，定时达到后，会自动启动下一次定时
	ULONG ulInterval;
	LARGE_INTEGER llStopTime;
}sBasicTimer;

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
void print_data_bit(char* A, int length, int iMode)
{
	int i, j;
	char B[8];
	int lineCount = 0;
	if (iMode == 0) {
		for (i = 0; i < length; i++) {
			lineCount++;
			if (A[i] == 0) {
				printf("0 ");
			}
			else {
				printf("1 ");
			}
			if (lineCount % 8 == 0) {
				printf(" ");
			}
			if (lineCount >= 40) {
				printf("\n");
				lineCount = 0;
			}
		}
	}
	else {
		for (i = 0; i < length; i++) {
			lineCount++;
			code(A[i], B, 8);
			for (j = 0; j < 8; j++) {
				if (B[j] == 0) {
					printf("0 ");
				}
				else {
					printf("1 ");
				}
				lineCount++;
			}
			printf(" ");
			if (lineCount >= 40) {
				printf("\n");
				lineCount = 0;
			}
		}
	}
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
#pragma  comment(lib,"ws2_32.lib")

////////my_functions

//成帧函数
char* get_frame(char* s);
bool define_fiveone(char* p, int index);
//差错函数
static const UCHAR aucCRCHi[] = {
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40
};
static const UCHAR aucCRCLo[] = {
	0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7,
	0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E,
	0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09, 0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9,
	0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC,
	0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
	0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32,
	0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D,
	0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A, 0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38,
	0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF,
	0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
	0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1,
	0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4,
	0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F, 0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB,
	0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA,
	0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
	0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0,
	0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97,
	0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C, 0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E,
	0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89,
	0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
	0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83,
	0x41, 0x81, 0x80, 0x40
};
unsigned short usMBCRC16(unsigned short* pucFrame, unsigned short usLen);
char* get_crc(char* s);
char* add_crc(char* p, char* crc);
char* add_serial_number(char* p, char* serial);


char* get_ph_data_bit(char* A, int length, int iMode);
char* locate_frame(char* s);
bool define_frame(char* p, int index);
char* de_frame(char* s);
char* de_crc(char* s);
bool if_ack(char* s);
char* get_reflect(int flag);

char* ten2two(int s);
int two2ten(char* p);

int  de_serial_number(char* p);
char* new_de_frame(char* s);
char* new_locate_frame(char* s);

char* add_dirty(char* p);
bool get_dirty(char* p);

//封装
char* encapsulate_frame(char* revData);
char* decapsulate_frame(char* recbits);

//other
char* get_eight_bites();
void print_array(int* a);

//交换机
char* add_code_node(char* p);
int get_decode_node(char* s);
char* add_code_source_node(char* p);
int get_decode_source_node(char* s);
char* get_retrans_frame(char* p);

bool disabled_0 = false;//解决广播风暴
bool disabled_1 = false;//解决广播风暴
char* get_broadcast_frame(char* p);

struct Switch_Table
{
	int addr;
	int port;
};
Switch_Table* st = (Switch_Table*)malloc(15 * sizeof(Switch_Table));
int st_size = 2;
void print_switch_table();
int find_st_port(int addr);
void reverse_addr_learn(int addr, int port);

//路由器
struct Router_Table
{
	int destination;
	int gataway;
	int port;
	int metric;
};
Router_Table* rt = (Router_Table*)malloc(15 * sizeof(Router_Table));


int rt_size = 5;
void print_router_table();
int find_rt_port(int destination);

int main(int argc, char* argv[])
{
	int iwk = 11;//没啥用，代替iWorkMode

	printf("***********************************6号网元 链路层************************************\n\n\n");

	printf("---------------------------------------相关参数--------------------------------------\n");

	SOCKET sock;
	struct sockaddr_in local_addr;
	struct sockaddr_in upper_addr;
	struct sockaddr_in lower_addr[10];  //最多10个下层对象，数组下标是下层实体在编号
	int lowerMode[10];
	struct sockaddr_in remote_addr;
	int lowerNumber;
	int len;
	char* buf;
	char* sendbuf;
	WSAData wsa;
	int retval, iSndRetval;
	fd_set readfds;
	timeval timeout;
	int i;
	unsigned long arg;
	int linecount = 0;
	int port;
	string s1, s2, s3;
	int count = 0;
	int spin = 0;
	char* cpIpAddr;
	int iRecvIntfNo;
	int iWorkMode = 0;
	int autoSendTime = 10;
	int autoSendSize = 800;
	int iSndTotal = 0;
	int iSndTotalCount = 0;
	int iSndErrorCount = 0;
	int iRcvForward = 0;
	int iRcvForwardCount = 0;
	int iRcvToUpper = 0;
	int iRcvToUpperCount = 0;
	int iRcvUnknownCount = 0;
	//带外命令接口
	SOCKET iCmdSock = 0;
	sockaddr_in local_cmd_addr;
	buf = NULL;
	buf = (char*)malloc(8 * sizeof(char));
	sendbuf = (char*)malloc(MAX_BUFFER_SIZE);
	if (sendbuf == NULL || buf == NULL) {
		if (sendbuf != NULL) {
			free(sendbuf);
		}
		if (buf != NULL) {
			free(buf);
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
		s2 = "LNK";
		s3 = argv[2];
	}
	else {
		//从命令行读取
		/*cout << "请输入设备号：";
		cin >> s1;*/
		s1 = '6';
		//cout << "请输入层次名（大写）：";
		//cin >> s2;
		s2 = "LNK";
		/*cout << "请输入实体号：";
		cin >> s3;*/
		s3 = '0';
	}
	Source_Device_number = s1[0] - 48;

	//交换表初始化

	st[0].addr = 1;
	st[0].port = 0;
	st[1].addr = 5;
	st[1].port = 0;

	//路由表初始化
	rt[0].destination = 1; rt[0].gataway = 1; rt[0].port = 2; rt[0].metric = 1;
	rt[1].destination = 3; rt[1].gataway = 3; rt[1].port = 0; rt[1].metric = 1;
	rt[2].destination = 4; rt[2].gataway = 3; rt[2].port = 0; rt[2].metric = 3;
	rt[3].destination = 5; rt[3].gataway = 1; rt[3].port = 2; rt[3].metric = 2;
	rt[4].destination = 6; rt[4].gataway = 1; rt[4].port = 2; rt[4].metric = 2;

	WSAStartup(0x101, &wsa);
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == SOCKET_ERROR)
		return 0;

	cfgParms.setDeviceID(s1);
	cfgParms.setLayer(s2);
	cfgParms.setEntityID(s3);
	cfgParms.read();
	cfgParms.print(); //打印出来看看是不是读出来了

	//////my_change
	cfgParms.isConfigExist = true;
	/////
	if (!cfgParms.isConfigExist) {
		//从键盘输入，需要连接的API端口号
		//偷个懒，要求必须填好配置文件
		return 0;
	}

	//取本层实体参数，并设置
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	retval = cfgParms.getValueInt(&port, CCfgFileParms::BASIC, (char*)"myPort", 0);
	if (0 > retval) {
		printf("参数错误\n");
		return 0;
	}
	local_addr.sin_port = htons(port);
	if (bind(sock, (sockaddr*)& local_addr, sizeof(sockaddr_in)) != 0) {
		printf("参数错误\n");
		return 0;

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


	//读上层实体参数
	upper_addr.sin_family = AF_INET;
	cpIpAddr = cfgParms.getValueStr(CCfgFileParms::UPPER, (char*)"upperIPAddr", 0);
	if (cpIpAddr == NULL) {
		printf("参数错误\n");
		return 0;
	}
	upper_addr.sin_addr.S_un.S_addr = inet_addr(cpIpAddr);

	retval = cfgParms.getValueInt(&port, CCfgFileParms::UPPER, (char*)"upperPort", 0);
	if (0 > retval) {
		printf("参数错误\n");
		return 0;
	}
	upper_addr.sin_port = htons(port);


	//取下层实体参数，并设置
	//先取数量
	lowerNumber = cfgParms.getNumber(CCfgFileParms::LOWER);
	if (0 > lowerNumber) {
		printf("参数错误\n");
		return 0;
	}
	//逐个读取
	for (i = 0; i < lowerNumber; i++) {
		lower_addr[i].sin_family = AF_INET;
		cpIpAddr = cfgParms.getValueStr(CCfgFileParms::LOWER, (char*)"lowerIPAddr", i);
		if (cpIpAddr == NULL) {
			printf("参数错误\n");
			return 0;
		}
		lower_addr[i].sin_addr.S_un.S_addr = inet_addr(cpIpAddr);

		retval = cfgParms.getValueInt(&port, CCfgFileParms::LOWER, (char*)"lowerPort", i);
		if (0 > retval) {
			printf("参数错误\n");
			return 0;
		}
		lower_addr[i].sin_port = htons(port);
		//低层接口是Byte或者是bit,默认是bit
		retval = cfgParms.getValueInt(&(lowerMode[i]), CCfgFileParms::LOWER, (char*)"lowerMode", i);
		if (0 > retval) {
			lowerMode[i] = 0;
		}
	}
	retval = cfgParms.getValueInt(&port, CCfgFileParms::BASIC, (char*)"myCmdPort", 0);
	if (retval == -1) {
		//默认参数，不接受命令
		iCmdSock = 0;
	}
	else {
		iCmdSock = socket(AF_INET, SOCK_DGRAM, 0);
		if (iCmdSock == SOCKET_ERROR) {
			iCmdSock = 0;
		}
		else {
			local_cmd_addr.sin_family = AF_INET;
			local_cmd_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
			local_cmd_addr.sin_port = htons(port);
			if (bind(iCmdSock, (sockaddr*)& local_cmd_addr, sizeof(sockaddr_in)) == SOCKET_ERROR) {
				closesocket(iCmdSock);
				iCmdSock = 0;
			}
		}
	}

	//设置套接字为非阻塞态
	arg = 1;
	ioctlsocket(sock, FIONBIO, &arg);
	if (iCmdSock > 0) {
		arg = 1;
		ioctlsocket(iCmdSock, FIONBIO, &arg);
	}

	if (my_work_mode == 2)
	{
		print_switch_table();
	}
	if (my_work_mode == 3)
	{
		print_router_table();
	}


	printf("\n---------------------------------------收发窗口--------------------------------------\n");

	initTimer();
	while (1) {
		FD_ZERO(&readfds);
		//采用了基于select机制，不断发送测试数据，和接收测试数据，也可以采用多线程，一线专发送，一线专接收的方案
		//设定超时时间
		if (sock > 0) {
			FD_SET(sock, &readfds);
		}
		if (iCmdSock > 0) {
			FD_SET(iCmdSock, &readfds);
		}
		setSelectTimeOut(&timeout, &sBasicTimer);
		retval = select(0, &readfds, NULL, NULL, &timeout);
		if (true == isTimeOut(&sBasicTimer)) {
			//TimeOut();
			continue;
		}

		if (FD_ISSET(sock, &readfds)) {

			len = sizeof(sockaddr_in);
			//int buf_len = 64;
			//retval = recvfrom(sock, buf, buf_len, 0, (sockaddr*)& remote_addr, &len); //超过这个大小就不能愉快地玩耍了，因为缓冲不够大
			buf = (char*)malloc(MAX_BUFFER_SIZE);
			retval = recvfrom(sock, buf, MAX_BUFFER_SIZE, 0, (sockaddr*)& remote_addr, &len); //超过这个大小就不能愉快地玩耍了，因为缓冲不够大
			buf[retval] = '\0';



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


			//收到数据后,通过源头判断是上层还是下层数据
			if (remote_addr.sin_port == upper_addr.sin_port) {
				//IP地址也应该比对的，偷个懒
				//是高层数据，从接口0发出去
				//printf("\ntest:563\n");
				if (Initial_Flag)
				{
					//接收上层所传递参数
					Des_Device_number = two2ten(buf);
					Initial_Flag = false;
					continue;
				}
				printf("\n接收数据：%s\n", buf);
				if (1) {

					if (Retrans_Flag_s)
						serial_num--;

					char* my_sendbuf = NULL;
					my_sendbuf = (char*)malloc(60 * sizeof(char));
					my_sendbuf = encapsulate_frame(buf);
					printf("\n封装：%s\n", my_sendbuf);
					iSndRetval = strlen(my_sendbuf);

					int send_port = 0;
					if (my_work_mode == 2)
					{
						send_port = find_st_port(Des_Device_number);
					}
					if (my_work_mode == 3)
					{
						send_port = find_rt_port(Des_Device_number);
					}

					if (send_port == -1)
					{
						iSndRetval = sendto(sock, my_sendbuf, iSndRetval, 0, (sockaddr*) & (lower_addr[0]), sizeof(sockaddr_in));
					}
					iSndRetval = sendto(sock, my_sendbuf, iSndRetval, 0, (sockaddr*) & (lower_addr[send_port]), sizeof(sockaddr_in));
					serial_num++;

					Retrans_Flag_s = true;
				}

				if (iSndRetval <= 0) {
					iSndErrorCount++;
				}
				else {
					iSndTotal += iSndRetval;
					iSndTotalCount++;
				}

				switch (iwk % 10) {
				case 1:
					//打印收发数据


					printf("\n共发送: %d 位, %d 次,转发 %d 位，%d 次;递交 %d 位，%d 次，发送错误 %d 次__________________________\n", iSndTotal, iSndTotalCount, iRcvForward, iRcvForwardCount, iRcvToUpper, iRcvToUpperCount, iSndErrorCount);

					break;
				case 0:
					break;
				}
				Sleep(10);
			}
			else {
				//下层收到的数据

				char* recbits = NULL;
				recbits = (char*)malloc(retval * sizeof(char));
				recbits = get_ph_data_bit(buf, retval, 0);
				recbits[retval] = '\0';


				if (remote_addr.sin_port == lower_addr[0].sin_port) {

					//接口0的数据，寻址判断
					char* my_initial_frame = locate_frame(recbits);

					if (!my_initial_frame)
					{
						printf("\nmy_initial_frame=NULL\n");
						continue;
					}

					int serial_num_r = de_serial_number(my_initial_frame);

					int D_n = get_decode_node(my_initial_frame);

					if (D_n != Source_Device_number)
					{
						continue;
					}

					//反向地址学习
					int S_n = get_decode_source_node(my_initial_frame);
					if (S_n != Source_Device_number)
					{
						if (my_work_mode == 2)
						{
							reverse_addr_learn(S_n, 0);
						}
					}

					

					//解决广播风暴
					if (D_n == Source_Device_number)
					{
						disabled_0 = false;
					}
					if (disabled_0)
					{
						continue;
					}
					bool dirty_t = get_dirty(my_initial_frame);

					if (D_n > Max_Device && D_n != 15)
					{
						char* broadcast_frame = get_broadcast_frame(my_initial_frame);
						int b_f_len = strlen(broadcast_frame);
						b_f_len = sendto(sock, broadcast_frame, b_f_len, 0, (sockaddr*) & (lower_addr[1]), sizeof(sockaddr_in));
						disabled_0 = true;
						printf("\n未知广播:%s\n设备 %d 物理层接口 0 断开！\n", my_initial_frame, Source_Device_number);
						continue;
					}
					if (D_n == 15)
					{
						int b_f_len = strlen(my_initial_frame);
						b_f_len = sendto(sock, my_initial_frame, b_f_len, 0, (sockaddr*) & (lower_addr[1]), sizeof(sockaddr_in));
						printf("\n收到广播帧：%s, 转发！\n", my_initial_frame);
						continue;
					}

					printf("\n目的设备号：%d\n", D_n);
					if ((dirty_t && Des_Device_number) || (!dirty_t && (D_n == Source_Device_number || D_n == 15)) || (D_n == Source_Device_number))
					{
						iRecvIntfNo = 0;
						if (lowerNumber >= 1) {
							if (1) {
								printf("\n从接口 0 收到数据：%s\n", recbits);
								char* my_recbits = NULL;
								my_recbits = (char*)malloc(8 * sizeof(char));

								//递交反馈信号
								if (dirty_t && Des_Device_number)
								{
									Retrans_Flag_s = false;

									my_recbits = de_frame(my_initial_frame);
									iSndRetval = sendto(sock, my_recbits, strlen(my_recbits), 0, (sockaddr*) & (upper_addr), sizeof(sockaddr_in));
									printf("\n递交反馈：%s\n", my_recbits);
									continue;
								}

								Frame_No++;
								my_recbits = decapsulate_frame(recbits);

								if (!ERROR_FLAG)
									printf("\n解封：%s\n", my_recbits);
								else
								{
									printf("\n解封失败！\n");
									Frame_No--;
									//continue;

								}
								int my_recbits_length = 8;
								//iSndRetval = BitArrayToByteArray(buf, retval, sendbuf, MAX_BUFFER_SIZE);
								if (ERROR_FLAG)
								{
									printf("\nERROR_FLAG\n");
									iSndErrorCount++;
									continue;
								}
								else
								{
									Retrans_Flag = true;
									int f = 1;
									char* reflect = NULL;
									reflect = get_reflect(f);
									reflect = get_retrans_frame(my_initial_frame);
									int send_port = 0;
									if (my_work_mode == 2)
									{
										send_port = find_st_port(S_n);
									}
									if (my_work_mode == 3)
									{
										send_port = find_rt_port(S_n);
									}
									int reflect_len = sendto(sock, reflect, strlen(reflect), 0, (sockaddr*) & (lower_addr[send_port]), sizeof(sockaddr_in));
									printf("\n发送确认帧：%s\n", reflect);

									Retrans_Flag = false;
								}
								if (serial_num_r == (Frame_No % 16))
								{
									printf("\n确认帧丢失！！\n");
									Frame_No--;
									continue;
								}
								iSndRetval = sendto(sock, my_recbits, my_recbits_length, 0, (sockaddr*) & (upper_addr), sizeof(sockaddr_in));

								if (iSndRetval <= 0) {
									iSndErrorCount++;
								}
								else {
									iRcvToUpper += iSndRetval;
									iRcvToUpperCount++;
								}
							}



						}
						printf("\n从接口0接收数据：%s\n", recbits);
						printf("\n将接口0数据 %d 比特递交给上层\n", retval);
					}
					if ((dirty_t && !Des_Device_number) || (!dirty_t && (D_n != Source_Device_number || D_n == 15)))
					{
						/*if (dirty_t)
						{
							Retrans_Flag = true;
							int f = 1;
							char* reflect_t = NULL;
							reflect_t = get_reflect(f);
							recbits = NULL;
							recbits = (char*)malloc(45 * sizeof(char));
							recbits = encapsulate_frame(reflect_t);

							Retrans_Flag = false;

						}*/
						int trans_frame_len = strlen(recbits);
						int send_port = 0;
						if (my_work_mode == 2)
						{
							send_port = find_st_port(D_n);
						}
						if (my_work_mode == 3)
						{
							send_port = find_rt_port(D_n);
						}

						trans_frame_len = sendto(sock, recbits, trans_frame_len, 0, (sockaddr*) & (lower_addr[send_port]), sizeof(sockaddr_in));
						printf("\n从接口0接收数据：%s\n", recbits);
						printf("\n将接口0数据 %d 比特转发给接口%d \n", strlen(recbits), send_port);

						if (trans_frame_len <= 0) {
							iSndErrorCount++;
						}
						else {
							iRcvForward += trans_frame_len;
							iRcvForwardCount++;
						}
					}
					Sleep(10);
				}


				else if (remote_addr.sin_port == lower_addr[1].sin_port) {




					iRecvIntfNo = 1;
					//接口1的数据，寻址判断

					char* my_initial_frame = locate_frame(recbits);
					if (!my_initial_frame)
					{
						printf("\nmy_initial_frame=NULL\n");
						continue;
					}

					int serial_num_r = de_serial_number(my_initial_frame);

					//反向地址学习
					int S_n = get_decode_source_node(my_initial_frame);
					if (S_n != Source_Device_number)
					{
						if (my_work_mode == 2)
						{
							reverse_addr_learn(S_n, 1);
						}
					}

					int D_n = get_decode_node(my_initial_frame);

					//解决广播风暴
					if (D_n == Source_Device_number)
					{
						disabled_1 = false;
					}

					if (disabled_1)
					{
						continue;
					}

					bool dirty_t = get_dirty(my_initial_frame);

					//未知广播
					if (D_n > Max_Device && D_n != 15)
					{
						char* broadcast_frame = get_broadcast_frame(my_initial_frame);
						int b_f_len = strlen(broadcast_frame);
						b_f_len = sendto(sock, broadcast_frame, b_f_len, 0, (sockaddr*) & (lower_addr[0]), sizeof(sockaddr_in));
						disabled_1 = true;
						printf("\n未知广播:%s\n设备 %d 物理层接口 1 断开！\n", my_initial_frame, Source_Device_number);
						continue;
					}
					if (D_n == 15)
					{
						int b_f_len = strlen(my_initial_frame);
						b_f_len = sendto(sock, my_initial_frame, b_f_len, 0, (sockaddr*) & (lower_addr[0]), sizeof(sockaddr_in));
						printf("\n收到广播帧：%s, 转发！\n", my_initial_frame);
						continue;
					}

					printf("\n目的设备号：%d\n", D_n);
					if ((dirty_t && Des_Device_number) || (!dirty_t && (D_n == Source_Device_number || D_n == 15)) || (D_n == Source_Device_number))
					{
						iRecvIntfNo = 0;
						if (lowerNumber >= 1) {
							if (1) {
								printf("\n从接口 1 收到数据：%s\n", recbits);
								char* my_recbits = NULL;
								my_recbits = (char*)malloc(8 * sizeof(char));

								//递交反馈信号
								if (dirty_t && Des_Device_number)
								{
									Retrans_Flag_s = false;
									my_recbits = de_frame(my_initial_frame);
									iSndRetval = sendto(sock, my_recbits, strlen(my_recbits), 0, (sockaddr*) & (upper_addr), sizeof(sockaddr_in));
									printf("\n递交反馈：%s\n", my_recbits);
									continue;
								}

								Frame_No++;
								my_recbits = decapsulate_frame(recbits);

								if (!ERROR_FLAG)
									printf("\n解封：%s\n", my_recbits);
								else
								{
									printf("\n解封失败！\n");
									Frame_No--;
									//continue;

								}
								int my_recbits_length = 8;

								if (ERROR_FLAG)
								{

									printf("\nERROR_FLAG\n");
									iSndErrorCount++;
									continue;
								}
								else
								{
									Retrans_Flag = true;
									int f = 1;
									char* reflect = NULL;
									reflect = get_reflect(f);
									reflect = get_retrans_frame(my_initial_frame);
									int send_port = 0;
									if (my_work_mode == 2)
									{
										send_port = find_st_port(S_n);
									}
									if (my_work_mode == 3)
									{
										send_port = find_rt_port(S_n);
									}

									int reflect_len = sendto(sock, reflect, strlen(reflect), 0, (sockaddr*) & (lower_addr[send_port]), sizeof(sockaddr_in));
									printf("\n发送确认帧：%s\n", reflect);

									Retrans_Flag = false;
								}

								if (serial_num_r == (Frame_No % 16))
								{
									printf("\n确认帧丢失！！\n");
									Frame_No--;
									continue;
								}
								iSndRetval = sendto(sock, my_recbits, my_recbits_length, 0, (sockaddr*) & (upper_addr), sizeof(sockaddr_in));

								if (iSndRetval <= 0) {
									iSndErrorCount++;
								}
								else {
									iRcvToUpper += iSndRetval;
									iRcvToUpperCount++;
								}
							}



						}
						printf("\n从接口1接收数据：%s\n", recbits);
						printf("\n将接口1数据 %d 比特递交给上层\n", retval);
					}
					if ((dirty_t && !Des_Device_number) || (!dirty_t && (D_n != Source_Device_number || D_n == 15)))
					{
						/*if (dirty_t)
						{
							Retrans_Flag = true;
							int f = 1;
							char* reflect_t = NULL;
							reflect_t = get_reflect(f);
							recbits = NULL;
							recbits = (char*)malloc(45 * sizeof(char));
							recbits = encapsulate_frame(reflect_t);

							Retrans_Flag = false;

						}*/
						int trans_frame_len = strlen(recbits);
						int send_port = 0;
						if (my_work_mode == 2)
						{
							send_port = find_st_port(D_n);
						}
						if (my_work_mode == 3)
						{
							send_port = find_rt_port(D_n);
						}

						trans_frame_len = sendto(sock, recbits, trans_frame_len, 0, (sockaddr*) & (lower_addr[send_port]), sizeof(sockaddr_in));
						printf("\n从接口1接收数据：%s\n", recbits);
						printf("\n将接口1数据 %d 比特转发给接口%d \n", strlen(recbits), send_port);

						if (trans_frame_len <= 0) {
							iSndErrorCount++;
						}
						else {
							iRcvForward += trans_frame_len;
							iRcvForwardCount++;
						}
					}

				}
				else {
					//不明来源，打印提示
					iRcvUnknownCount++;
					switch (iwk % 10) {
					case 1:
						//打印收发数据
						printf("\n不明来源数据 %d 次__________________________\n", iRcvUnknownCount);
						print_data_bit(buf, retval, 1);
						break;
					case 0:
						break;
					}
					continue;
				}
				//打印
				switch (iwk % 10) {
				case 1:
					//打印收发数据


					printf("\n共发送: %d 位, %d 次,转发 %d 位，%d 次;递交 %d 位，%d 次，发送错误 %d 次__________________________\n", iSndTotal, iSndTotalCount, iRcvForward, iRcvForwardCount, iRcvToUpper, iRcvToUpperCount, iSndErrorCount);
					//print_data_bit(buf, retval, lowerMode[iRecvIntfNo]);
					break;
				case 0:
					break;
				}
			}
			Sleep(10);
		}
		if (iCmdSock == 0)
			continue;
		if (FD_ISSET(iCmdSock, &readfds)) {
			retval = recv(iCmdSock, buf, MAX_BUFFER_SIZE, 0);
			if (retval <= 0) {
				continue;
			}
			printf("\n接收数据：%s\n", buf);
			if (strncmp(buf, "exit", 5) == 0) {
				break;
			}
		}
	}
	free(sendbuf);
	free(buf);
	if (sock > 0)
		closesocket(sock);
	if (iCmdSock)
		closesocket(iCmdSock);
	WSACleanup();
	return 0;
}

char* get_frame(char* s)
{
	int i;
	for (i = 0; i < strlen(s) - 5; i++)
	{

		char* q = NULL;
		q = (char*)malloc(50 * sizeof(char));
		if (define_fiveone(s, i))
		{
			int j;
			for (j = strlen(s) + 1; j > i + 5; j--)
				s[j] = s[j - 1];
			s[j] = '0';

		}

	}
	char* p = NULL;
	p = (char*)malloc(50 * sizeof(char));

	p[0] = '0'; p[1] = '1'; p[2] = '1'; p[3] = '1'; p[4] = '1'; p[5] = '1'; p[6] = '1'; p[7] = '0';
	for (i = 0; i < strlen(s); i++)
	{
		p[i + 8] = s[i];
	}
	p[i + 8] = '0'; p[i + 9] = '1'; p[i + 10] = '1'; p[i + 11] = '1'; p[i + 12] = '1'; p[i + 13] = '1';
	p[i + 14] = '1'; p[i + 15] = '0'; p[i + 16] = '\0';
	return p;
}
bool define_fiveone(char* p, int index)
{
	int i;
	for (i = 0; i < 5; i++)
	{
		if (p[index + i] == '0')
			return false;
	}
	return true;
}
char* get_crc(char* s)
{
	int msk = 1;

	unsigned short D_s[8];
	for (int i = 0; i < 8; i++)
	{
		if (s[i] == '1')
			D_s[i] = 1;
		else
			D_s[i] = 0;
	}
	unsigned short D_r = usMBCRC16(D_s, 8);

	int crc[10] = { NULL };
	for (int i = 0; i < 8; i++)
	{
		crc[i] = (int)((D_r & msk) ? 1 : 0);
		msk <<= 1;
	}
	char* new_crc = NULL;
	new_crc = (char*)malloc(10 * sizeof(char));
	int i;
	for (i = 0; i < 8; i++)
	{
		if (crc[i] == 1)
			new_crc[i] = '1';
		else
			new_crc[i] = '0';
	}
	new_crc[i] = '\0';
	return new_crc;
}
unsigned short usMBCRC16(unsigned short* pucFrame, unsigned short usLen)
{
	unsigned char ucCRCHi = 0xFF;
	unsigned char ucCRCLo = 0xFF;
	int iIndex;
	while (usLen--)
	{
		iIndex = ucCRCLo ^ *(pucFrame++);
		ucCRCLo = (unsigned char)(ucCRCHi ^ aucCRCHi[iIndex]);
		ucCRCHi = aucCRCLo[iIndex];
	}
	return (unsigned short)(ucCRCHi << 8 | ucCRCLo);
}
char* add_crc(char* p, char* crc)
{
	int n = strlen(p);
	int i;
	char* q = NULL;
	q = (char*)malloc(100 * sizeof(char));
	for (i = 0; i < n - 8; i++)
		q[i] = p[i];
	int j;
	int k = 0;
	for (j = i; j < i + 8; j++, k++)
		q[j] = crc[k];
	int u = j;
	for (j = u; j < u + 8; j++, i++)
		q[j] = p[i];
	q[j] = '\0';
	return q;
}
void print_array(int* a)
{
	for (int i = 0; i < 8; i++)
		printf("%d", a[i]);
	printf("\n");
}
char* get_eight_bites()
{
	//srand((unsigned)time(NULL));
	char* p = NULL;
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
char* de_frame(char* s)
{
	char* p = NULL;
	p = (char*)malloc(8 * sizeof(char));
	int len = strlen(s);
	int i;
	if (len == 45)
	{

		for (i = 0; i < 8; i++)
			p[i] = s[i + 21];
		p[i] = '\0';

	}
	else
	{
		for (i = 0; i < 9; i++)
		{

			if (define_fiveone(s, i + 21))
			{
				p[i] = '1'; p[i + 1] = '1'; p[i + 2] = '1'; p[i + 3] = '1'; p[i + 4] = '1';
				int j = 0;
				for (j = 0; j < 3 - i; j++)
				{
					p[i + j + 5] = s[i + j + 27];
				}
				p[i + j + 5] = '\0';
				return p;
			}
			else
				p[i] = s[i + 21];
		}
		p[i] = '\0';
	}
	return p;
}
char* de_crc(char* s)
{
	char* p = NULL;
	p = (char*)malloc(8 * sizeof(char));
	int len = strlen(s);
	int i, j;
	for (i = 0, j = len - 16; i < 8; i++, j++)
	{
		p[i] = s[j];
	}
	p[i] = '\0';
	return p;
}
char* get_ph_data_bit(char* A, int length, int iMode)
{
	int i, j;
	char B[8];
	char* p = NULL;
	p = (char*)malloc(length * sizeof(char));
	int lineCount = 0;

	for (i = 0; i < length; i++)
	{
		lineCount++;
		if (A[i] == '0')
		{
			p[i] = '0';
		}
		else
		{
			p[i] = '1';
		}
		if (lineCount % 8 == 0)
		{
			//printf(" ");
		}
		if (lineCount >= 40)
		{
			//printf("\n");
			lineCount = 0;
		}

	}

	return p;
}
char* locate_frame(char* s)
{
	int start; int end;
	int i, j;
	char* p = NULL;

	for (j = 0; j < strlen(s); j++)
	{
		for (i = j; i < strlen(s) - 8; i++)
		{
			if (define_frame(s, i))
			{
				start = i;
				break;
			}
		}
		for (i = strlen(s) - 8; i >= j; i--)
		{
			if (define_frame(s, i))
			{
				end = i + 7;
				break;
			}
		}
		if ((end - start + 1) >= 45 && (end - start + 1) <= 47)
		{
			p = (char*)malloc((end - start + 1) * sizeof(char));
			int k, r;
			for (k = start, r = 0; k <= end; k++, r++)
				p[r] = s[k];
			p[r] = '\0';
			return p;
		}
		else
			continue;
	}
	return p;

}
bool define_frame(char* p, int index)
{
	if (p[index] != '0')
		return false;
	int i;
	for (i = 1; i <= 6; i++)
	{
		if (p[index + i] != '1')
			return false;
	}
	if (p[index + i] != '0')
		return false;
	return true;
}
char* get_reflect(int flag)
{

	char* p;
	p = (char*)malloc(8 * sizeof(char));
	int i;
	/*char c = '1';
	if (flag == 0)
		c = '0';*/
	if (flag == 1)
	{
		for (i = 0; i < 8; i++)
		{
			p[i] = '1';
		}
	}
	else
	{
		for (i = 0; i < 8; i++)
		{
			p[i] = '0';
		}
	}
	p[i] = '\0';
	return p;
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
char* add_serial_number(char* p, char* serial)
{
	int n = strlen(p);
	int i;
	char* q;
	q = (char*)malloc(100 * sizeof(char));
	for (i = 0; i < 8; i++)
		q[i] = p[i];
	int j;
	int k = 0;
	for (j = i; j < i + 4; j++, k++)
		q[j] = serial[k];
	int u = j;
	for (j = u; j < n + 4; j++, i++)
		q[j] = p[i];
	q[j] = '\0';
	return q;
}
int de_serial_number(char* s)
{
	int sn;
	char* p = NULL;
	p = (char*)malloc(4 * sizeof(char));
	int len = strlen(s);
	int i;
	for (i = 0; i < 4; i++)
	{
		p[i] = s[i + 17];
	}
	p[i] = '\0';
	sn = two2ten(p);
	return sn;
}
char* decapsulate_frame(char* recbits)
{
	int ii;
	ERROR_FLAG = false;
	//Frame_No++;
	char* frame = NULL;
	frame = (char*)malloc(strlen(recbits) * sizeof(char));
	char* bits = NULL;
	bits = (char*)malloc(8 * sizeof(char));
	char* crc = NULL;
	crc = (char*)malloc(8 * sizeof(char));
	char* re_crc = NULL;
	re_crc = (char*)malloc(8 * sizeof(char));
	//提取帧
	printf("\n-------------提取帧----------------\n");

	//frame = de_frame(recbits);
	frame = locate_frame(recbits);
	if (frame)
	{
		ii = 1;
		printf("\n成功提取帧：");
		printf("%s\n", frame);

	}
	else
	{
		printf("\n误码！！！\n");
		ERROR_FLAG = true;
		return NULL;
	}
	//提取序列号
	printf("\n--------------提取序列号--------------\n");
	int serial_number = de_serial_number(frame);
	if (!ERROR_FLAG)
	{
		if (serial_number != (Frame_No % 16 + 1) && serial_number + 15 != (Frame_No % 16))
		{
			ERROR_FLAG = false;
		}

		//ERROR_FLAG = false;
		if (!ERROR_FLAG)
		{
			ii = 1;
			printf("\n成功提取序列号：%d\n", serial_number);
		}
		else
			return NULL;
	}
	//提取比特流
	if (!ERROR_FLAG)
	{
		printf("\n-----------提取比特流--------------\n");

		bits = de_frame(frame);
		if (bits)
		{
			ii = 1;
			printf("\n成功提取比特流：%s\n", bits);
		}
		else
		{
			//printf("\n误码！！！\n");
			ERROR_FLAG = true;
			return NULL;
		}
	}
	//提取校验码
	if (!ERROR_FLAG)
	{
		printf("\n------------差错检测---------------\n");

		crc = de_crc(frame);
		if (crc)
		{
			ii = 1;
			printf("\n成功提取校验码：");
			printf("%s\n", crc);
		}
		else
		{
			printf("\n误码！！！\n");
			ERROR_FLAG = true;
			return NULL;
		}
	}
	//计算检验码
	if (!ERROR_FLAG)
	{

		re_crc = get_crc(bits);
		if (re_crc)
		{
			ii = 1;
			printf("\nbits：%s\n", bits);
			printf("\n重算校验码：%s\n", re_crc);
		}
		else
		{
			//printf("\n误码！！！\n");
			ERROR_FLAG = true;
			return NULL;
		}
	}
	//比较
	if (!ERROR_FLAG)
	{
		if (strcmp(crc, re_crc) != 0)
		{
			printf("\n---%s\n%s---\n", crc, re_crc);
			printf("\nre_crc误码！！！\n");
			ERROR_FLAG = true;
			return NULL;
		}
	}

	/*Frame_No = Frame_No % 16;*/
	return bits;
}
char* new_de_frame(char* s)
{
	char* p = NULL;
	p = (char*)malloc(8 * sizeof(char));
	int len = strlen(s);
	int i;
	if (len == 32)
	{
		for (i = 0; i < 8; i++)
			p[i] = s[i + 8];
		p[i] = '\0';
	}
	else
	{
		for (i = 0; i < 8; i++)
		{
			if (define_fiveone(s, i + 8))
			{
				p[i] = s[i + 8]; i++; p[i] = s[i + 8]; i++; p[i] = s[i + 8]; i++; p[i] = s[i + 8]; i++; p[i] = s[i + 8]; i++; p[i] = '1';

				/*p[i] = s[i + 8]; i++; p[i] = s[i + 8]; i++; p[i] = s[i + 8]; i++; p[i] = s[i + 8]; i++; p[i] = s[i + 8]; i++;
				i++;*/
			}
			else
				p[i] = s[i + 8];
		}
		p[i] = '\0';
	}
	return p;
}
char* new_locate_frame(char* s)
{
	int start = 0; int end = 0;
	int i, j;
	char* p = NULL;

	for (j = 0; j < strlen(s); j++)
	{
		for (i = j; i < strlen(s) - 8; i++)
		{
			if (define_frame(s, i))
			{
				start = i;
				break;
			}
		}
		for (i = strlen(s) - 8; i >= j; i--)
		{
			if (define_frame(s, i))
			{
				end = i + 7;
				break;
			}
		}
		if ((end - start + 1) >= 32 && (end - start + 1) <= 34)
		{
			p = (char*)malloc((end - start + 1) * sizeof(char));
			int k, r;
			for (k = start, r = 0; k <= end; k++, r++)
				p[r] = s[k];
			p[r] = '\0';
			return p;
		}
		else
			continue;
	}
	return p;

}
char* encapsulate_frame(char* revData)
{

	//成帧
	//printf("\n----------------成帧----------------\n");
	char* revData_t = NULL;
	revData_t = (char*)malloc(8 * sizeof(char));
	int ii;
	for (ii = 0; ii < 8; ii++)
	{
		revData_t[ii] = revData[ii];
	}
	revData_t[ii] = '\0';
	char* p;
	p = (char*)malloc(100 * sizeof(char));
	p = get_frame(revData);
	//printf("\n成帧成功：%s\n", p);

	//差错控制
	//printf("\n--------------差错控制--------------\n");
	char* crc = get_crc(revData_t);
	printf("\n%s计算得校验码：%s\n", revData_t, crc);
	char* q;
	q = (char*)malloc(100 * sizeof(char));
	q = add_crc(p, crc);

	//加序列号
	//printf("\n--------------加序列号--------------\n");
	int serial_number = serial_num % 16;
	char* s_n = ten2two(serial_number);
	q = add_serial_number(q, s_n);
	//printf("\n成功添加序列号：%s\n", q);
	//serial_num++;
	//printf("\n--------------加目的设备号--------------\n");
	q = add_code_node(q);
	//printf("\n成功添加序列号：%s\n", q);
	//printf("\n--------------加源设备号--------------\n");
	q = add_code_source_node(q);
	//printf("\n--------------加重传标志--------------\n");
	q = add_dirty(q);
	//over
	return q;
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
char* add_code_node(char* p)
{
	char* D_n = ten2two(Des_Device_number);
	int n = strlen(p);
	int i;
	char* q;
	q = (char*)malloc(100 * sizeof(char));
	for (i = 0; i < 8; i++)
		q[i] = p[i];
	int j;
	int k = 0;
	for (j = i; j < i + 4; j++, k++)
		q[j] = D_n[k];
	int u = j;
	for (j = u; j < n + 4; j++, i++)
		q[j] = p[i];
	q[j] = '\0';
	return q;
}
int get_decode_node(char* s)
{
	int sn;
	char* p = NULL;
	p = (char*)malloc(4 * sizeof(char));
	int len = strlen(s);
	int i;
	for (i = 0; i < 4; i++)
	{
		p[i] = s[i + 13];
	}
	p[i] = '\0';
	sn = two2ten(p);
	return sn;
}

char* add_code_source_node(char* p)
{
	char* D_n = ten2two(Source_Device_number);
	int n = strlen(p);
	int i;
	char* q;
	q = (char*)malloc(100 * sizeof(char));
	for (i = 0; i < 8; i++)
		q[i] = p[i];
	int j;
	int k = 0;
	for (j = i; j < i + 4; j++, k++)
		q[j] = D_n[k];
	int u = j;
	for (j = u; j < n + 4; j++, i++)
		q[j] = p[i];
	q[j] = '\0';
	return q;
}
int get_decode_source_node(char* s)
{
	int sn;
	char* p = NULL;
	p = (char*)malloc(4 * sizeof(char));
	int len = strlen(s);
	int i;
	for (i = 0; i < 4; i++)
	{
		p[i] = s[i + 9];
	}
	p[i] = '\0';
	sn = two2ten(p);
	return sn;
}

char* add_dirty(char* p)
{
	int n = strlen(p);

	char ch;
	if (Retrans_Flag)
		ch = '1';
	else
		ch = '0';
	int i;
	char* q;
	q = (char*)malloc(100 * sizeof(char));
	for (i = 0; i < 8; i++)
		q[i] = p[i];
	int j;
	int k = 0;
	for (j = i; j < i + 1; j++, k++)
		q[j] = ch;
	int u = j;
	for (j = u; j < n + 1; j++, i++)
		q[j] = p[i];
	q[j] = '\0';
	return q;
}
bool get_dirty(char* p)
{
	char ch = p[8];
	if (ch == '1')
		return true;
	else
		return false;
}
char* get_retrans_frame(char* p)
{
	int n = strlen(p);
	char* q = NULL;
	q = (char*)malloc(n * sizeof(char));
	int i, j;
	for (i = 0; i < 8; i++)
		q[i] = p[i];
	q[i] = '1';
	for (j = 9; j < 13; j++)
		q[j] = p[j + 4];
	for (j = 13; j < 17; j++)
		q[j] = p[j - 4];
	for (j = 17; j < 21; j++)
		q[j] = '0';
	for (j = 21; j < 26; j++)
		q[j] = '1';
	q[j] = '0';
	for (j = 27; j < 30; j++)
		q[j] = '1';
	for (j = 30; j < 38; j++)
		q[j] = '0';
	q[j] = '0';
	for (j = 39; j < 45; j++)
		q[j] = '1';
	q[j] = '0';
	j++;
	q[j] = '\0';

	return q;
}
void print_switch_table()
{
	printf("\n");

	printf("------------MAC Table-----------\n");
	for (int i = 0; i < st_size; i++)
	{
		printf("addr: %d   port: %d\n", st[i].addr, st[i].port);
	}
	printf("--------------------------------\n");
}

int find_st_port(int addr)
{
	int i;
	for (i = 0; i < st_size; i++)
	{
		if (st[i].addr == addr)
			return st[i].port;
	}
	return -1;
}

void reverse_addr_learn(int addr, int port)
{
	if ((find_st_port(addr) >= 0) || addr == 15)
	{
		return;
	}
	st[st_size].port = port;
	st[st_size].addr = addr;
	st_size++;
	printf("\n更新MAC表.......\n");
	print_switch_table();
}
char* get_broadcast_frame(char* p)
{
	char* q = NULL;
	q = (char*)malloc(100 * sizeof(char));
	int i;
	int n = strlen(p);
	for (i = 0; i < 13; i++)
		q[i] = p[i];
	for (i = 13; i < 17; i++)
		q[i] = '1';
	for (i = 17; i < n; i++)
		q[i] = p[i];
	q[i] = '\0';
	return q;
}
void print_router_table()
{
	printf("\n");
	printf("-------------------Routing Table------------------\n");
	for (int i = 0; i < rt_size; i++)
	{
		printf("Destination: %d   Gataway: %d   Port: %d   Metric:%d\n", rt[i].destination, rt[i].gataway, rt[i].port, rt[i].metric);
	}
	printf("-------------------------------------------------\n");
}
int find_rt_port(int destination)
{
	int i;
	for (i = 0; i < rt_size; i++)
	{
		if (rt[i].destination == destination)
			return rt[i].port;
	}
	return -1;
}
