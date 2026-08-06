#include <Arduino.h>

#if defined(ESP32)
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "USB.h"
#include "USBMSC.h"
#include "firmware_msc_fat.h"

namespace {

USBMSC g_msc;

constexpr uint32_t kFlashfsJournalSize = 32u * 8u;
constexpr uint32_t kFlashfsJournalItems = 32u;
constexpr uint32_t kFlashfsScanStep = 128u;
constexpr uint32_t kErasedWord = 0xFFFFFFFFu;

// g_disk covers all sectors except the BLACKBOX data range.
// For LBA < g_bbStartLba:  g_disk offset = lba * DISK_SECTOR_SIZE
// For LBA >= g_bbEndLba:   g_disk offset = (lba - g_bbSectors) * DISK_SECTOR_SIZE
uint8_t*  g_disk        = nullptr;
uint16_t  g_totalSectors = 0;
uint16_t  g_metaLbas    = 0;   // tableSectors + 2
uint16_t  g_bbStartLba  = 0;
uint16_t  g_bbEndLba    = 0;
uint16_t  g_bbSectors   = 0;
uint16_t  g_infoStartLba = 0;
uint16_t  g_infoEndLba   = 0;

const esp_partition_t* g_flashPart = nullptr;
uint32_t g_exportSize = 0;
uint32_t g_usedSize   = 0;
uint32_t g_visibleSize = 0;
char     g_infoText[256] = {};
uint32_t g_infoLen = 0;

struct JournalItem { uint32_t logBegin; uint32_t logEnd; };

uint32_t detectUsedSize()
{
  uint32_t used = 0;
  for(uint32_t i = 0; i < kFlashfsJournalItems; i++)
  {
    JournalItem item = {0, 0};
    if(esp_partition_read_raw(g_flashPart, g_exportSize + i * sizeof(item), &item, sizeof(item)) != ESP_OK) break;
    if(item.logBegin == kErasedWord && item.logEnd == kErasedWord) break;
    if(item.logEnd != kErasedWord) { used = std::max(used, std::min(item.logEnd, g_exportSize)); continue; }
    if(item.logBegin != kErasedWord && item.logBegin < g_exportSize)
    {
      for(uint32_t a = item.logBegin; a < g_exportSize; a += kFlashfsScanStep)
      {
        uint32_t m = 0;
        if(esp_partition_read_raw(g_flashPart, a, &m, 4) != ESP_OK) break;
        if(m == kErasedWord) { used = std::max(used, a); break; }
      }
    }
  }
  return std::min(used, g_exportSize);
}

bool setupDisk()
{
  g_flashPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if(g_flashPart && g_flashPart->size > kFlashfsJournalSize)
  {
    g_exportSize = g_flashPart->size - kFlashfsJournalSize;
    g_usedSize   = detectUsedSize();
  }
  g_visibleSize = g_usedSize;

  const unsigned long total = static_cast<unsigned long>(g_exportSize);
  const unsigned long used  = static_cast<unsigned long>(g_usedSize);
  const unsigned long pct   = total ? (used * 100ul / total) : 0ul;
  g_infoLen = static_cast<uint32_t>(snprintf(
    g_infoText, sizeof(g_infoText),
    "ESPFC Blackbox MSC\r\n"
    "Export bytes: %lu\r\n"
    "Used bytes:   %lu\r\n"
    "Used:         %lu%%\r\n"
    "File: BLACKBOX.BBL\r\n",
    total, used, pct
  ));
  if(g_infoLen >= sizeof(g_infoText)) g_infoLen = sizeof(g_infoText) - 1;

  const uint16_t bbSectors   = static_cast<uint16_t>(FAT_SIZE_TO_SECTORS(g_visibleSize));
  const uint16_t infoSectors = static_cast<uint16_t>(FAT_SIZE_TO_SECTORS(g_infoLen));
  const uint32_t dataSectors = 16u + bbSectors + infoSectors;

  bool isFat16 = false;
  uint16_t tableSectors = fat_sectors_per_alloc_table(dataSectors, false);
  g_totalSectors = static_cast<uint16_t>(dataSectors + tableSectors + 2u);
  if(g_totalSectors > 0xFF4)
  {
    isFat16 = true;
    tableSectors = fat_sectors_per_alloc_table(dataSectors, true);
    g_totalSectors = static_cast<uint16_t>(dataSectors + tableSectors + 2u);
  }

  g_metaLbas   = static_cast<uint16_t>(tableSectors + 2u);
  g_bbStartLba = g_metaLbas;
  g_bbEndLba   = static_cast<uint16_t>(g_metaLbas + bbSectors);
  g_bbSectors  = bbSectors;
  g_infoStartLba = g_bbEndLba;
  g_infoEndLba   = static_cast<uint16_t>(g_infoStartLba + infoSectors);

  // Allocate disk covering everything except the BLACKBOX data range.
  const uint16_t diskSectors = static_cast<uint16_t>(g_totalSectors - bbSectors);
  g_disk = static_cast<uint8_t*>(calloc(diskSectors, DISK_SECTOR_SIZE));
  if(!g_disk) return false;

  fat_boot_sector_t* boot = fat_add_boot_sector(g_disk, g_totalSectors, tableSectors, fat_file_system_type(isFat16), "ESPFC-BBLOG", 0x45534643);
  (void)fat_add_table(g_disk, boot, isFat16);
  fat_add_root_file(g_disk, 0, "BLACKBOX", "BBL", g_visibleSize, 2, isFat16);
  fat_add_root_file(g_disk, 1, "INFO", "TXT", g_infoLen, static_cast<uint16_t>(2u + bbSectors), isFat16);

  // Place INFO.TXT content in g_disk using hole-mapped offset.
  uint8_t* infoDst = g_disk + (g_infoStartLba - bbSectors) * DISK_SECTOR_SIZE;
  memcpy(infoDst, g_infoText, g_infoLen);

  return true;
}

// Translate a virtual LBA to a g_disk byte offset; returns -1 for BLACKBOX range.
int32_t gdiskOffset(uint32_t lba)
{
  if(lba < g_bbStartLba) return static_cast<int32_t>(lba * DISK_SECTOR_SIZE);
  if(lba < g_bbEndLba)   return -1;
  return static_cast<int32_t>((lba - g_bbSectors) * DISK_SECTOR_SIZE);
}

int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
  if(!g_disk) { memset(buffer, 0, bufsize); return static_cast<int32_t>(bufsize); }

  int32_t off = gdiskOffset(lba);
  if(off >= 0)
  {
    memcpy(buffer, g_disk + off + offset, bufsize);
  }
  else if(g_flashPart)
  {
    const uint32_t absOff = static_cast<uint32_t>(lba - g_bbStartLba) * DISK_SECTOR_SIZE + offset;
    if(absOff < g_visibleSize)
    {
      const uint32_t canRead = std::min<uint32_t>(bufsize, g_visibleSize - absOff);
      if(esp_partition_read_raw(g_flashPart, absOff, buffer, canRead) != ESP_OK)
        memset(buffer, 0, bufsize);
      else if(canRead < bufsize)
        memset(reinterpret_cast<uint8_t*>(buffer) + canRead, 0, bufsize - canRead);
    }
    else
    {
      memset(buffer, 0, bufsize);
    }
  }
  else
  {
    memset(buffer, 0, bufsize);
  }

  return static_cast<int32_t>(bufsize);
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  if(!g_disk) return static_cast<int32_t>(bufsize);

  int32_t off = gdiskOffset(lba);
  if(off >= 0) memcpy(g_disk + off + offset, buffer, bufsize);
  // BLACKBOX range: acknowledged but not written to flash.

  return static_cast<int32_t>(bufsize);
}

bool onStartStop(uint8_t power_condition, bool start, bool load_eject)
{
  (void)power_condition; (void)start; (void)load_eject;
  return true;
}

void initBlackboxMsc()
{
  if(!setupDisk()) return;

  g_msc.vendorID("ESPFC");
  g_msc.productID("Blackbox MSC");
  g_msc.productRevision("1.0");
  g_msc.onStartStop(onStartStop);
  g_msc.onRead(onRead);
  g_msc.onWrite(onWrite);
  g_msc.mediaPresent(true);
  if(g_msc.begin(g_totalSectors, DISK_SECTOR_SIZE))
    USB.begin();
}

} // namespace
#endif

void setup()
{
#if defined(ESP32)
  const esp_partition_t* app0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if(app0)
  {
    esp_ota_set_boot_partition(app0);
  }

  initBlackboxMsc();
#endif
}

void loop()
{
  delay(1000);
}
