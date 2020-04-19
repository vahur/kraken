#define WIN32_LEAN_AND_MEAN
#define NOCRYPT
#define NOGDI
#define NOSERVICE
#define NOMCX
#define NOIME
#include <Windows.h>

#include "kraken.c"

static HANDLE hstdout;

#define print_cstr(x) print_str((x), sizeof(x) - sizeof(char))

static void print_str(const char *str, size_t len)
{
    WriteFile(hstdout, str, (DWORD)len, NULL, NULL);
}

static void print_wstr(const wchar_t *str)
{
    char buf[512];
    int len = WideCharToMultiByte(GetConsoleOutputCP(), 0, str, -1, buf, sizeof(buf), NULL, NULL);

    if (len > 1)
        WriteFile(hstdout, buf, (DWORD)len, NULL, NULL);
}

static void print_status(const struct kraken_status *status)
{
    char buf[256];

    int count = wsprintfA(buf, "Liquid: %d.%d C, Fan: %d RPM, Pump: %d RPM, Firmware: %u.%u.%u\r\n",
            status->liquid_temp.degrees, status->liquid_temp.tenths, status->fan_rpm, status->pump_rpm,
            (unsigned)status->fw_ver.major, (unsigned)status->fw_ver.minor, (unsigned)status->fw_ver.incremental);

    print_str(buf, count);
}

static void print_error(const struct kraken_error *err)
{
    char buf[512];

    int count = wsprintfA(buf, "%s: error code %08XH\r\n", err->msg, err->code);
    print_str(buf, count);
}

void __stdcall mainCRTStartup()
{
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    int retcode = 1;
    struct kraken_error err = {S_OK, NULL};
    hstdout = GetStdHandle(STD_OUTPUT_HANDLE);

    HANDLE kraken = kraken_open(&err);

    if (kraken == INVALID_HANDLE_VALUE) {
        if (FAILED(err.code))
            print_error(&err);
        else
            print_cstr("Kraken not found\r\n");

        goto exit_free_argv;
    }

    if (argc > 1) {
        int success;

        if (lstrcmpW(L"silent", argv[1]) == 0) {
            success = kraken_set_profile(kraken, SILENT, &err);
        }
        else if (lstrcmpW(L"perf", argv[1]) == 0) {
            success = kraken_set_profile(kraken, PERF, &err);
        }
        else if (lstrcmpW(L"test", argv[1]) == 0) {
            success = kraken_set_fan_pump_level(kraken, 0, 25, &err)
                  && kraken_set_fan_pump_level(kraken, 1, 60, &err);
        }
        else if (lstrcmpW(L"max", argv[1]) == 0) {
            success = kraken_set_fan_pump_level(kraken, 0, 100, &err)
                  && kraken_set_fan_pump_level(kraken, 1, 100, &err);
        }
        else {
            print_cstr("Unknown option: ");
            print_wstr(argv[1]);
            print_cstr("\r\n");
            goto exit;
        }

        if (!success) {
            print_error(&err);
            goto exit;
        }

        retcode = 0;
        goto exit;
    }

    LocalFree(argv);
    argv = NULL;

    while (1) {
        struct kraken_status status;

        if (!kraken_read_status(kraken, &status, &err)) {
            print_error(&err);
            goto exit;
        }

        print_status(&status);
    }

exit:
    CloseHandle(kraken);
exit_free_argv:
    LocalFree(argv);
    ExitProcess(retcode);
}
