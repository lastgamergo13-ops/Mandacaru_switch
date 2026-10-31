#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "opus.h"
#include "opus_multistream.h"
int main(void){
  int rate=48000, ch=6, err, streams, coupled; unsigned char map[256];
  OpusMSEncoder *enc = opus_multistream_surround_encoder_create(rate, ch, 1, &streams, &coupled, map, OPUS_APPLICATION_AUDIO, &err);
  if(err!=OPUS_OK){printf("enc fail %d\n",err);return 1;}
  printf("streams=%d coupled=%d\n",streams,coupled);
  int frame=960; short *pcm=malloc(frame*ch*2);
  for(int i=0;i<frame;i++)for(int c=0;c<ch;c++)pcm[i*ch+c]=(short)(8000*sin(i*0.05*(c+1)));
  unsigned char pkt[4000]; 
  /* the same sequence the service uses */
  int need = opus_multistream_decoder_get_size(streams, coupled);
  printf("get_size=%d\n", need);
  void *work = aligned_alloc(4096, 65536 > need ? 65536 : need); memset(work,0xAB,need);
  OpusMSDecoder *dec = (OpusMSDecoder*)work;
  int r = opus_multistream_decoder_init(dec, rate, ch, streams, coupled, map);
  printf("init=%d\n", r);
  OpusMSDecoder *ref = opus_multistream_decoder_create(rate, ch, streams, coupled, map, &err);
  short *o1=malloc(5760*ch*2), *o2=malloc(5760*ch*2); int bad=0;
  for(int f=0; f<20; f++){
    int n = opus_multistream_encode(enc, pcm, frame, pkt, sizeof pkt);
    if(n<0){printf("encode %d\n",n);return 1;}
    int a = opus_multistream_decode(dec, pkt, n, o1, 5760, 0); /* big frame_size like the service passes */
    int b = opus_multistream_decode(ref, pkt, n, o2, 5760, 0);
    if(a!=frame||b!=frame||memcmp(o1,o2,frame*ch*2)) bad++;
    if(f==9){ opus_multistream_decoder_ctl(dec, OPUS_RESET_STATE); opus_multistream_decoder_ctl(ref, OPUS_RESET_STATE);} 
  }
  printf("frames decoded ok, mismatches=%d\n", bad);
  printf("invalid layout size=%d (0 means rejected)\n", opus_multistream_decoder_get_size(3,4));
  return bad!=0;
}
