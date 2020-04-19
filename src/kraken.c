#include <shellapi.h>
#include <SetupAPI.h>
#include <stdint.h>
#include <initguid.h>
#include <devpropdef.h>
#include <devguid.h>
#include <hidclass.h>

#define VENDOR_ID_NZXT 0x1e71
#define PRODUCT_ID_KRAKEN_X52 0x170e

enum kraken_profile_type {
    SILENT,
    PERF
};

struct kraken_temp {
    uint8_t degrees;
    uint8_t tenths;
};

struct kraken_fw_version {
    uint8_t major;
    uint8_t minor;
    uint8_t incremental;
};

struct kraken_status {
    struct kraken_temp liquid_temp;
    unsigned fan_rpm;
    unsigned pump_rpm;
    struct kraken_fw_version fw_ver;
};

struct kraken_error {
    uint32_t code;
    const char *msg;
};

struct kraken_status_packet {
    uint8_t status_code;
    uint8_t liquid_temp;
    uint8_t liquid_temp_tenths;
    uint8_t fan_rpm_hi;
    uint8_t fan_rpm_lo;
    uint8_t pump_rpm_hi;
    uint8_t pump_rpm_lo;
    uint8_t reserved1[3];
    uint8_t device_number;
    uint8_t fw_version_major;
    uint8_t reserved2[1];
    uint8_t fw_version_minor;
    uint8_t fw_version_incremental;
    uint8_t reserved3[2];
    uint8_t reserved4[64 - 16];
};

struct kraken_fan_level {
    uint8_t temp;
    uint8_t level;
};

struct kraken_profile {
    uint8_t pump;
    unsigned num_levels;
    const struct kraken_fan_level *levels;
};

static const struct kraken_fan_level kraken_silent_fan_levels[] = {
    {  0,   25 },
    {  35,  35 },
    {  40,  45 },
    {  45,  55 },
    {  50,  75 },
    {  55,  85 },
    {  60, 100 },
    { 100, 100 }
};

static const struct kraken_fan_level kraken_silent_pump_levels[] = {
    {   0,  50 },
    {  36,  60 },
    {  40,  70 },
    {  45,  80 },
    {  50,  90 },
    {  55, 100 },
    { 100, 100 }
};

static const struct kraken_fan_level kraken_perf_fan_levels[] = {
    {   0,  50 },
    {  40,  60 },
    {  45,  70 },
    {  50,  80 },
    {  55,  90 },
    {  60, 100 },
    { 100, 100 }
};

static const struct kraken_fan_level kraken_perf_pump_levels[] = {
    {   0,  70 },
    {  40,  80 },
    {  45,  85 },
    {  50,  90 },
    {  55, 100 },
    { 100, 100 }
};

#define NUM_ELEMS(x) (sizeof((x)) / sizeof((x)[0]))

static const struct kraken_profile kraken_silent_fan_profile = {
    0, NUM_ELEMS(kraken_silent_fan_levels), kraken_silent_fan_levels
};

static const struct kraken_profile kraken_perf_fan_profile = {
    0, NUM_ELEMS(kraken_perf_fan_levels), kraken_perf_fan_levels
};

static const struct kraken_profile kraken_silent_pump_profile = {
    1, NUM_ELEMS(kraken_silent_pump_levels), kraken_silent_pump_levels
};

static const struct kraken_profile kraken_perf_pump_profile = {
    1, NUM_ELEMS(kraken_perf_pump_levels), kraken_perf_pump_levels
};

#if defined(DEBUG)
    #define CLR(x) SecureZeroMemory(&(x), sizeof((x)))
#else
    #define CLR(x) ZeroMemory(&(x), sizeof((x)))
#endif

static void kraken_set_err(struct kraken_error *err, const char *msg) {
    err->code = GetLastError();
    err->msg = msg;
}

static HANDLE kraken_init(HDEVINFO hdev, PSP_DEVICE_INTERFACE_DATA dev_if, struct kraken_error *err)
{
    DEVPROPTYPE dev_prop_type;
    UINT16 version = 0;

    // Get version
    SetupDiGetDeviceInterfacePropertyW(hdev, dev_if, &DEVPKEY_DeviceInterface_HID_VersionNumber,
            &dev_prop_type, (PBYTE)&version, sizeof(version), NULL, 0);

    BYTE buf[sizeof(SP_DEVICE_INTERFACE_DATA) + 1024 * sizeof(TCHAR)];
    PSP_DEVICE_INTERFACE_DETAIL_DATA dev_if_detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)buf;
    dev_if_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    if (!SetupDiGetDeviceInterfaceDetail(hdev, dev_if, dev_if_detail, sizeof(buf),
            NULL, NULL)) {
        kraken_set_err(err, "Cannot get device interface details");
        return INVALID_HANDLE_VALUE;
    }

    HANDLE handle = CreateFile(dev_if_detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (handle == INVALID_HANDLE_VALUE) {
        kraken_set_err(err, "Cannot open device file");
    }

    return handle;
}

HANDLE kraken_open(struct kraken_error *err)
{
    HDEVINFO hdev = SetupDiGetClassDevs(&GUID_DEVINTERFACE_HID, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

    if (hdev == INVALID_HANDLE_VALUE) {
        kraken_set_err(err, "Failed to get HID devices");
        return hdev;
    }

    DWORD i = 0;
    SP_DEVINFO_DATA dev_info;
    SP_DEVICE_INTERFACE_DATA dev_if;
    DEVPROPTYPE dev_prop_type;

    CLR(dev_info);
    dev_info.cbSize = sizeof(dev_info);

    CLR(dev_if);
    dev_if.cbSize = sizeof(dev_if);

    while (SetupDiEnumDeviceInfo(hdev, i++, &dev_info)) {
        UINT16 vendor_id;
        UINT16 product_id;

        if (SetupDiEnumDeviceInterfaces(hdev, &dev_info, &GUID_DEVINTERFACE_HID, 0, &dev_if)) {
            // Get vendor id
            if (!SetupDiGetDeviceInterfacePropertyW(hdev, &dev_if, &DEVPKEY_DeviceInterface_HID_VendorId,
                    &dev_prop_type, (PBYTE)&vendor_id, sizeof(vendor_id), NULL, 0)) {
                continue;
            }

            if (vendor_id != VENDOR_ID_NZXT) {
                continue;
            }

            // Get product id
            if (!SetupDiGetDeviceInterfacePropertyW(hdev, &dev_if, &DEVPKEY_DeviceInterface_HID_ProductId,
                    &dev_prop_type, (PBYTE)&product_id, sizeof(product_id), NULL, 0)) {
                continue;
            }

            if (product_id == PRODUCT_ID_KRAKEN_X52) {
                return kraken_init(hdev, &dev_if, err);
            }
        }
    }

    return INVALID_HANDLE_VALUE;
}

int kraken_read_status(HANDLE kraken, struct kraken_status *status, struct kraken_error *err)
{
    struct kraken_status_packet packet;
    DWORD num_read;

    if (!ReadFile(kraken, &packet, sizeof(packet), &num_read, NULL)) {
        kraken_set_err(err, "Cannot read status");
        return 0;
    }

    status->liquid_temp.degrees = packet.liquid_temp;
    status->liquid_temp.tenths = packet.liquid_temp_tenths;
    status->fan_rpm = ((packet.fan_rpm_hi & 0xff) << 8) | (packet.fan_rpm_lo & 0xff);
    status->pump_rpm = ((packet.pump_rpm_hi & 0xff) << 8) | (packet.pump_rpm_lo & 0xff);
    status->fw_ver.major = packet.fw_version_major;
    status->fw_ver.minor = packet.fw_version_minor;
    status->fw_ver.incremental = packet.fw_version_incremental;
    return 1;
}

static int kraken_send_fan_pump_profile(HANDLE kraken, const struct kraken_profile *profile, struct kraken_error *err)
{
    uint8_t packet[65];
    CLR(packet);

    packet[0] = 2;
    packet[1] = 77;
    uint8_t dest = 128 | (profile->pump ? 64 : 0); // 128 = save

    for (unsigned i = 0; i < profile->num_levels; i++) {
        packet[2] = dest | ((uint8_t)i);
        packet[3] = profile->levels[i].temp;
        packet[4] = profile->levels[i].level;

        DWORD num_written;

        if (!WriteFile(kraken, &packet, sizeof(packet), &num_written, NULL)) {
            kraken_set_err(err, "Failed to write fan/pump level from profile");
            return 0;
        }
    }

    return 1;
}

int kraken_set_fan_pump_level(HANDLE kraken, int pump, uint8_t level, struct kraken_error *err)
{
    struct kraken_fan_level fan_levels[] = { {0, level}, {60, 100} };
    struct kraken_profile profile = { pump, 2, fan_levels };

    return kraken_send_fan_pump_profile(kraken, &profile, err);
}

int kraken_set_profile(HANDLE kraken, enum kraken_profile_type profile_type, struct kraken_error *err)
{
    const struct kraken_profile *fan_profile;
    const struct kraken_profile *pump_profile;

    switch (profile_type) {
    case SILENT:
        fan_profile = &kraken_silent_fan_profile;
        pump_profile = &kraken_silent_pump_profile;
        break;
    case PERF:
        fan_profile = &kraken_perf_fan_profile;
        pump_profile = &kraken_perf_pump_profile;
        break;
    default:
        err->code = ERROR_INVALID_PARAMETER;
        err->msg = "Invalid profile type";
        return 0;
    }

    return kraken_send_fan_pump_profile(kraken, fan_profile, err)
        && kraken_send_fan_pump_profile(kraken, pump_profile, err);
}
