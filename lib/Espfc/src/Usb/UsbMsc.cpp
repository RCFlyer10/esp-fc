#include "UsbMsc.h"
#include <platform.h>

#if defined(CONFIG_TINYUSB_MSC_ENABLED) && CONFIG_TINYUSB_MSC_ENABLED
#include "FirmwareMSC.h"
#endif

#ifdef USE_FLASHFS
extern "C" {
    int flashfsReadAbs(uint32_t address, uint8_t *data, unsigned int len);
    void flashfsWriteAbs(uint32_t address, const uint8_t *data, unsigned int len);
    uint32_t flashfsGetSize(void);
    int flashfsInit(void);
}
#endif

namespace Espfc {

bool usbMscAvailable = false;

[[maybe_unused]] static int32_t msc_read_cb(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
#ifdef USE_FLASHFS
    const uint32_t block = 512;
    uint32_t address = lba * block + offset;
    return flashfsReadAbs(address, reinterpret_cast<uint8_t*>(buffer), bufsize);
#else
    (void)lba; (void)offset; (void)buffer; (void)bufsize;
    return 0;
#endif
}

[[maybe_unused]] static int32_t msc_write_cb(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
#ifdef USE_FLASHFS
    const uint32_t block = 512;
    uint32_t address = lba * block + offset;
    flashfsWriteAbs(address, buffer, bufsize);
    return bufsize;
#else
    (void)lba; (void)offset; (void)buffer; (void)bufsize;
    return 0;
#endif
}

void initUsbMsc()
{
#if defined(CONFIG_TINYUSB_MSC_ENABLED) && CONFIG_TINYUSB_MSC_ENABLED
    // Ensure flashfs is initialized if available
#ifdef USE_FLASHFS
    flashfsInit();
    uint32_t size = flashfsGetSize();
    if(size == 0) return;
    FirmwareMSC msc;
    if(msc.begin())
    {
      usbMscAvailable = true;
    }
#endif
#endif
}

} // namespace Espfc
