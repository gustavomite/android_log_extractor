Coletor de logs do Android

Baixar platform tools:

https://dl.google.com/android/repository/platform-tools-latest-windows.zip


Compilar:

windres resource.rc -o resource.o

gcc extratorlog.c  resource.o -o ExtratorLogs.exe -mwindows -lcomdlg32 -lcomctl32 -lgdi32