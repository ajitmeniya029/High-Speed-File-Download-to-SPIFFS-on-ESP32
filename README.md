This project demonstrates secure file download over HTTPS on ESP32 with storage in SPIFFS.
It includes:
Wi-Fi STA connection with auto-reconnect.
HTTPS client with certificate bundle validation.
Error handling for network, timeout, and storage issues.
Retry with exponential backoff on transient failures.
Buffered SPIFFS writes with free-space checks to ensure reliable storage.
Maintains a minimum download + write speed target of 400 KBps.
