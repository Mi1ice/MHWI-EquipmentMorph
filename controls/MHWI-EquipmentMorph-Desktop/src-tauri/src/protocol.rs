use serde::Serialize;

pub const RESPONSE_BYTES: usize = 39_256;
#[derive(Clone, Serialize, Debug, PartialEq)]
pub struct Key {
    pub index: u32, pub name: String, pub minimum: f32, pub maximum: f32,
    pub requested: f32, pub applied: f32, pub source: String,
    pub owned: bool, pub contribution: f32,
}
#[derive(Clone, Serialize, Debug, PartialEq)]
pub struct Target { pub handle: String, pub part: u32, pub model: String, pub keys: Vec<Key> }
#[derive(Clone, Debug)]
pub struct Response { pub result: i32, pub source: String, pub targets: Vec<Target> }
fn u32_at(b: &[u8], at: usize) -> u32 { u32::from_le_bytes(b[at..at+4].try_into().unwrap()) }
fn u64_at(b: &[u8], at: usize) -> u64 { u64::from_le_bytes(b[at..at+8].try_into().unwrap()) }
fn float(b: &[u8], at: usize) -> Result<f32,String> {
    let value=f32::from_le_bytes(b[at..at+4].try_into().unwrap());
    if value.is_finite(){Ok(value)}else{Err("非有限权重".into())}
}
fn text(b:&[u8])->Result<String,String>{
    let end=b.iter().position(|v|*v==0).ok_or("标识符缺少结束符")?;
    Ok(std::str::from_utf8(&b[..end]).map_err(|_|"标识符编码无效")?.to_owned())
}
pub fn request(action:u32,sequence:u64,target:u64,key:u32,weight:f32)->[u8;40]{
    let mut b=[0u8;40];
    for(at,value)in[(0,0x434d484du32),(4,1),(8,40),(12,action),(32,key)]{b[at..at+4].copy_from_slice(&value.to_le_bytes());}
    b[16..24].copy_from_slice(&sequence.to_le_bytes());b[24..32].copy_from_slice(&target.to_le_bytes());
    b[36..40].copy_from_slice(&weight.to_le_bytes());b
}
pub fn response(b:&[u8],sequence:u64,pid:u32)->Result<Response,String>{
    if b.len()!=RESPONSE_BYTES || u32_at(b,0)!=0x434d484d || u32_at(b,4)!=1
        || u32_at(b,8) as usize!=RESPONSE_BYTES || u64_at(b,16)!=sequence || u32_at(b,40)!=pid
        || u32_at(b,48)>5 || u32_at(b,52)>320 {return Err("控制桥接响应不兼容".into());}
    let mut targets=Vec::new();let mut seen=std::collections::BTreeSet::new();
    for i in 0..u32_at(b,48) as usize{
        let at=56+i*160;let handle=u64_at(b,at);let part=u32_at(b,at+8);
        let first=u32_at(b,at+152) as usize;let count=u32_at(b,at+156) as usize;
        if handle==0||!seen.insert(handle)||part>4||count>64||count!=u32_at(b,at+12) as usize
            || first+count>u32_at(b,52) as usize {return Err("装备范围无效".into());}
        let mut keys=Vec::new();
        for k in 0..count{
            let key=856+(first+k)*120;
            let minimum=float(b,key+80)?;let maximum=float(b,key+84)?;
            if u64_at(b,key)!=handle||u32_at(b,key+8) as usize!=k||u32_at(b,key+12)>1||minimum>maximum{return Err("形态键范围无效".into());}
            keys.push(Key{index:k as u32,name:text(&b[key+16..key+80])?,minimum,maximum,
                requested:float(b,key+96)?,applied:float(b,key+100)?,source:u64_at(b,key+104).to_string(),
                owned:u32_at(b,key+12)!=0,contribution:float(b,key+112)?});
        }
        targets.push(Target{handle:handle.to_string(),part,model:text(&b[at+16..at+144])?,keys});
    }
    Ok(Response{result:u32_at(b,12) as i32,source:u64_at(b,24).to_string(),targets})
}
#[cfg(test)]
mod tests{
    use super::*;
    #[test]fn request_preserves_64_bit_handles_and_float32(){
        let b=request(1,99,u64::MAX-2,63,0.13724681f32);
        assert_eq!(u64_at(&b,24),u64::MAX-2);assert_eq!(float(&b,36).unwrap(),0.13724681f32);
    }
    #[test]fn reject_wrong_size_and_header(){
        assert!(response(&[],1,2).is_err());assert!(response(&[0;RESPONSE_BYTES],1,2).is_err());
    }
    fn fixture()->[u8;RESPONSE_BYTES]{
        let mut b=[0;RESPONSE_BYTES];
        for(at,v)in[(0,0x434d484du32),(4,1),(8,RESPONSE_BYTES as u32),(40,4242),(48,1),(52,1),
            (64,1),(68,1),(212,1)]{b[at..at+4].copy_from_slice(&v.to_le_bytes());}
        b[16..24].copy_from_slice(&99u64.to_le_bytes());b[24..32].copy_from_slice(&(u64::MAX-1).to_le_bytes());
        b[56..64].copy_from_slice(&(u64::MAX-2).to_le_bytes());b[72..81].copy_from_slice(b"synthetic");
        b[856..864].copy_from_slice(&(u64::MAX-2).to_le_bytes());b[868..872].copy_from_slice(&1u32.to_le_bytes());
        b[872..881].copy_from_slice(b"vrc.blink");b[940..944].copy_from_slice(&1f32.to_le_bytes());
        for at in [952,956,968]{b[at..at+4].copy_from_slice(&0.13724681f32.to_le_bytes());}
        b[960..968].copy_from_slice(&(u64::MAX-1).to_le_bytes());b
    }
    #[test]fn decode_actual_layout_without_javascript_integer_rounding(){
        let parsed=response(&fixture(),99,4242).unwrap();
        assert_eq!(parsed.source,(u64::MAX-1).to_string());
        assert_eq!(parsed.targets[0].handle,(u64::MAX-2).to_string());
        assert_eq!(parsed.targets[0].keys[0].name,"vrc.blink");
        assert_eq!(parsed.targets[0].keys[0].contribution,0.13724681f32);
        assert!(parsed.targets[0].keys[0].owned);
    }
    #[test]fn reject_wrong_process_sequence_key_ranges_and_nonfinite_weights(){
        let b=fixture();assert!(response(&b,98,4242).is_err());assert!(response(&b,99,4243).is_err());
        for(at,v)in[(212,65u32),(864,1),(868,2),(952,f32::NAN.to_bits()),(956,f32::INFINITY.to_bits())]{
            let mut bad=b;bad[at..at+4].copy_from_slice(&v.to_le_bytes());assert!(response(&bad,99,4242).is_err());
        }
    }
    #[test]fn reject_unterminated_or_invalid_utf8_identifiers(){
        let mut b=fixture();b[872..936].fill(b'x');assert!(response(&b,99,4242).is_err());
        b=fixture();b[872]=255;assert!(response(&b,99,4242).is_err());
    }
}
