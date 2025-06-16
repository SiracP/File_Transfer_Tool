# File Transfer Tool

Lightweight Qt-based application for discovering peers on a LAN and sending files reliably with SHA-256 integrity checks and user-mediated accept/reject.

---

## 🚀 Features

- **Peer Discovery**  
  Broadcasts UDP “hello” packets and lists all responding hosts (hostname, IP, OS, MAC, ping).

- **Transfer Request / Consent**  
  Sender first sends a transfer request; receiver sees a dialog with sender name, IP, file name & size, and can **Accept** or **Reject**.

- **Chunked TCP File Transfer**  
  File is sent in 64 KiB chunks over TCP, with a progress dialog on both sides.

- **Integrity Check**  
  SHA-256 hash is computed before send and verified after receive. Transfer is acknowledged only if the hash matches.

- **User-Friendly Progress & Messages**  
  - Sender: “Sending…”, “Awaiting approval…”, “Success!” or “Rejected.”  
  - Receiver: “Receiving…”, “File saved at …”, “Integrity OK” or “HASH_ERROR”.

---

## ⚙️ Requirements

- **Qt 6** (Core, Gui, Widgets, Network)  
- **CMake 3.16+**  
- A C++17-capable compiler (GCC, Clang, MSVC)

---
