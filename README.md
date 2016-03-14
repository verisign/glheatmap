# glheatmap
OpenGL-based interactive IPv4 heatmap

# Copyrights
Copyright (C) 2016 Verisign, Inc. and licensed under the GNU GPL, version 2

Portions Copyright 2007 by The Measurement Factory, Inc and licensed under the GNU GPL, version 2

# Usage
```
$ cat timestamp-ip.dat | ./glheatmap [opts]
```

## Options
```
-a           Automatically adjust point size
-p size      Specify point size
-b packets   Pause playback at specified packet count
-s ip:port   Read from TCP socket at ip:port instead of stdin.  IPv4 only at this time
-u           Input contains just IP addresses, no timestamps
-F           Fullscreen mode
-m keep/set  Mask input IP addresses.  'keep' bits unchanged; 'set' bits always set
```

## Input format
Default input format is two whitespace separated fields containing timestamps and IP addresses.  Timestamps are in Unix epoch, as returned by ```time()```.  IP addresses are parsed with ```inet_pton(AF_INET, ...)```.  For example:

    1448866220	105.12.72.7
    1448866221	117.239.180.112
    1448866221	169.55.139.9
    1448866221	162.242.247.106
    1448866221	169.53.133.141
    1448866222	85.214.78.14
    1448866222	128.173.54.190
    1448866222	118.72.253.197
    1448866223	182.163.224.144
    1448866223	120.52.20.15
    1448866224	61.138.38.46
    1448866224	95.211.169.7
    1448866224	67.217.167.60
    1448866225	14.48.40.218
    1448866225	218.29.231.26
    1448866226	89.248.168.48
    1448866226	23.253.229.234


# Example Visualization

The following video was generated using the glheatmap software:

[![IP Heatmap Analysis of November 30 Root Server Traffic](http://img.youtube.com/vi/f8J52JhukLo/0.jpg)](http://www.youtube.com/watch?v=f8J52JhukLo)
