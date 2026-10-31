#pragma once
namespace workercounttest {
inline unsigned Run(){
    unsigned checks=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);checks++;};
    {
        launchvalidationtest::Fixture f;
        bool queued=false;
        for(const unsigned workers:{8u,1u,3u,8u}){
            f.workers=workers;inputfasttest::Put(f.engine,0x68D14,workers);
            for(unsigned i=0;i<workers;i++)f.Launch(i);
            check(f.Adapter().Live(),"8-to-1 and later worker-count changes allowed only between complete cycles");
            if(queued){
                check(!f.Adapter().View().applied.IsZero()&&f.Input(1,true)>0&&f.Input(3,true)>0&&f.Input(2,true)>0,
                    "nonzero private input keeps compute-dispatch coverage across worker-count changes");
            }
            f.Finish();check(f.Adapter().Live()&&f.Adapter().Fixed().workers==workers,"current complete batch retains its own worker count");
            if(!queued){f.Request(.6f,1);f.Queue();inputfasttest::Need(f.Adapter().Completed(20,100));queued=true;}
        }
    }
    for(unsigned phase=0;phase<3;++phase){
        launchvalidationtest::Fixture f;f.Launch(0);f.Launch(1);f.Launch(2);
        const facecontinuous::Frame changed{inputfasttest::Address(f.engine),inputfasttest::Address(f.renderer),0x30000,f.counter,f.counter,1,0,GetCurrentThreadId()};
        bool accepted=false;
        if(phase==0)accepted=f.Adapter().Launch(changed,0,0x70000);
        if(phase==1)accepted=f.Adapter().Wait(changed,0,0x80000,0);
        if(phase==2)accepted=f.Adapter().Boundary(changed);
        check(!accepted&&!f.Adapter().Live(),"worker count cannot change during an open launch/wait/join cycle");
    }
    for(const auto counts:{std::pair{8u,1u},std::pair{1u,8u},std::pair{3u,1u}}){
        facecontinuous::Frame old{0x10000,0x20000,0x30000,100,100,counts.first,0,1};
        facecontinuous::DrainBoundary drain;check(drain.Start(old,100,12),"retirement starts from former worker count");
        auto batch=old;batch.workers=counts.second;
        for(unsigned i=0;i<batch.workers;i++)check(drain.Launch(batch,i,0x40000+i*256),"retirement new batch uses current count");
        for(unsigned i=0;i<batch.workers;i++)check(drain.Wait(batch,i,0x50000+i*256,0),"retirement waits for complete current batch");
        check(drain.Join(batch)&&drain.Signal(0x20000,0x30000,101,101,101,true)&&!drain.Ready(),"retirement still requires successor join");
        batch.before=batch.after=101;batch.workers=counts.first;
        for(unsigned i=0;i<batch.workers;i++)check(drain.Launch(batch,i,0x40000+i*256),"successor can have a different worker count");
        for(unsigned i=0;i<batch.workers;i++)check(drain.Wait(batch,i,0x50000+i*256,0),"all successor workers must be waited");
        check(drain.Join(batch)&&!drain.Ready(),"successor join alone cannot authorize GPU reuse");
        check(drain.Completed(12,101)&&drain.Ready(),"reuse requires CPU proof and both GPU completions after count changes");
    }
    for(unsigned phase=0;phase<3;++phase){
        const facecontinuous::Frame old{0x10000,0x20000,0x30000,100,100,3,0,1};
        facecontinuous::DrainBoundary drain;check(drain.Start(old,100,12)&&drain.Launch(old,0,0x40000),"invalid retirement fixture starts");
        auto changed=old;changed.workers=1;bool accepted=false;
        if(phase==0)accepted=drain.Launch(changed,0,0x40000);
        if(phase==1)accepted=drain.Wait(changed,0,0x50000,0);
        if(phase==2)accepted=drain.Join(changed);
        check(!accepted&&drain.Failed(),"retirement rejects in-batch count changes and incomplete coverage");
    }
    return checks;
}
}
