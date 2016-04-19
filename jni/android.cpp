/*
    Copyright (C) 2013 Stephane Poinsart <s@poinsart.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * VotAR.cpp : native image parsing functions
 */


/*
 * jni bitmap handling was started from "ivo" very useful post helpful post :
 * http://stackoverflow.com/questions/2881939/android-read-png-image-without-alpha-and-decode-as-argb-8888
 */

#include <jni.h>
#include <string.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <math.h>
#include <utility>
#include <endian.h>
#include <include/android.hpp>
#include <stdlib.h>
#include <time.h>

#include "count-simple.hpp"
#include "android.hpp"
#include "common.hpp"

#define  LOG_TAG    "nativeAnalyze"

extern "C" {


JNIEnv *globalenv;
jobject globaltask;
jmethodID publishMethod;
jobjectArray progressArray;

int prcount[4]={0,0,0,0};

float startTime;


void benchmarkStart() {
	startTime = (float)clock()/CLOCKS_PER_SEC;
	Log_i("Benchmark: 0.000 | Starting");
}
void benchmarkElapsed(const char *text) {
	float endTime = (float)clock()/CLOCKS_PER_SEC;

	float timeElapsed = endTime - startTime;
	Log_i("Benchmark: %8f | %s", timeElapsed, text);
}



// this function is a modified version of the BSD "toInt" in decaf project
// from Sattvik Software & Technology Resources, Ltd. Co.
// https://github.com/sattvik/decafbot/blob/master/ndk/jni/decafbot.c
jobject javaInteger(JNIEnv* env, jint value) {
	jclass integerClass;
	jmethodID valueOfMethod;

	/* get class for Integer */
	integerClass = env->FindClass("java/lang/Integer");
	if (integerClass == NULL) {
		Log_e("Failed to find class for Integer");
		return NULL;
	}

	/* get Integer.valueOf(int) method */
	valueOfMethod = env->GetStaticMethodID(integerClass, "valueOf", "(I)Ljava/lang/Integer;");
	if (valueOfMethod == NULL) {
		Log_e("Failed to find static method Integer.valueOf(int)");
		return NULL;
	}

	/* do the conversion */
	return env->CallStaticObjectMethod(integerClass, valueOfMethod, value);
}

jobjectArray globalJmarkArray=NULL;

JNIEXPORT void JNICALL Java_com_poinsart_votar_VotarMain_00024AnalyzeTask_free(JNIEnv *env) {
	if (globalJmarkArray) {
		env->DeleteGlobalRef(globalJmarkArray);
		globalJmarkArray=NULL;
	}
}

void publish_progress(int step) {
	jobject progress=javaInteger(globalenv,step);
	globalenv->SetObjectArrayElement(progressArray, 0, progress);
	globalenv->CallVoidMethod(globaltask, publishMethod, progressArray);
}

/*
 * jni bitmap handling was started from "ivo" very useful post helpful post :
 * http://stackoverflow.com/questions/2881939/android-read-png-image-without-alpha-and-decode-as-argb-8888
 */


JNIEXPORT jboolean JNICALL Java_com_poinsart_votar_VotarMain_00024AnalyzeTask_nativeAnalyze(JNIEnv *env, jobject task, jobject ar)
{
	AndroidBitmapInfo info;
	unsigned int *pixels;
	unsigned int width, height, pixelcount;
	int mark[MAX_MARK_COUNT][3];
	int markcount;
	jboolean isCopy=0;

	globalenv=env;
	globaltask=task;

	Java_com_poinsart_votar_VotarMain_00024AnalyzeTask_free(env);

	Log_i("Now in nativeAnalyze code");
	benchmarkStart();

	jclass taskClass = env->GetObjectClass(task);
	if(taskClass==NULL) {
		Log_e("Internal Error: failed to find class for object task");
		return false;
	}
	jclass arClass = env->GetObjectClass(ar);
	if(arClass==NULL) {
		Log_e("Internal Error: failed to find class for object ar");
		return false;
	}


	publishMethod = env->GetMethodID(taskClass, "publishProgress", "([Ljava/lang/Object;)V");
	if(publishMethod==NULL) {
		Log_e("Internal Error: failed to find java method publishProgress ([Ljava/lang/Object;)V");
		return false;
	}



	jclass jobjectArrayClass = env->FindClass("[Ljava/lang/Object;");
	if (jobjectArrayClass == NULL) {
		Log_e("Failed to find class for Object[]");
		return false;
	}
	jclass jIntegerClass = env->FindClass("java/lang/Integer");
	if (jIntegerClass == NULL) {
		Log_e("Failed to find class for Integer");
		return false;
	}

	jclass jmarkClass=env->FindClass("com/poinsart/votar/Mark");
	if (jmarkClass==NULL) {
		Log_e("Internal Error: failed to find java class com/poinsart/votar/Mark");
		return false;
	}

	progressArray = env->NewObjectArray(1, jIntegerClass, NULL);
	if (progressArray == NULL) {
		Log_e("Failed to allocate object array for published progress.");
		return false;
	}

	// get fields of the AnalyzeReturn class, into appropriate C types

	// photo
	jfieldID photoField = env->GetFieldID(arClass, "photo", "Landroid/graphics/Bitmap;");
	if (photoField == NULL) {
		Log_e("Failed to find field photo.");
		return false;
	}
	jobject photo=env->GetObjectField(ar, photoField);
	if (photo == NULL) {
		Log_e("Failed to read field photo.");
		return false;
	}

	// prcount[]
	jfieldID prcountField = env->GetFieldID(arClass, "prcount", "[I");
	if (prcountField == NULL) {
		Log_e("Failed to find field prcount.");
		return false;
	}
	jintArray jprcount=(jintArray)env->GetObjectField(ar, prcountField);
	if (jprcount == NULL) {
		Log_e("Failed to read prcount photo.");
		return false;
	}


	// mark[]
	jfieldID markField = env->GetFieldID(arClass, "mark", "[Lcom/poinsart/votar/Mark;");
	if (markField == NULL) {
		Log_e("Failed to find field mark.");
		return false;
	}


	prcount[0]=prcount[1]=prcount[2]=prcount[3]=0;

	//globalBitmap=bitmap=(jobject)env->NewGlobalRef(bitmap);

	/////////////////////////////
	// initialize pixels array
	if (AndroidBitmap_getInfo(env, photo, &info) < 0) {
		Log_e("Failed to get Bitmap info");
		return false;
	}
	width=info.width;
	height=info.height;
	pixelcount=width*height;
	Log_i("Handling Bitmap in native code... Width: %d, Height: %d", width, height);

	if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
		Log_e("Incompatible Bitmap format");
		return false;
	}

	void** voidpointer=(void**) &pixels;
	if (AndroidBitmap_lockPixels(env, photo, voidpointer) < 0) {
		Log_e("Failed to lock the pixels of the Bitmap");
	}

	publish_progress(1);

	simple_analyze(pixels,width, height, mark, markcount, prcount);


	if(AndroidBitmap_unlockPixels(env, photo) < 0) {
		Log_e("Failed to unlock the pixels of the Bitmap");
		return false;
	}

	/////////////////////////////
	// return prcount[4] to java through jprcount array argument
	jint *eprcount=env->GetIntArrayElements(jprcount, &isCopy);
	if (eprcount==NULL) {
		Log_e("Internal Error: failed on GetIntArrayElements(jprcount, &isCopy) ");
		return false;
	}

	eprcount[0]=prcount[0];
	eprcount[1]=prcount[1];
	eprcount[2]=prcount[2];
	eprcount[3]=prcount[3];
	env->ReleaseIntArrayElements(jprcount, eprcount, JNI_COMMIT);

	jmethodID jmarkConstructor=env->GetMethodID(jmarkClass, "<init>", "(III)V");
	if (jmarkConstructor==NULL) {
		Log_e("Internal Error: failed to find constructor for java class com/poinsart/votar/Mark");
		return false;
	}
	jobjectArray jmarkArray=env->NewObjectArray(markcount, jmarkClass, NULL);
	for (int i=0; i<markcount; i++) {
		jobject jmarkCurrent=env->NewObject(jmarkClass, jmarkConstructor, mark[i][0], mark[i][1], mark[i][2]);
		if (jmarkCurrent==NULL) {
			Log_e("Internal Error: failed to create jmark object (out of memory ?)");
			return false;
		}
		env->SetObjectArrayElement(jmarkArray, i, jmarkCurrent);
	}
	globalJmarkArray=(jobjectArray) env->NewGlobalRef(jmarkArray);
	env->SetObjectField(ar, markField,globalJmarkArray);
	return true;
}
}
