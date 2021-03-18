
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include <jsni.h>
#include <memory.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include "esUtil.h"
#include "matrix.h"
#include "cameraShader.h"
#include "CameraOpenGLES.h"


//#ifdef __cplusplus
//    extern "C" {
//#endif

#define UNIT 1
#define TEXTURE_COOR_UNIT 1

//顶点着色器脚本代码
const char * codeVertexShader = \
"attribute vec3 aPosition;							\n" \
"uniform mat4 uMVPMatrix;	 						\n" \
"attribute vec2 aTexCoor; 							\n" \
"varying vec2 vTexCoor;		 						\n" \
"void main() 										\n" \
"{ 													\n" \
"	gl_Position = uMVPMatrix * vec4(aPosition, 1); 	\n" \
" 	vTexCoor = aTexCoor;							\n" \
"} 													\n" \
;

//-------------MATH---------------
const char * codeFragShader = \
"precision mediump float;											\n" \
"uniform sampler2D yTexture; 										\n" \
"uniform sampler2D uTexture; 										\n" \
"uniform sampler2D vTexture; 										\n" \
"varying vec2 vTexCoor;												\n" \
"void main()														\n" \
"{																	\n" \
"	float y = texture2D(yTexture, vTexCoor).r;						\n" \
"	float u = texture2D(uTexture, vTexCoor).r;											\n" \
"	float v = texture2D(vTexture, vTexCoor).r;													\n" \
"	vec3 yuv = vec3(y, u, v);												\n" \
"	vec3 offset = vec3(16.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0);								\n" \
"	mat3 mtr = mat3(1.0, 1.0, 1.0, -0.001, -0.3441, 1.772, 1.402, -0.7141, 0.001);						\n" \
"	vec4 curColor = vec4(mtr * (yuv - offset), 1);												\n" \
"	gl_FragColor = curColor;													\n" \
"}																	\n" \
;






//渲染顶点坐标数据
const float dataVertex[] = 
{
	-1 * UNIT, 1 * UNIT, 0,
	-1 * UNIT, -1 * UNIT, 0,
	1 * UNIT, 1 * UNIT, 0,
	1 * UNIT, -1 * UNIT, 0
};
//渲染纹理坐标数据
const float dataTexCoor[] =
{
	0 * TEXTURE_COOR_UNIT, 0 * TEXTURE_COOR_UNIT,
	0 * TEXTURE_COOR_UNIT, 1 * TEXTURE_COOR_UNIT,
	1 * TEXTURE_COOR_UNIT, 0 * TEXTURE_COOR_UNIT,
	1 * TEXTURE_COOR_UNIT, 1 * TEXTURE_COOR_UNIT
};

void printData(void* data, const int size, const char * name);

//全局变量
Instance *instance;

//函数返回fname指定文件的全部内容，如果打不开文件，则返回NULL，并显示打开文件错误 
char *getfileall(char *fname)
{
    FILE *fp;
    char *str;
    char txt[1000];
    int filesize;
    //打开一个文件
    if ((fp=fopen(fname,"r"))==NULL){
        printf("打开文件%s错误\n",fname);
        return NULL;
    }
    //将文件指针移到末尾
    fseek(fp,0,SEEK_END);
    filesize = ftell(fp);//通过ftell函数获得指针到文件头的偏移字节数。
    
    str=(char *)malloc(filesize);//动态分配str内存
//    str=malloc(filesize);//动态分配str内存
    str[0]=0;//字符串置空
//    memset(str,filesize*sizeof(char),0);//清空数组,字符串置空第二种用法
    rewind(fp);
    
    while((fgets(txt,1000,fp))!=NULL){//循环读取1000字节,如果没有数据则退出循环
        strcat(str,txt);//拼接字符串
    }
    fclose(fp);
    return str;
}

void init(int pWidth, int pHeight)
{
	LOGI_EU("init()");
	instance = (Instance *)malloc(sizeof(Instance));
	memset(instance, 0, sizeof(Instance));
	//	1.初始化着色器
	GLuint shaders[2] = {0};
	shaders[0] = initShader(codeVertexShader, GL_VERTEX_SHADER);
	shaders[1] = initShader(codeFragShader, GL_FRAGMENT_SHADER);
	instance->pProgram = initProgram(shaders, 2);
	instance->maMVPMatrixHandle = glGetUniformLocation( instance->pProgram, "uMVPMatrix");
	instance->maPositionHandle = glGetAttribLocation(instance->pProgram, "aPosition");
	instance->maTexCoorHandle = glGetAttribLocation(instance->pProgram, "aTexCoor");
	instance->myTextureHandle = glGetUniformLocation(instance->pProgram, "yTexture");
	instance->muTextureHandle = glGetUniformLocation(instance->pProgram, "uTexture");
	instance->mvTextureHandle = glGetUniformLocation(instance->pProgram, "vTexture");
	//	2.初始化纹理
	//		2.1生成纹理id
	glGenTextures(1, &instance->yTexture);
	glGenTextures(1, &instance->uTexture);
	glGenTextures(1, &instance->vTexture);
	LOGI_EU("init() yT = %d, uT = %d, vT = %d.", instance->yTexture, instance->uTexture, instance->vTexture);
	LOGI_EU("%s %d error = %d", __FILE__,__LINE__, glGetError());
	//	3.分配Yuv数据内存
	instance->yBufferSize = sizeof(char) * pWidth * pHeight;
	instance->uBufferSize = sizeof(char) * pWidth / 2 * pHeight / 2;
	instance->vBufferSize = sizeof(char) * pWidth / 2 * pHeight / 2;
	instance->yBuffer = (signed char *)malloc(instance->yBufferSize);
	instance->uBuffer = (signed char *)malloc(instance->uBufferSize);
	instance->vBuffer = (signed char *)malloc(instance->vBufferSize);
	memset(instance->yBuffer, 0, instance->yBufferSize);
	memset(instance->uBuffer, 0, instance->uBufferSize);
	memset(instance->vBuffer, 0, instance->vBufferSize);
	instance->pHeight = pHeight;
	instance->pWidth = pWidth;
	LOGI_EU("width = %d, height = %d", instance->pWidth, instance->pHeight);
	//清理背景
	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	//允许深度检测
//	glEnable(GL_DEPTH_TEST);
	LOGI_EU("%s %d error = %d", __FILE__,__LINE__, glGetError());
}


void changeLayout(int width, int height)
{

	LOGI_EU("changeLayout() width = %d, height = %d", width, height);
	if(instance != 0)
	  {
	    instance->vWidth = width;
	    instance->vHeight = height;
	  }
	unsigned int angle;
	unsigned int eW, eH;
	float vRadio, radio;
	angle = 270;
	vRadio = (float)height / width;
	if (angle == 90 || angle == 270)
	  {
	    radio = ((float)instance->pWidth / instance->pHeight);
	  }
	else
	  {
	    radio = ((float)instance->pHeight / instance->pWidth);
	  }
	if(vRadio < radio)
	  {
	    eH = instance->vHeight;
	    eW = (unsigned int)(eH / radio);
	  }
	else
	  {
	    eW = instance->vWidth;
	    eH = (unsigned int)(eW * radio);
	  }
	LOGI_EU("changeLayout() eW = %d, eH = %d, radio = %f, vRadio = %f, instance->pHeight = %d, instance->pWidth = %d",
				eW, eH, radio, vRadio, instance->pHeight, instance->pWidth);
	glViewport(0, 0, eW, eH);
}

void drawframe()
{
	char* srcp = getfileall("nemausa/yufile1.yuv");
	//char * srcp = (char*)(JSNIGetArgOfCallback(env, info, 0));
	//__uint32_t size = (__uint32_t)(JSNIToCDouble(env, JSNIGetArgOfCallback(env, info, 1)));
	int size = 1280*720*3/2;

//	printData(srcp, 20, "yuvDatas y10 : ");
//	printData(srcp + ((size * 2) / 3), 80, "yuvDatas vu20 : ");
	memcpy(instance->yBuffer, srcp, instance->yBufferSize);
//	printData(instance->yBuffer, 20, "instance->yBuffer y10 : ");
	int i,j;
	j = 0;
	for(i = instance->yBufferSize; i < size; i+=2)
	  {
	    instance->vBuffer[j] = srcp[i];
	    instance->uBuffer[j] = srcp[i + 1];
	    ++j;
	  }
	//	LOGI_EU("vBuffer size = %d", j);
	//(*env)->ReleaseByteArrayElements(env, yuvDatas, srcp, JNI_ABORT);
	drawFrame(instance);
}


void release()
{
	LOGI_EU("release()");
	if(instance != 0)
		{
			free(instance->yBuffer);
			free(instance->uBuffer);
			free(instance->vBuffer);
			instance->yBuffer = 0;
			free(instance);
			instance = 0;
		}
}

void printData(void* data, const int size, const char * name)
{
	char * dt = (char *)data;
	int i,j;
	char * str = (char *) malloc(size * sizeof(char) * 2);
	memset(str, '\0', size * sizeof(char) * 2);
	for (i = 0; i < size; i++)
		{
			sprintf(str + 2 * i,"%02x", dt[i]);
		}
	LOGI_EU("%s , %s", name, str);
	free(str);
}




