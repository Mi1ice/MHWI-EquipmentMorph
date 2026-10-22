#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod protocol;
use protocol::{Target,RESPONSE_BYTES};
use serde::Serialize;
use std::{collections::VecDeque,ffi::c_void,sync::{Arc,Mutex,atomic::{AtomicBool,AtomicU32,Ordering}},time::{Duration,Instant}};
use tauri::{Manager,WindowEvent};
use tauri_plugin_global_shortcut::{Code,GlobalShortcutExt,Modifiers,Shortcut,ShortcutState};

unsafe extern "C" {
    fn mhm_find_game()->u32;
    fn mhm_open_game(pid:u32)->*mut c_void;
    fn mhm_game_alive(client:*mut c_void)->i32;
    fn mhm_disconnect(client:*mut c_void);
    fn mhm_close_game(client:*mut c_void);
    fn mhm_exchange(client:*mut c_void,input:*const u8,input_bytes:usize,output:*mut u8,output_bytes:usize)->i32;
    fn mhm_foreground_pid()->u32;
    fn mhm_focus_game(pid:u32);
}
#[derive(Clone,Serialize)]
#[serde(rename_all="camelCase")]
struct View {
    connected:bool,pid:u32,generation:String,source:String,targets:Vec<Target>,
    error:Option<String>,shortcut_error:Option<String>,
}
struct Pending{action:u32,target:u64,key:u32,weight:f32}
struct Inner{view:View,queue:VecDeque<Pending>,epoch:u64,last_ui:Instant}
struct State{inner:Mutex<Inner>,auto_pid:AtomicU32,visible:AtomicBool}
impl State{
    fn new(pid:u32)->Self{Self{inner:Mutex::new(Inner{view:View{connected:false,pid:0,generation:"0".into(),source:"0".into(),
        targets:vec![],error:None,shortcut_error:None},queue:VecDeque::new(),epoch:0,last_ui:Instant::now()}),auto_pid:AtomicU32::new(pid),visible:AtomicBool::new(false)}}
    fn release(&self){let mut state=self.inner.lock().unwrap();state.queue.clear();state.queue.push_back(Pending{action:3,target:0,key:0,weight:0.});}
    fn disconnected(&self){
        let mut s=self.inner.lock().unwrap();s.queue.clear();s.epoch+=1;s.view.generation=s.epoch.to_string();
        s.view.connected=false;s.view.source="0".into();s.view.targets.clear();
    }
}
#[tauri::command]
fn snapshot(state:tauri::State<'_,Arc<State>>)->View{
    let mut inner=state.inner.lock().unwrap();inner.last_ui=Instant::now();inner.view.clone()
}
#[tauri::command]
fn change(state:tauri::State<'_,Arc<State>>,action:String,target:String,key:u32,weight:f64,generation:String)->Result<(),String>{
    let action=match action.as_str(){"set"=>1,"drop"=>2,"release"=>3,_=>return Err("未知控制操作".into())};
    let target=target.parse::<u64>().map_err(|_|"装备标识无效")?;
    let value=weight as f32;
    if !weight.is_finite()||!value.is_finite(){return Err("请输入有效权重".into());}
    let mut inner=state.inner.lock().unwrap();inner.last_ui=Instant::now();
    if !inner.view.connected||generation!=inner.view.generation{return Err("连接已变化，请重新选择形态键".into());}
    if action!=3{
        let k=inner.view.targets.iter().find(|t|t.handle==target.to_string()).and_then(|t|t.keys.iter().find(|k|k.index==key)).ok_or("装备或形态键已失效")?;
        if action==1&&(value<k.minimum||value>k.maximum){return Err("权重超出此形态键的范围".into());}
    }
    if action==3{inner.queue.clear();}
    else if action==1&&inner.queue.back().is_some_and(|p|p.action==1&&p.target==target&&p.key==key){inner.queue.pop_back();}
    if inner.queue.len()>=64{inner.queue.clear();inner.queue.push_back(Pending{action:3,target:0,key:0,weight:0.});return Err("操作过快，已释放本窗口控制".into());}
    inner.queue.push_back(Pending{action,target,key,weight:value});Ok(())
}
fn show(app:&tauri::AppHandle){
    let state=app.state::<Arc<State>>();state.inner.lock().unwrap().last_ui=Instant::now();state.visible.store(true,Ordering::Release);
    if let Some(w)=app.get_webview_window("main"){let _=w.show();let _=w.unminimize();let _=w.set_focus();}
}
fn hide(app:&tauri::AppHandle,release:bool){
    let state=app.state::<Arc<State>>();if release{state.release();}
    state.visible.store(false,Ordering::Release);
    if let Some(w)=app.get_webview_window("main"){let _=w.hide();}
    let pid=state.inner.lock().unwrap().view.pid;if pid!=0{unsafe{mhm_focus_game(pid)};}
}
#[tauri::command]
fn hide_window(app:tauri::AppHandle){hide(&app,false);}
#[tauri::command]
fn exit_app(app:tauri::AppHandle){app.state::<Arc<State>>().release();app.exit(0);}
struct Game{handle:*mut c_void,pid:u32}
impl Drop for Game{fn drop(&mut self){unsafe{mhm_close_game(self.handle)}}}
fn worker(app:tauri::AppHandle,state:Arc<State>){
    let shortcut=Shortcut::new(Some(Modifiers::CONTROL|Modifiers::SHIFT),Code::KeyM);
    let mut game:Option<Game>=None;let mut sequence=0u64;let mut registered=false;let mut shortcut_notified=false;
    loop{
        let fixed=state.auto_pid.load(Ordering::Acquire);
        let wanted=if fixed!=0{fixed}else{unsafe{mhm_find_game()}};
        let dead=game.as_ref().is_some_and(|g|unsafe{mhm_game_alive(g.handle)==0});
        if dead&&fixed!=0{app.exit(0);return;}
        if dead||game.as_ref().is_some_and(|g|g.pid!=wanted){game=None;state.disconnected();}
        if game.is_none()&&wanted!=0{
            let handle=unsafe{mhm_open_game(wanted)};
            if !handle.is_null(){game=Some(Game{handle,pid:wanted});}
            else if fixed!=0{app.exit(0);return;}
        }
        let foreground=unsafe{mhm_foreground_pid()};
        let in_scope=game.as_ref().is_some_and(|g|foreground==g.pid||foreground==std::process::id());
        if in_scope&&!registered{
            match app.global_shortcut().register(shortcut){
                Ok(())=>{registered=true;shortcut_notified=false;state.inner.lock().unwrap().view.shortcut_error=None;}
                Err(error)=>{
                    state.inner.lock().unwrap().view.shortcut_error=Some(format!("Ctrl＋Shift＋M 注册失败，请检查快捷键占用或运行权限：{error}"));
                    if !shortcut_notified{show(&app);shortcut_notified=true;}
                }
            }
        }else if !in_scope&&registered{let _=app.global_shortcut().unregister(shortcut);registered=false;}
        if let Some(ref g)=game{
            let minimized=app.get_webview_window("main").is_some_and(|w|w.is_minimized().unwrap_or(false));
            let pending={
                let mut s=state.inner.lock().unwrap();s.view.pid=g.pid;
                if state.visible.load(Ordering::Acquire)&&!minimized&&s.last_ui.elapsed()>Duration::from_millis(1500){
                    s.queue.clear();unsafe{mhm_disconnect(g.handle)};s.view.connected=false;s.view.targets.clear();s.view.source="0".into();
                    s.epoch+=1;s.view.generation=s.epoch.to_string();None
                }else{Some(s.queue.pop_front().unwrap_or(Pending{action:0,target:0,key:0,weight:0.}))}
            };
            if let Some(p)=pending{
                sequence=match sequence.checked_add(1){Some(v)=>v,None=>{app.exit(1);return;}};
                let request=protocol::request(p.action,sequence,p.target,p.key,p.weight);let mut bytes=[0u8;RESPONSE_BYTES];
                let result=unsafe{mhm_exchange(g.handle,request.as_ptr(),request.len(),bytes.as_mut_ptr(),bytes.len())};
                if result==0{let connected=state.inner.lock().unwrap().view.connected;if connected{state.disconnected();}}
                else{match protocol::response(&bytes,sequence,g.pid){
                    Ok(response)=>{
                        let mut s=state.inner.lock().unwrap();
                        if result==2{s.queue.clear();s.epoch+=1;s.view.generation=s.epoch.to_string();}
                        s.view.connected=true;s.view.source=response.source;s.view.targets=response.targets;
                        if response.result!=0{s.view.error=Some(match response.result{
                            1=>"权重或输入无效，原贡献保留",2=>"装备正在更换，请重新选择",3=>"控制容量不足，原贡献保留",
                            4=>"形态键互斥冲突，原贡献保留",5=>"运行时尚未准备好",_=>"控制请求未接受，请检查运行时日志"}.into());}
                        else if p.action!=0{s.view.error=None;}
                    }
                    Err(error)=>{unsafe{mhm_disconnect(g.handle)};state.disconnected();state.inner.lock().unwrap().view.error=Some(error);}
                }}
            }
        }
        std::thread::sleep(Duration::from_millis(250));
    }
}
fn pid_argument(args:&[String])->u32{
    args.windows(2).find(|a|a[0]=="--game-pid").and_then(|a|a[1].parse::<u32>().ok()).unwrap_or(0)
}
fn main(){
    let pid=pid_argument(&std::env::args().collect::<Vec<_>>());
    let state=Arc::new(State::new(pid));
    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app,args,_|{
            let pid=pid_argument(&args);
            if pid!=0{app.state::<Arc<State>>().auto_pid.store(pid,Ordering::Release);}else{show(app);}
        }))
        .plugin(tauri_plugin_global_shortcut::Builder::new().with_handler(|app,_,event|{
            if event.state()!=ShortcutState::Pressed{return;}
            let state=app.state::<Arc<State>>();let pid=state.inner.lock().unwrap().view.pid;
            let foreground=unsafe{mhm_foreground_pid()};
            if foreground!=pid&&foreground!=std::process::id(){return;}
            let minimized=app.get_webview_window("main").is_some_and(|w|w.is_minimized().unwrap_or(false));
            if state.visible.load(Ordering::Acquire)&&!minimized{hide(app,false);}else{show(app);}
        }).build())
        .manage(state.clone())
        .invoke_handler(tauri::generate_handler![snapshot,change,hide_window,exit_app])
        .setup(move|app|{
            let handle=app.handle().clone();let state=state.clone();
            std::thread::spawn(move||worker(handle,state));
            if pid==0{show(app.handle());}Ok(())
        })
        .on_window_event(|window,event|{
            if let WindowEvent::CloseRequested{api,..}=event{api.prevent_close();hide(window.app_handle(),true);}
        })
        .run(tauri::generate_context!()).expect("Tauri control center failed");
}
#[cfg(test)]
mod tests{
    use super::*;
    #[test]fn disconnect_invalidates_generation_and_queued_gestures(){
        let state=State::new(42);
        {let mut s=state.inner.lock().unwrap();s.view.connected=true;s.view.source="99".into();
            s.queue.push_back(Pending{action:1,target:9,key:0,weight:1.});}
        state.disconnected();let s=state.inner.lock().unwrap();
        assert!(!s.view.connected);assert_eq!(s.view.generation,"1");assert_eq!(s.view.source,"0");assert!(s.queue.is_empty());
    }
    #[test]fn close_replaces_all_queued_weights_with_one_release(){
        let state=State::new(42);
        state.inner.lock().unwrap().queue.push_back(Pending{action:1,target:9,key:0,weight:1.});
        state.release();state.release();let s=state.inner.lock().unwrap();
        assert_eq!(s.queue.len(),1);assert_eq!(s.queue.front().unwrap().action,3);
    }
}
