# SQLite3 Demo with spiffs

This example updates a boot count. Check `EXAMPLE_FRESH_START` if boot count is always 1.  
Then logs a counter into `log` table every 10 seconds.  
Also gets number of logs every minutes.

```shell
idf.py set-target esp32
idf.py build flash
idf.py monitor
```
