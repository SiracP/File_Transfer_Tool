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

## 🔭 Screenshots

- **Discover Screen**
![image](https://github.com/user-attachments/assets/95ff537f-b467-4b63-b0e3-51ed78c5750e)


- **Received Files Screen**
![image](https://github.com/user-attachments/assets/d6cc3240-68e7-448d-8533-42041f505f97)


- **Sent Files Screen**
![image](https://github.com/user-attachments/assets/362bac74-1ba7-42e7-b54a-541b03fccdd7)


- **Sending Transfer Request**
![image](https://github.com/user-attachments/assets/41f75f2e-0641-4242-a7ba-b3644c345a7b)


- **Accept/Reject Transfer**
![image](https://github.com/user-attachments/assets/0efad451-c9aa-4d87-9547-0b729c24898a)


- **Receiver Refused**
![image](https://github.com/user-attachments/assets/9379462f-65e1-4f76-ac80-fc5e3f8c3262)


- **Sending the File**
![image](https://github.com/user-attachments/assets/92830244-ec0d-4bc0-9fcd-bb630d3e6065)


- **File Transfer Completed Sender**
![image](https://github.com/user-attachments/assets/31c7745f-e9eb-4d85-bbf5-3cc4eb9aaaff)


- **File Transfer Completed Receiver**
![image](https://github.com/user-attachments/assets/0bbdb74f-8df8-4400-8f87-ce1b0be8d355)


