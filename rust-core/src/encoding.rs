//! Encoding utilities — base64, URL encode/decode.

use base64::{Engine, engine::general_purpose::STANDARD};

pub fn base64_encode(data: &[u8]) -> String {
    STANDARD.encode(data)
}

pub fn base64_decode(data: &[u8]) -> String {
    match std::str::from_utf8(data) {
        Ok(s) => match STANDARD.decode(s) {
            Ok(decoded) => String::from_utf8_lossy(&decoded).into_owned(),
            Err(e) => format!("[Decode error: {}]", e),
        },
        Err(e) => format!("[UTF-8 error: {}]", e),
    }
}

pub fn url_encode(text: &str) -> String {
    urlencoding::encode(text).into_owned()
}

pub fn url_decode(text: &str) -> String {
    urlencoding::decode(text)
        .unwrap_or_else(|_| text.into())
        .into_owned()
}
