//! Temporary helper: build a minimal valid payload.bin from boot.img +
//! xbl_config.img so the full-OTA extraction path can be exercised without
//! downloading a real OTA.
use std::path::Path;

use payload_extract::proto::chromeos_update_engine::{
    DeltaArchiveManifest, Extent, InstallOperation, PartitionInfo, PartitionUpdate,
};
use prost::Message;

fn install_op(data_offset: u64, data_length: u64, block_size: u32) -> InstallOperation {
    let blocks = data_length.div_ceil(block_size as u64);
    InstallOperation {
        r#type: payload_extract::proto::chromeos_update_engine::install_operation::Type::Replace
            as i32,
        data_offset: Some(data_offset),
        data_length: Some(data_length),
        dst_extents: vec![Extent {
            start_block: Some(0),
            num_blocks: Some(blocks),
        }],
        ..Default::default()
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let boot_path = Path::new(&args[1]);
    let xbl_path = Path::new(&args[2]);
    let out_path = Path::new(&args[3]);
    let boot = std::fs::read(boot_path).unwrap();
    let xbl = std::fs::read(xbl_path).unwrap();
    let block_size = 4096u32;

    let boot_part = PartitionUpdate {
        partition_name: "boot".to_string(),
        new_partition_info: Some(PartitionInfo {
            size: Some(boot.len() as u64),
            ..Default::default()
        }),
        operations: vec![install_op(0, boot.len() as u64, block_size)],
        ..Default::default()
    };
    let xbl_part = PartitionUpdate {
        partition_name: "xbl_config".to_string(),
        new_partition_info: Some(PartitionInfo {
            size: Some(xbl.len() as u64),
            ..Default::default()
        }),
        operations: vec![install_op(boot.len() as u64, xbl.len() as u64, block_size)],
        ..Default::default()
    };
    let manifest = DeltaArchiveManifest {
        block_size: Some(block_size),
        partitions: vec![boot_part, xbl_part],
        ..Default::default()
    };
    let manifest_bytes = manifest.encode_to_vec();

    let mut payload = Vec::new();
    payload.extend_from_slice(b"CrAU");
    payload.extend_from_slice(&2u64.to_be_bytes());
    payload.extend_from_slice(&(manifest_bytes.len() as u64).to_be_bytes());
    payload.extend_from_slice(&0u32.to_be_bytes());
    payload.extend_from_slice(&manifest_bytes);
    payload.extend_from_slice(&boot);
    payload.extend_from_slice(&xbl);
    std::fs::write(out_path, &payload).unwrap();
    println!(
        "wrote {} bytes (boot={} xbl={} manifest={})",
        payload.len(),
        boot.len(),
        xbl.len(),
        manifest_bytes.len()
    );
}
