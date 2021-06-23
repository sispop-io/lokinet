/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class network_loki_lokinet_LokinetDaemon */

#ifndef _Included_network_loki_lokinet_LokinetDaemon
#define _Included_network_loki_lokinet_LokinetDaemon
#ifdef __cplusplus
extern "C"
{
#endif
  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    Obtain
   * Signature: ()Ljava/nio/ByteBuffer;
   */
  JNIEXPORT jobject JNICALL
  Java_network_loki_lokinet_LokinetDaemon_Obtain(JNIEnv*, jclass);

  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    Free
   * Signature: (Ljava/nio/ByteBuffer;)V
   */
  JNIEXPORT void JNICALL
  Java_network_loki_lokinet_LokinetDaemon_Free(JNIEnv*, jclass, jobject);

  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    Configure
   * Signature: (Lnetwork/loki/lokinet/LokinetConfig;)Z
   */
  JNIEXPORT jboolean JNICALL
  Java_network_loki_lokinet_LokinetDaemon_Configure(JNIEnv*, jobject, jobject);

  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    Mainloop
   * Signature: ()I
   */
  JNIEXPORT jint JNICALL
  Java_network_loki_lokinet_LokinetDaemon_Mainloop(JNIEnv*, jobject);

  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    IsRunning
   * Signature: ()Z
   */
  JNIEXPORT jboolean JNICALL
  Java_network_loki_lokinet_LokinetDaemon_IsRunning(JNIEnv*, jobject);

  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    Stop
   * Signature: ()Z
   */
  JNIEXPORT jboolean JNICALL
  Java_network_loki_lokinet_LokinetDaemon_Stop(JNIEnv*, jobject);

  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    InjectVPNFD
   * Signature: ()V
   */
  JNIEXPORT void JNICALL
  Java_network_loki_lokinet_LokinetDaemon_InjectVPNFD(JNIEnv*, jobject);

  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    GetUDPSocket
   * Signature: ()I
   */
  JNIEXPORT jint JNICALL
  Java_network_loki_lokinet_LokinetDaemon_GetUDPSocket(JNIEnv*, jobject);

  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    DetectFreeRange
   * Signature: ()Ljava/lang/String;
   */
  JNIEXPORT jstring JNICALL
  Java_network_loki_lokinet_LokinetDaemon_DetectFreeRange(JNIEnv*, jclass);

  /*
   * Class:     network_loki_lokinet_LokinetDaemon
   * Method:    DumpStatus
   * Signature: ()Ljava/lang/String;
   */
  JNIEXPORT jstring JNICALL
  Java_network_loki_lokinet_LokinetDaemon_DumpStatus(JNIEnv*, jobject);

#ifdef __cplusplus
}
#endif
#endif
