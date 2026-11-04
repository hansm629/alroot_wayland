package com.alroot.timing;
import android.os.Build;
import android.view.Choreographer;

/** UI looper only. At most one posted callback. The native return value is
 * the requested source for the NEXT vsync; OFF uses the same one legacy
 * callback + one JNI invocation as the original Activity. No Timer or NDK clock. */
public final class SingleVsyncPump {
 public interface Sink {
  boolean raw(long frame);
  boolean timeline(long frame,long id,long deadline,long target);
 }
 private final Sink sink;
 private Choreographer choreographer;
 private boolean enabled,posted;
 private int epoch;
 private Api33 modern;
 public SingleVsyncPump(Sink s){sink=s;}
 private final Choreographer.FrameCallback legacy=this::rawFrame;
 private void rawFrame(long n){
  if(!enabled || !posted)return;posted=false;int generation=epoch;
  boolean wanted=sink.raw(n);
  if(enabled && generation==epoch)next(wanted);
 }
 private void next(boolean useTimeline) {
  if(posted)return;posted=true;
  if(useTimeline && Build.VERSION.SDK_INT>=33){
   if(modern==null)modern=new Api33(this);modern.post(choreographer);return;
  }
  choreographer.postFrameCallback(legacy);
 }
 public void start(){if(enabled)return;enabled=true;epoch++;choreographer=Choreographer.getInstance();next(false);}
 public void stop(){if(!enabled)return;enabled=false;++epoch;posted=false;
  choreographer.removeFrameCallback(legacy);
  if(Build.VERSION.SDK_INT>=33 && modern!=null)modern.remove(choreographer);
 }
 @android.annotation.TargetApi(33) private static final class Api33 implements Choreographer.VsyncCallback {
  private final SingleVsyncPump p;Api33(SingleVsyncPump p){this.p=p;}
  void post(Choreographer c){c.postVsyncCallback(this);}
  void remove(Choreographer c){c.removeVsyncCallback(this);}
  @Override public void onVsync(Choreographer.FrameData d){
   if(!p.enabled || !p.posted)return;p.posted=false;int generation=p.epoch;
   long frame=d.getFrameTimeNanos();Choreographer.FrameTimeline t=d.getPreferredFrameTimeline();
   boolean next=t==null?p.sink.timeline(frame,-1,0,0):p.sink.timeline(frame,t.getVsyncId(),t.getDeadlineNanos(),t.getExpectedPresentationTimeNanos());
   if(p.enabled && generation==p.epoch)p.next(next);
  }
 }
}
