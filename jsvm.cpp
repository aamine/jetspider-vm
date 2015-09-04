/*
    jsvm.cpp - SpiderMonkey bytecode VM interface
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DEBUG 1
#include "jsapi.h"
#undef JS_THREADSAFE
#include "jsscript.h"
#include "jsfun.h"
#include "jsopcode.h"
#include "jsxdrapi.h"
#include "jscntxt.h"

using namespace JS;
using namespace js;

static JSClass globalClass = {
    "global",
    JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub,         // add
    JS_PropertyStub,         // del
    JS_PropertyStub,         // get
    JS_StrictPropertyStub,   // set
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    NULL
};

enum run_mode {
    RM_EXEC,
    RM_EXEC_PRINT,
    RM_DISASM
};

static int run(enum run_mode, JSContext *ctx, const char *filename, const char *function);
static void reportError(JSContext *ctx, const char *message, JSErrorReport *report);
static JSObject * loadScript(JSContext *ctx, JSObject *global, const char *filename);
static bool hasFileExt(const char *path, const char *ext);
static volatile int disassemble(JSContext *ctx, JSObject *global, JSObject *script, const char *function);
static volatile int execute(JSContext *ctx, JSObject *global, JSObject *script, bool print_result);
static char * readContent(JSContext *ctx, const char *filename, size_t *len);
static JSObject * decodeJSC(JSContext *ctx, JSObject *global, const char *buf, size_t len);
static void exposeGlobalFunction(JSContext *ctx, JSObject *global, JSObject *fun);
static JSObject * decodeJSFunction(JSXDRState *xdr);
static JSObject * decodeJSScript(JSXDRState *xdr);
static bool printJSValue(JSContext *ctx, jsval val);
static bool defineBuiltinFunctions(JSContext *ctx, JSObject *global);

static bool enableTracing = false;

int
main(int argc, const char *argv[])
{
    enum run_mode mode = RM_EXEC;
    int i = 1;

    while (argc > i && argv[i][0] == '-') {
        if (strcmp(argv[i], "--disassemble") == 0 || strcmp(argv[i], "-d") == 0) {
            mode = RM_DISASM;
        }
        else if (strcmp(argv[i], "--print") == 0 || strcmp(argv[i], "-p") == 0) {
            mode = RM_EXEC_PRINT;
        }
        else if (strcmp(argv[i], "--trace") == 0 || strcmp(argv[i], "-t") == 0) {
            enableTracing = true;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options] SOURCE\n", argv[0]);
            printf("Options:\n");
            printf("\t-p, --print\tExecutes source and print the last result.\n");
            printf("\t-t, --trace\tExecutes source with tracing.\n");
            printf("\t-d, --disassemble\tDisassembles source.\n");
            exit(0);
        }
        else {
            fprintf(stderr, "%s: error: unknown option: %s\n", argv[0], argv[i]);
            exit(1);
        }
        i++;
    }
    int n_args = argc - i;

    JSRuntime *runtime = JS_NewRuntime(8L * 1024L * 1024L);
    if (!runtime) {
        fprintf(stderr, "JS_NewRuntime failed\n");
        exit(1);
    }

    JSContext *ctx = JS_NewContext(runtime, 8192);
    if (!ctx) {
        fprintf(stderr, "JS_NewContext failed\n");
        exit(1);
    }
    JS_SetErrorReporter(ctx, reportError);

    int status;
    if (mode == RM_EXEC || mode == RM_EXEC_PRINT) {
        if (n_args != 1) {
            fprintf(stderr, "jsvm: wrong number of arguments\n");
            exit(1);
        }
        const char *filename = argv[i++];
        status = run(mode, ctx, filename, NULL);
    }
    else if (mode == RM_DISASM) {
        if (n_args == 0 || n_args > 2) {
            fprintf(stderr, "jsvm: wrong number of arguments\n");
            exit(1);
        }
        const char *filename = argv[i++];
        const char *function = (n_args > 1 ? argv[i++] : (const char*)0);
        status = run(RM_DISASM, ctx, filename, function);
    }
    else {
        fprintf(stderr, "[FATAL] bad run mode: %d", mode);
        exit(3);
    }

    JS_DestroyContext(ctx);
    JS_DestroyRuntime(runtime);
    JS_ShutDown();

    exit(status);
}

static volatile void
jsvm_breakpoint1(void)
{
    ;
}

static int
run(enum run_mode mode, JSContext *ctx, const char *filename, const char *function)
{
    JSAutoRequest ar(ctx);

    JSObject *global = JS_NewCompartmentAndGlobalObject(ctx, &globalClass, NULL);
    if (!global) {
        return 1;
    }

    JSAutoEnterCompartment ac;
    if (!ac.enter(ctx, global)) {
        return 1;
    }

    if (!JS_InitStandardClasses(ctx, global)) {
        fprintf(stderr, "could not initialize standard JS classes");
        return 1;
    }
    if (!defineBuiltinFunctions(ctx, global)) {
        return 1;
    }

    JSObject *script = loadScript(ctx, global, filename);
    if (!script) return 1;
    AutoObjectRooter root(ctx, script);

    jsvm_breakpoint1();
    if (mode == RM_DISASM) {
        return disassemble(ctx, global, script, function);
    }
    else if (mode == RM_EXEC || mode == RM_EXEC_PRINT) {
        return execute(ctx, global, script, (mode == RM_EXEC_PRINT ? true : false));
    }
    else {
        fprintf(stderr, "[FATAL] bad run mode: %d\n", (int)mode);
        return 1;
    }
}

static void
reportError(JSContext *ctx, const char *message, JSErrorReport *report)
{
     fprintf(stderr, "%s:%u:%s\n",
         report->filename ? report->filename : "[no filename]",
         (unsigned int) report->lineno,
         message);
}

static JSObject *
loadScript(JSContext *ctx, JSObject *global, const char *filename)
{
    size_t len;
    char *buf = readContent(ctx, filename, &len);
    if (!buf) return NULL;

    if (hasFileExt(filename, ".js")) {
        JS_SetOptions(ctx, JS_GetOptions(ctx) | JSOPTION_COMPILE_N_GO);
        return JS_CompileScript(ctx, global, buf, len, filename, 1);
    }
    else if (hasFileExt(filename, ".jsc")) {
        return decodeJSC(ctx, global, buf, len);
    }
    else {
        fprintf(stderr, "could not detect file type: %s\n", filename);
        return NULL;
    }
}

static bool
hasFileExt(const char *path, const char *ext)
{
    char *s = strrchr(path, '.');
    if (!s) return false;
    return strcmp(s, ext) == 0;
}

static volatile int
disassemble(JSContext *ctx, JSObject *global, JSObject *script, const char *function)
{
    if (function) {
        jsval fun;
        JS_GetProperty(ctx, global, function, &fun);
        if (JSVAL_IS_VOID(fun) || JSVAL_IS_NULL(fun)) {
            fprintf(stderr, "function not defined: %s\n", function);
            return 1;
        }
        JSObject *funObj = JSVAL_TO_OBJECT(fun);
        if (!funObj->isFunction()) {
            fprintf(stderr, "is not a function: %s\n", function);
            return 1;
        }
        JSFunction *f = JS_ValueToFunction(ctx, fun);
        JSScript *fbody = FUN_SCRIPT(f);
        jsvm_breakpoint1();
        js_Disassemble(ctx, fbody, false, stdout);
    }
    else {
        JSScript *toplevel = (JSScript*)script->privateData;
        jsvm_breakpoint1();
        js_Disassemble(ctx, toplevel, false, stdout);
    }
    return 0;
}

static volatile int
execute(JSContext *ctx, JSObject *global, JSObject *script, bool print_result)
{
    if (enableTracing) {
        ctx->logfp = stderr;
        ctx->logPrevPc = NULL;
    }
    jsval ret;
    if (!JS_ExecuteScript(ctx, global, script, &ret)) {
        fprintf(stderr, "execution failed\n");
        return 1;
    }
    if (print_result) {
        if (!printJSValue(ctx, ret)) {
            return 1;
        }
    }
    return 0;
}

static char *
readContent(JSContext *ctx, const char *filename, size_t *len)
{
    struct stat st;

    // Read binary into memory buffer
    if (stat(filename, &st) != 0) {
        perror(filename);
        return NULL;
    }
    *len = st.st_size;

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "could not open file: %s\n", filename);
        return NULL;
    }
    char *buf = (char*)ctx->malloc(*len + 1);
    if (!buf) {
        fprintf(stderr, "could not allocate memory\n");
        return NULL;
    }
    size_t r = fread(buf, 1, *len, fp);
    fclose(fp);
    if (r < *len && !feof(fp)) {
        perror(filename);
        return NULL;
    }
    buf[*len] = '\0';
    return buf;
}

static JSObject *
decodeJSC(JSContext *ctx, JSObject *global, const char *buf, size_t len)
{
    JSXDRState *xdr = JS_XDRNewMem(ctx, JSXDR_DECODE);
    if (!xdr) {
        fprintf(stderr, "could not initialize XDR session\n");
        ctx->free((void*)buf);
        return NULL;
    }
    // buf is managed by XDR from here.
    JS_XDRMemSetData(xdr, (void*)buf, (uint32)len);

    uint32 magic;
    if (!JS_XDRUint32(xdr, &magic)) {
        JS_XDRDestroy(xdr);
        return NULL;
    }

    uint32 nunits;   // (nunits-1) functions and 1 toplevel script
    if (!JS_XDRUint32(xdr, &nunits)) {
        JS_XDRDestroy(xdr);
        return NULL;
    }

    for (int i = 0; i < nunits - 1; i++) {
        JSObject *fun = decodeJSFunction(xdr);
        if (!fun) return NULL;
        fun->setParent(global);   // set static link
        exposeGlobalFunction(ctx, global, fun);
    }
    JSObject *toplevelScript = decodeJSScript(xdr);

    uint32 left = JS_XDRMemDataLeft(xdr);
    if (left > 0) {
        fprintf(stderr, "[FATAL] data remained in XDR buffer (%d bytes)\n", left);
        JS_XDRDestroy(xdr);
        return NULL;
    }
    JS_XDRDestroy(xdr);
    return toplevelScript;
}

static void
exposeGlobalFunction(JSContext *ctx, JSObject *global, JSObject *fun)
{
    JSAtom *name = fun->getFunctionPrivate()->atom;
    global->defineProperty(ctx, ATOM_TO_JSID(name), ObjectValue(*fun), NULL, NULL, JSPROP_ENUMERATE);
}

static JSObject *
decodeJSFunction(JSXDRState *xdr)
{
    JSObject *fun;
    if (!js_XDRFunctionObject(xdr, &fun)) {
        fprintf(stderr, "could not load a function\n");
        return NULL;
    }
    return fun;
}

static JSObject *
decodeJSScript(JSXDRState *xdr)
{
    JSScript *script = (JSScript*)xdr->cx->malloc(sizeof(JSScript));
    if (!script) {
        fprintf(stderr, "could not allocate JSScript\n");
        return NULL;
    }
    script->globalsOffset = 0xFF;   // make invalid to avoid assertion
    JSBool hasMagic = true;
    if (!js_XDRScript(xdr, &script, &hasMagic)) {
        fprintf(stderr, "could not load program\n");
        return NULL;
    }
    JSObject *scriptObj = js_NewScriptObject(xdr->cx, script);
    if (!scriptObj) {
        fprintf(stderr, "could not allocate script object\n");
        xdr->cx->free((void*)script);
        return NULL;
    }
    return scriptObj;
}

static bool
printJSValue(JSContext *ctx, jsval val)
{
    JSString *str = JS_ValueToString(ctx, val);
    if (!str) return false;
    char *s = JS_EncodeString(ctx, str);
    if (!s) return false;
    fprintf(stdout, "%s\n", s);
    JS_free(ctx, s);
    fflush(stdout);
    return true;
}

// from shell/js.cpp
static JSBool
jsvm_f_p(JSContext *cx, uintN argc, jsval *vp)
{
    jsval *argv;
    uintN i;
    JSString *str;
    char *bytes;

    argv = JS_ARGV(cx, vp);
    for (i = 0; i < argc; i++) {
        str = JS_ValueToString(cx, argv[i]);
        if (!str)
            return JS_FALSE;
        bytes = JS_EncodeString(cx, str);
        if (!bytes)
            return JS_FALSE;
        fprintf(stdout, "%s%s", i ? " " : "", bytes);
        JS_free(cx, bytes);
    }

    fputc('\n', stdout);
    fflush(stdout);

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

static bool
defineBuiltinFunctions(JSContext *ctx, JSObject *global)
{
    if (!JS_DefineFunction(ctx, global, "p", jsvm_f_p, 0, 0)) {
        fprintf(stderr, "could not define print()");
        return false;
    }
    return true;
}
