# JetSpider VM

SpiderMonkey VM interface (command).
The JetSpider compiler generates executable files for this VM.
See https://github.com/aamine/jetspider-course for compiler details.

## Build

1. Get SpiderMonkey 1.8.5 from https://developer.mozilla.org/ja/docs/SpiderMonkey
2. Apply SpiderMonkey.diff
3. Build SpiderMonkey with debugging support, by "configure --enable-debug"
4. Copy libmozjs185.dylib (or .so) as libmozjs_debug.a
5. make

## License

Same as SpiderMonkey (MPL / GPL / LGPL).
