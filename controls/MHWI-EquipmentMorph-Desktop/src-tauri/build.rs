fn main() {
    cc::Build::new()
        .cpp(true)
        .std("c++20")
        .flag("/utf-8")
        .file("../native/client.cpp")
        .compile("mhm_desktop_client");
    println!("cargo:rerun-if-changed=../native/client.cpp");
    println!("cargo:rerun-if-changed=../../../runtime/EquipmentMorphControlBridge/Protocol.hpp");
    println!("cargo:rerun-if-changed=../../../runtime/EquipmentMorphControlBridge/Pipe.hpp");
    println!("cargo:rustc-link-lib=user32");
    // Generate the taskbar mark in OUT_DIR; no generated binary belongs in source.
    let icon=std::path::PathBuf::from(std::env::var_os("OUT_DIR").unwrap()).join("control-center.ico");
    let mut dib=vec![0u8;40+32*32*4+128];
    dib[0..4].copy_from_slice(&40u32.to_le_bytes());
    dib[4..8].copy_from_slice(&32u32.to_le_bytes());
    dib[8..12].copy_from_slice(&64u32.to_le_bytes());
    dib[12..14].copy_from_slice(&1u16.to_le_bytes());
    dib[14..16].copy_from_slice(&32u16.to_le_bytes());
    let points=[(4f32,24f32),(10.,8.),(16.,18.),(22.,8.),(28.,24.)];
    for y in 0..32{for x in 0..32{
        let ink=points.windows(2).any(|p|{
            let(dx,dy)=(p[1].0-p[0].0,p[1].1-p[0].1);
            let t=(((x as f32-p[0].0)*dx+(y as f32-p[0].1)*dy)/(dx*dx+dy*dy)).clamp(0.,1.);
            (x as f32-p[0].0-t*dx).hypot(y as f32-p[0].1-t*dy)<1.5
        });
        if ink{let at=40+((31-y)*32+x)*4;dib[at..at+4].copy_from_slice(&[20,20,20,255]);}
    }}
    let mut bytes=vec![0,0,1,0,1,0,32,32,0,0,1,0,32,0];
    bytes.extend_from_slice(&(dib.len() as u32).to_le_bytes());bytes.extend_from_slice(&22u32.to_le_bytes());bytes.extend(dib);
    std::fs::write(&icon,bytes).unwrap();
    tauri_build::try_build(tauri_build::Attributes::new().windows_attributes(
        tauri_build::WindowsAttributes::new().window_icon_path(icon))).expect("Tauri resources");
}
