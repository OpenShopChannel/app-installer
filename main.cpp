#include <iostream>
#include <wiiuse/wpad.h>
#include <sdcard/wiisd_io.h>
#include <cstring>
#include <fat.h>
#include <sys/stat.h>
#include <unistd.h>
#include "miniz.h"

extern "C" {
#include "libpatcher/libpatcher.h"
}

// Video
static void* xfb = nullptr;
static GXRModeObj* rmode = nullptr;

// I/O devices
const DISC_INTERFACE *sd_slot = &__io_wiisd;
const DISC_INTERFACE *usb = &__io_usbstorage;

template<typename T>
static void return_loop(T error_code)
{
  std::cout << "Error Code: " << error_code << std::endl;
  std::cout << "Press the HOME button to return to the Wii Menu" << std::endl;
  while (true)
  {
    WPAD_ScanPads();
    u32 pressed = WPAD_ButtonsDown(0);
    if (pressed & WPAD_BUTTON_HOME)
      exit(0);

    VIDEO_WaitVSync();
  }
}


s32 init_fat() {
  // Initialize IO
  usb->startup();
  sd_slot->startup();

  // Check if the SD Card is inserted
  bool isInserted = __io_wiisd.isInserted();

  // Try to mount the SD Card before the USB
  if (isInserted) {
    fatMountSimple("fat", sd_slot);
  } else {
    // Since the SD Card is not inserted, we will attempt to mount the USB.
    bool USB = __io_usbstorage.isInserted();
    if (USB) {
      fatMountSimple("fat", usb);
    } else {
      std::cout << "Please insert either an SD Card or USB." << std::endl;
      __io_usbstorage.shutdown();
      __io_wiisd.shutdown();
      return_loop("AAAAA");
    }
  }
  return 0;
}

static void power_cb() {
  STM_ShutdownToIdle();
}

static void reset_cb(u32 level, void *unk) {
  WII_ReturnToMenu();
}

int main()
{
  SYS_SetPowerCallback(power_cb);
  SYS_SetResetCallback(reset_cb);

  VIDEO_Init();

  rmode = VIDEO_GetPreferredMode(nullptr);
  xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight,
               rmode->fbWidth * VI_DISPLAY_PIX_SZ);
  VIDEO_Configure(rmode);
  VIDEO_SetNextFramebuffer(xfb);
  VIDEO_SetBlack(FALSE);
  VIDEO_Flush();
  VIDEO_WaitVSync();
  if (rmode->viTVMode & VI_NON_INTERLACE)
    VIDEO_WaitVSync();

  bool _success = apply_patches();
  if (!_success) {
    std::cout << "Failed to apply patches!" << std::endl;
    sleep(5);
    WII_ReturnToMenu();
  }

  WPAD_Init();
  ISFS_Initialize();
  CONF_Init();

  init_fat();

  std::cout << "OSC Title Installer (c) Open Shop Channel 2023" << std::endl << std::endl;
  std::cout << "Moves the downloaded homebrew app onto your SD Card or USB." << std::endl;

  // Content 3 of the current app is the zipped up contents that will be moved to the SD Card.
  s32 es_fd = ES_OpenContent(3);
  if (es_fd < 0)
  {
    // Internal ES error
    std::cout << "Failed to open content number 3." << std::endl;
    return_loop(es_fd);
  }

  u32 data_size = ES_SeekContent(es_fd, 0, SEEK_END);
  ES_SeekContent(es_fd, 0, SEEK_SET);

  u32 aligned_length = data_size;
  u32 remainder = aligned_length % 32;
  if (remainder != 0) {
    aligned_length += 32 - remainder;
  }

  void* zip_data = aligned_alloc(32, aligned_length);
  s32 ret = ES_ReadContent(es_fd, reinterpret_cast<u8*>(zip_data), data_size);
  if (ret < 0)
  {
    std::cout << "Failed to read content number 3." << std::endl;
    return_loop(ret);
  }

  ES_CloseContent(es_fd);

  mz_zip_archive zip_archive;
  std::memset(&zip_archive, 0, sizeof(zip_archive));
  mz_bool success = mz_zip_reader_init_mem(&zip_archive, zip_data, data_size, 0);
  if (!success)
  {
    std::cout << "Failed to init zip reader" << std::endl;
    return_loop("ZIP_INIT_FAIL");
  }

  mz_uint imax = mz_zip_reader_get_num_files(&zip_archive);
  char full_path[2048];
  for (mz_uint i = 0; i < imax; i++) {
    // One day we will get <format>.
    bzero(full_path, 1024);
    mz_zip_archive_file_stat file_stat;
    mz_zip_reader_file_stat(&zip_archive, i, &file_stat);
    std::sprintf(full_path, "fat:/%s", file_stat.m_filename);
    if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) {
      if (full_path[strlen(full_path)-1] == '/') {
        full_path[strlen(full_path)-1] = 0x00;
      }
      if (mkdir(full_path, 0777) < 0 && errno != EEXIST) {
        std::cout << "Failed to create directory." << std::endl;
        return_loop("DIR_CREATE_FAIL");
      }
    } else {
      if (mz_zip_reader_extract_to_file(&zip_archive, i, full_path, 0) < 0) {
        std::cout << "Failed to file " << full_path << " to device." << std::endl;
        return_loop("ZIP_EXTRACT_FAIL");
      }
    }
  }

  mz_zip_reader_end(&zip_archive);

  // Now we need to rename the 4th content to be the 2nd.
  u64 title_id{};
  ret = ES_GetTitleID(&title_id);
  if (ret < 0)
  {
    std::cout << "Failed to get current title id" << std::endl;
    return_loop(ret);
  }

  u32 lower = static_cast<u32>((title_id & 0xFFFFFFFF00000000LL) >> 32);
  u32 upper = static_cast<u32>(title_id & 0xFFFFFFFFLL);

  char executable[128];
  char old_path[128];
  sprintf(executable, "/title/%08x/%08x/content/00000002.app", lower, upper);
  sprintf(old_path, "/title/%08x/%08x/content/00000004.app", lower, upper);

  ISFS_Delete(executable);
  ret = ISFS_CreateFile(executable, 0, 3, 3, 3);
  if (ret < 0) {
    std::cout << "Failed to create new executable" << std::endl;
    return_loop(ret);
  }

  s32 fs_fd = ISFS_Open(executable, ISFS_OPEN_WRITE);
  if (fs_fd < 0) {
    std::cout << "Failed opening new executable" << std::endl;
    return_loop(fs_fd);
  }

  // Content 4 of the current app is the new executable.
  es_fd = ES_OpenContent(4);
  if (es_fd < 0)
  {
    // Internal ES error
    std::cout << "Failed to open content number 4." << std::endl;
    return_loop(es_fd);
  }

  data_size = ES_SeekContent(es_fd, 0, SEEK_END);
  ES_SeekContent(es_fd, 0, SEEK_SET);

  aligned_length = data_size;
  remainder = aligned_length % 32;
  if (remainder != 0) {
    aligned_length += 32 - remainder;
  }

  void* exe_data = aligned_alloc(32, aligned_length);
  ret = ES_ReadContent(es_fd, reinterpret_cast<u8*>(exe_data), data_size);
  if (ret < 0)
  {
    std::cout << "Failed to read content number 4." << std::endl;
    return_loop(ret);
  }

  ES_CloseContent(es_fd);

  ret = ISFS_Write(fs_fd, exe_data, data_size);
  if (ret < 0) {
    std::cout << "Error writing executable" << std::endl;
    return_loop(ret);
  }

  ret = ISFS_Close(fs_fd);
  if (ret < 0) {
    std::cout << "Error closing executable" << std::endl;
    return_loop(ret);
  }

  ISFS_Delete(old_path);

  std::cout << "Successfully completed!" << std::endl;
  std::cout << "Next time you load this channel, you will be forwarded to the homebrew." << std::endl;
  std::cout << "Returning to Wii Menu in ";
  for (int i = 5; i > 0; i--) {
    std::cout << i;
    std::cout << ".";
    sleep(1);
  }


  // Unmount fat and deinit IO
  fatUnmount("fat:/");
  __io_usbstorage.shutdown();
  __io_wiisd.shutdown();

  // Now return
  WII_ReturnToMenu();
  return 0;
}
