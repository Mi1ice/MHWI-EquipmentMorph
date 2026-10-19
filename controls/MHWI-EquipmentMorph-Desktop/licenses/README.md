# Supplemental dependency licenses

Some locked crates omit workspace license files from their published archives. These unmodified texts supplement the notices collected from locally installed dependencies:

- alloc-stdlib 0.3.0: [Dropbox BSD license at the crate source revision](https://github.com/dropbox/rust-alloc-no-stdlib/blob/0a81fd6928ea3b33c8cd484aa4575d50ffb98012/LICENSE).
- selectors 0.38.0, The Servo Project Developers: [Mozilla Public License 2.0](https://www.mozilla.org/media/MPL/2.0/index.txt), as identified by its Cargo metadata and source file headers.
- tauri-plugin 2.7.2: [Tauri workspace MIT license at the crate source revision](https://github.com/tauri-apps/tauri/blob/59b63ab500da9859f12d8888c3298bc29bba0b92/LICENSE-MIT).
- webview2-com, webview2-com-macros, webview2-com-sys 0.39.1: [workspace MIT license at the crate source revision](https://github.com/wravery/webview2-rs/blob/edc2caf886175ccaebe86078c9cfe1ae2a187328/LICENSE).

This folder contains license texts, not copies of the dependencies. Source versions and checksums remain pinned by Cargo.lock.
