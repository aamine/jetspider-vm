# JetSpider VM

SpiderMonkey VM interface (command).
The JetSpider compiler generates executable files for this VM.
See https://github.com/aamine/jetspider-course for compiler details.

## Platform

I tried on Mac OS X 10.10 (Yosemite) and 10.11 (El Capitan).

## Build

1. Get SpiderMonkey 1.8.5 from https://developer.mozilla.org/ja/docs/SpiderMonkey
2. Apply SpiderMonkey.diff
3. Build SpiderMonkey with debugging and static library support,
   by "configure --enable-debug --enable-static".
4. "make install" in SpiderMonkey srcdir.
5. "make" in this directory.

## License

Same as SpiderMonkey (MPL / GPL / LGPL).
